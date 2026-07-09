// =============================================================
//  waveshare_display.c — Waveshare 12.3" Display + GT911 Touch
//
//  Target:  ESP32-P4-Nano
//  Panel:   Himax HX8399-C, 2-lane MIPI-DSI, portrait 720×1920
//           75 MHz DPI, RGB565 framebuffer
//           (P4-Nano has 2 DSI lanes per schematic)
//  Touch:   Goodix GT911, I2C (espressif/esp_lcd_touch_gt911)
//  Panel driver: espressif/esp_lcd_hx8399
//
//  LVGL sees landscape 1920×720.
//  Flush cb PPA-rotates into DPI double framebuffers.
//  on_refresh_done ISR calls lv_display_flush_ready at vsync.
//
//  Timing source: ws_panel_12_3_a_4lane_mode / _init[]
//  from Waveshare Linux kernel driver (open source).
//  Controller confirmed from unlock sequence 0xB9,0x83,0x10,0x2E.
// =============================================================

#include "waveshare_display.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_touch.h"
#include "esp_ldo_regulator.h"
#include "hal/axi_icm_ll.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "hat_pins.h"
#include <string.h>

// HX8399 panel driver — espressif/esp_lcd_hx8399 from ESP Component Registry
#include "esp_lcd_hx8399.h"

// GT911 touch — espressif/esp_lcd_touch_gt911
#include "esp_lcd_touch_gt911.h"

static const char *TAG = "ws_disp";

// ── Pin configuration (override via -D flags in platformio.ini) ──────────
#ifndef WS_I2C_SDA
  #define WS_I2C_SDA          31
#endif
#ifndef WS_I2C_SCL
  #define WS_I2C_SCL          32
#endif
#ifndef WS_TOUCH_INT
  #define WS_TOUCH_INT        23
#endif
#ifndef WS_TOUCH_RST
  // GT911 hard-reset GPIO sets I2C address:
  //   INT low  during reset → addr 0x5D
  //   INT high during reset → addr 0x14
  // Not wired on current hardware — skip hard reset (relies on power-on
  // state / board-level strapping for the I2C address instead).
  #define WS_TOUCH_RST        -1
#endif
#ifndef WS_BACKLIGHT_GPIO
  #define WS_BACKLIGHT_GPIO   22
#endif
#ifndef WS_GT911_I2C_ADDR
  #define WS_GT911_I2C_ADDR   0x5D
#endif

// ── DSI / DPI timing (portrait 720×1920) ─────────────────────────────────
// Source: ws_panel_12_3_a_4lane_mode in Waveshare kernel driver.
// htotal = 720 + HFP(10) + HSync(10) + HBP(12) = 752
// vtotal = 1920 + VFP(64) + VSync(18) + VBP(4) = 2006
// P4-Nano has 2 DSI lanes only (confirmed from schematic).
// Lane rate from HX8399_PANEL_BUS_DSI_2CH_CONFIG() in esp_lcd_hx8399.h.
#define WS_DSI_LANE_NUM       2
#define WS_DSI_LANE_MBPS      950   // 2-lane rate per esp_lcd_hx8399 component
#define WS_DPI_CLK_MHZ        75    // reduced for 2-lane bandwidth
#define WS_HSYNC_PULSE_WIDTH  10
#define WS_HSYNC_BACK_PORCH   12
#define WS_HSYNC_FRONT_PORCH  10
#define WS_VSYNC_PULSE_WIDTH  18
#define WS_VSYNC_BACK_PORCH   4
#define WS_VSYNC_FRONT_PORCH  64

// Panel native resolution (portrait)
#define WS_PANEL_H  720
#define WS_PANEL_V  1920

// VDD_MIPI_DPHY is hardwired to internal LDO channel 3 on ESP32-P4 (SoC-level,
// not board-specific). Without powering this rail, the DSI PHY PLL never
// locks and esp_lcd_new_dsi_bus() hangs forever in its lock-wait loop.
#define WS_DSI_PHY_LDO_CHAN     3
#define WS_DSI_PHY_LDO_MV       2500

// ── Module-private handles ────────────────────────────────────────────────
static esp_lcd_panel_handle_t   s_panel   = NULL;
static esp_lcd_touch_handle_t   s_touch   = NULL;
static i2c_master_bus_handle_t  s_i2c     = NULL;
static esp_lcd_dsi_bus_handle_t s_dsi_bus = NULL;
static esp_ldo_channel_handle_t s_dsi_phy_ldo = NULL;
static ppa_client_handle_t      s_ppa     = NULL;
static void                    *s_rot_buf = NULL;

// ── vsync ISR → LVGL flush ready ─────────────────────────────────────────
static bool IRAM_ATTR prv_dpi_trans_done_cb(esp_lcd_panel_handle_t panel,
                                             esp_lcd_dpi_panel_event_data_t *edata,
                                             void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

// ── HX8399-C init sequence ────────────────────────────────────────────────
// Source: ws_panel_12_3_a_4lane_init[] from Waveshare kernel driver.
// Struct layout matches hx8399_lcd_init_cmd_t from esp_lcd_hx8399.h:
//   { .cmd, .data, .data_bytes, .delay_ms }
// This overrides the component's built-in default init sequence so we
// get the exact Waveshare 12.3" panel calibration (4-lane, 720×1920).
static const hx8399_lcd_init_cmd_t s_hx8399_init_cmds[] = {
    // Manufacturer command unlock — identifies controller as HX8399-C
    {0xB9, (uint8_t[]){0x83, 0x10, 0x2E},                                3,   0},
    // SETPOWER
    {0xB1, (uint8_t[]){0x7C, 0x25, 0x25, 0x8F, 0x8F, 0x4C, 0x87,
                       0x62, 0x1A, 0x7C, 0x7C, 0x4E, 0x4E},            13,   0},
    // SETDISP
    {0xB2, (uint8_t[]){0x00, 0x02, 0x00, 0x90, 0x14, 0x0C, 0x02,
                       0x0C, 0x02, 0x37},                               10,   0},
    // SETCYC
    {0xB4, (uint8_t[]){0x00, 0xFF, 0x02, 0xC0, 0x02, 0xC0, 0x00,
                       0x00, 0x08, 0x00, 0x04, 0x08, 0x00, 0x04,
                       0x04, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x01, 0x01},                               24,   0},
    // SETVCOM
    {0xB6, (uint8_t[]){0xAF, 0xAF},                                      2,   0},
    // SETGIP0
    {0xD3, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
                       0x10, 0x32, 0x10, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x07, 0x07, 0x07, 0x07, 0x57, 0x07,
                       0x07, 0x07, 0x07, 0x07},                         32,   0},
    // SETGIP1
    {0xD5, (uint8_t[]){0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x21, 0x20, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x18,
                       0x18, 0x02, 0x03, 0x00, 0x01, 0x06, 0x07,
                       0x04, 0x05, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18},                                      44,   0},
    // SETGIP2
    {0xD6, (uint8_t[]){0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x20, 0x21, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x18,
                       0x18, 0x05, 0x04, 0x07, 0x06, 0x01, 0x00,
                       0x03, 0x02, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18},                                      44,   0},
    // SETGAMMA
    {0xE0, (uint8_t[]){0x00, 0x14, 0x1F, 0x35, 0x3F, 0x3F, 0x37,
                       0x48, 0x07, 0x0C, 0x0E, 0x11, 0x13, 0x12,
                       0x14, 0x0F, 0x17, 0x00, 0x14, 0x1F, 0x35,
                       0x3F, 0x3F, 0x37, 0x48, 0x07, 0x0C, 0x0E,
                       0x11, 0x13, 0x12, 0x14, 0x0F, 0x17},             34,   0},
    // NOTE: SETMIPI (0xBA) is sent by esp_lcd_hx8399 component based on lane_num.
    // Do NOT include it here — the component handles lane configuration.
    // SETPANEL — BGR order
    {0xCC, (uint8_t[]){0x08},                                            1,   0},
    // SETOFFSET
    {0xC6, (uint8_t[]){0xFF, 0xF9},                                      2,   0},
    // NOTE: Sleep out (0x11) is sent by the esp_lcd_hx8399 component before
    // this array runs. Display ON (0x29) is sent HERE, not via the later
    // esp_lcd_panel_disp_on_off() call — panel_hx8399_init() calls
    // hx8399->init(panel) (which starts the DPI video stream) immediately
    // after this array finishes. Once video streaming begins, this board's
    // DSI host never reopens a command-FIFO window for further DBI commands,
    // so disp_on_off() hangs forever (ESP-IDF's mipi_dsi_hal_host_gen_write_dcs_command
    // has an unconditional, timeout-free spin-wait). Sending DISPON before
    // hx8399->init(panel) avoids the problem entirely.
    {0xB9, (uint8_t[]){0x00},                                            1,   0},   // close manufacturer page
    {0x29, NULL,                                                         0,  10},   // Display ON
};

// ── LVGL flush callback ───────────────────────────────────────────────────
static void prv_lvgl_flush_cb(lv_display_t *disp,
                               const lv_area_t *area,
                               uint8_t *px_map)
{
    if (!s_panel || !px_map || !s_rot_buf) {
        lv_display_flush_ready(disp);
        return;
    }

    esp_cache_msync(px_map,
                    (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    ppa_srm_oper_config_t cfg = {
        .in.buffer          = px_map,
        .in.pic_w           = LCD_H_RES,
        .in.pic_h           = LCD_V_RES,
        .in.block_w         = LCD_H_RES,
        .in.block_h         = LCD_V_RES,
        .in.block_offset_x  = 0,
        .in.block_offset_y  = 0,
        .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer         = s_rot_buf,
        .out.buffer_size    = (size_t)WS_PANEL_H * WS_PANEL_V * sizeof(uint16_t),
        .out.pic_w          = WS_PANEL_H,
        .out.pic_h          = WS_PANEL_V,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle     = PPA_SRM_ROTATION_ANGLE_90,
        .scale_x            = 1.0f,
        .scale_y            = 1.0f,
        .rgb_swap           = 0,
        .byte_swap          = 0,
        .mode               = PPA_TRANS_MODE_BLOCKING,
    };

    if (ppa_do_scale_rotate_mirror(s_ppa, &cfg) != ESP_OK) {
        // SW fallback
        const uint16_t *src = (const uint16_t *)px_map;
        uint16_t *dst = (uint16_t *)s_rot_buf;
        for (int y = 0; y < LCD_V_RES; y++)
            for (int x = 0; x < LCD_H_RES; x++)
                dst[y + (LCD_H_RES - 1 - x) * LCD_V_RES] = src[y * LCD_H_RES + x];
    }

    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, WS_PANEL_H, WS_PANEL_V, s_rot_buf);
}

// ── LVGL touch callback ───────────────────────────────────────────────────
// GT911 reports portrait coords (0..720, 0..1920).
// Map to LVGL landscape (0..1920, 0..720): lx=py, ly=(WS_PANEL_H-1)-px
static void prv_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (!s_touch) return;

    esp_lcd_touch_read_data(s_touch);

    // esp_lcd_touch_get_coordinates deprecated in esp_lcd_touch ≥2.0.
    // Use esp_lcd_touch_get_data (returns bool, fills esp_lcd_touch_point_data_t).
    esp_lcd_touch_point_data_t pt = {};
    uint8_t cnt = 0;
    esp_lcd_touch_get_data(s_touch, &pt, &cnt, 1);

    if (cnt > 0) {
        data->point.x = (lv_coord_t)pt.y;                   // portrait y → landscape x
        data->point.y = (lv_coord_t)(WS_PANEL_H - 1 - pt.x); // portrait x → landscape y
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── Display 5V rail enable (HAT LM2596S-5, ON/OFF gated by GPIO4) ─────────
// Active-low: drive LOW to enable. 10k pull-up on the HAT (R_5V_EN1, net
// 5V0_ENABLE) defaults this to disabled/HIGH if firmware never configures
// it, so the display stays unpowered (and can't backfeed ESP_3V3 via the
// DSI cable) until we explicitly turn it on here, first thing.
static void prv_display_power_enable(void)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << HAT_DISP_PWR_EN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);
    gpio_set_level(HAT_DISP_PWR_EN, 0);   // enable LM2596S-5 5V output
    vTaskDelay(pdMS_TO_TICKS(50));        // let the display's onboard rails settle
    ESP_LOGI(TAG, "display 5V rail enabled via GPIO%d", HAT_DISP_PWR_EN);
}

// ── Backlight via LEDC PWM ────────────────────────────────────────────────
static void prv_backlight_on(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_1,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);
    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_1,
        .timer_sel  = LEDC_TIMER_1,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = WS_BACKLIGHT_GPIO,
        .duty       = (1 << 10) - 1,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    ESP_LOGI(TAG, "backlight ON (GPIO%d)", WS_BACKLIGHT_GPIO);
}

// ── I2C bus init ──────────────────────────────────────────────────────────
static esp_err_t prv_i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = (gpio_num_t)WS_I2C_SDA,
        .scl_io_num                   = (gpio_num_t)WS_I2C_SCL,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c);
}

// ── HX8399-C DSI panel init ───────────────────────────────────────────────
static esp_err_t prv_panel_init(void)
{
    // Power VDD_MIPI_DPHY before touching the DSI bus — see WS_DSI_PHY_LDO_CHAN comment.
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = WS_DSI_PHY_LDO_CHAN,
        .voltage_mv = WS_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_dsi_phy_ldo),
                        TAG, "DSI PHY LDO power-on failed");

    // Boost AXI arbiter priority for both DW-GDMA master ports to max (15).
    // The DSI bridge's frame-buffer-fetch DMA runs on one of these; at default
    // priority (0, tied with CPU/cache) it loses arbitration for PSRAM access
    // under load and underruns ("can't fetch data from external memory fast
    // enough"). Underrun itself self-recovers, but the FOLLOWING disp_on_off()
    // DCS command then blocks forever in an unguarded HAL spin-wait, killing
    // the watchdog. Raising DMA priority prevents the underrun in the first place.
    axi_icm_ll_set_dw_gdma_qos_arbiter_prio(0, 15, 15);
    axi_icm_ll_set_dw_gdma_qos_arbiter_prio(1, 15, 15);

    // DSI bus — 2 lanes at 950 Mbps each (P4-Nano hardware limit)
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = WS_DSI_LANE_NUM,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = WS_DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus),
                        TAG, "DSI bus create failed");

    // DBI (command) channel — used by esp_lcd_hx8399 to send init sequence
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t io_cfg = HX8399_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &io_cfg, &io),
                        TAG, "Panel IO (DBI) create failed");

    // DPI (video) channel config
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = WS_DPI_CLK_MHZ,
        .virtual_channel    = 0,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs            = 2,
        .video_timing = {
            .h_size            = WS_PANEL_H,
            .v_size            = WS_PANEL_V,
            .hsync_pulse_width = WS_HSYNC_PULSE_WIDTH,
            .hsync_back_porch  = WS_HSYNC_BACK_PORCH,
            .hsync_front_porch = WS_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = WS_VSYNC_PULSE_WIDTH,
            .vsync_back_porch  = WS_VSYNC_BACK_PORCH,
            .vsync_front_porch = WS_VSYNC_FRONT_PORCH,
        },
        .flags.use_dma2d = true,
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,          // no hardware reset line wired; panel
                                       // relies on its own onboard POR plus
                                       // the SWRESET DBI command (see
                                       // panel_hx8399_reset() software path)
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };

    // Vendor config: pass our Waveshare init sequence and DSI/DPI config.
    // esp_lcd_hx8399 uses this to override its built-in defaults with
    // the 4-lane 720×1920 calibration from the Waveshare kernel driver.
    hx8399_vendor_config_t vendor_cfg = {
        .init_cmds      = s_hx8399_init_cmds,
        .init_cmds_size = sizeof(s_hx8399_init_cmds) / sizeof(s_hx8399_init_cmds[0]),
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_cfg,
            .lane_num   = WS_DSI_LANE_NUM,   // tells component to send HX8399_DSI_2_LANE
        },
    };
    panel_cfg.vendor_config = &vendor_cfg;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_hx8399(io, &panel_cfg, &s_panel),
                        TAG, "HX8399 panel create failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Panel reset failed");
    // Display ON is sent from inside s_hx8399_init_cmds (before video streaming
    // starts) — see comment there. Do NOT call esp_lcd_panel_disp_on_off() here.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG, "Panel init failed");

    // PPA SRM for hardware rotation in flush cb
    ppa_client_config_t ppa_cfg = { .oper_type = PPA_OPERATION_SRM };
    ESP_RETURN_ON_ERROR(ppa_register_client(&ppa_cfg, &s_ppa),
                        TAG, "PPA register failed");

    // Rotation buffer — portrait 720×1920 RGB565, 128-byte aligned for PPA
    s_rot_buf = heap_caps_aligned_alloc(128,
                    WS_PANEL_H * WS_PANEL_V * sizeof(uint16_t),
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_ERROR(s_rot_buf ? ESP_OK : ESP_ERR_NO_MEM,
                        TAG, "rot_buf alloc failed");

    ESP_LOGI(TAG, "HX8399-C ready — portrait %dx%d, LVGL landscape %dx%d",
             WS_PANEL_H, WS_PANEL_V, LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

// ── GT911 touch init ──────────────────────────────────────────────────────
static esp_err_t prv_touch_init(void)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr            = WS_GT911_I2C_ADDR,
        .scl_speed_hz        = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset       = 0,
        .lcd_cmd_bits        = 16,   // GT911 uses 16-bit register addresses
        .lcd_param_bits      = 8,
        .flags.disable_control_phase = 0,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c, &tp_io_cfg, &tp_io),
                        TAG, "GT911 IO create failed");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = WS_PANEL_H,
        .y_max        = WS_PANEL_V,
        .int_gpio_num = (gpio_num_t)WS_TOUCH_INT,
        .rst_gpio_num = (gpio_num_t)WS_TOUCH_RST,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch),
                        TAG, "GT911 init failed");
    ESP_LOGI(TAG, "GT911 touch ready  addr=0x%02X  INT=GPIO%d  RST=GPIO%d",
             WS_GT911_I2C_ADDR, WS_TOUCH_INT, WS_TOUCH_RST);
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────
esp_lcd_panel_handle_t ws_get_panel(void) { return s_panel; }

esp_err_t ws_display_init(lv_display_t **disp_out)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "init: display 5V rail enable GPIO%d", HAT_DISP_PWR_EN);
    prv_display_power_enable();

    ESP_LOGI(TAG, "init: I2C SDA=GPIO%d SCL=GPIO%d", WS_I2C_SDA, WS_I2C_SCL);
    if ((ret = prv_i2c_init()) != ESP_OK) return ret;

    // Touch init runs BEFORE the DSI panel sequence, deliberately. It's
    // electrically independent of DSI entirely (separate pins, separate
    // I2C protocol), and esp_lcd_touch_new_i2c_gt911() logs the GT911's
    // product ID register read (TouchPad_ID:...) as part of its normal
    // init flow. That gives us a clean, independent signal that the
    // display is powered and alive even if the DSI link below hangs.
    // Non-fatal: a missing/failed touch controller shouldn't prevent the
    // panel from displaying graphics, and (for bring-up) shouldn't block
    // us from finding out whether the DSI sequence below succeeds or not.
    ESP_LOGI(TAG, "init: GT911 touch");
    if ((ret = prv_touch_init()) != ESP_OK) {
        ESP_LOGW(TAG, "GT911 touch init failed (0x%x) — continuing without touch", ret);
    }

    ESP_LOGI(TAG, "init: backlight GPIO%d", WS_BACKLIGHT_GPIO);
    prv_backlight_on();

    ESP_LOGI(TAG, "init: HX8399-C  %d lanes  %d Mbps  %d MHz DPI",
             WS_DSI_LANE_NUM, WS_DSI_LANE_MBPS, WS_DPI_CLK_MHZ);
    if ((ret = prv_panel_init()) != ESP_OK) return ret;

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, prv_lvgl_flush_cb);

    esp_lcd_dpi_panel_event_callbacks_t dpi_cbs = {
        .on_refresh_done = prv_dpi_trans_done_cb,
    };
    ESP_ERROR_CHECK(
        esp_lcd_dpi_panel_register_event_callbacks(s_panel, &dpi_cbs, disp));

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, prv_lvgl_touch_cb);

    *disp_out = disp;
    ESP_LOGI(TAG, "ws_display_init complete  %dx%d landscape", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}
