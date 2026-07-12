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
#include "soc/mipi_dsi_host_struct.h"
#include "soc/mipi_dsi_bridge_struct.h"
#include "soc/interrupts.h"
#include "esp_intr_alloc.h"
#include <string.h>

#ifdef DSI_BRIDGE_STATUS_TEST
static volatile uint32_t s_brg_isr_count = 0;
#endif

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

// ── Display MCU (I2C GPIO expander/regulator) ─────────────────────────────
// Discovered 2026-07-11 from the Raspberry Pi kernel driver source
// (drivers/regulator/waveshare-panel-regulator.c, compatible string
// "waveshare,touchscreen-panel-regulator", driver name "waveshare_touchscreen"
// — matches the panel's own dmesg output exactly) plus the panel's DSI
// driver (drivers/gpu/drm/panel/panel-waveshare-dsi-v2.c). This chip sits
// at I2C address 0x45 and is a 16-bit I2C GPIO expander that gates AVDD,
// IOVCC, LCD reset, backlight enable, and the GT911 touch reset line — none
// of which are native SoC GPIOs. Our firmware never talked to this chip
// before; the DSI panel and GT911 touch chip were both sitting in reset
// indefinitely, which fully explains both the DSI SWRESET hang and the
// I2C-never-ACKs finding from the same session (see CONTEXT.md).
#define MCU_I2C_ADDR          0x45
#define MCU_REG_TP            0x94   // GPIO output state, bits 8-15 (high byte)
#define MCU_REG_LCD           0x95   // GPIO output state, bits 0-7 (low byte)
#define MCU_REG_PWM           0x96   // backlight brightness 0-255
#define MCU_REG_SIZE          0x97   // read-only: panel size (12.3" panel reads 123)
#define MCU_REG_ID            0x98   // read-only: hw id
#define MCU_REG_VERSION       0x99   // read-only: mcu firmware version

#define MCU_GPIO_AVDD          0
#define MCU_GPIO_LCD_RESET     1
#define MCU_GPIO_BL_ENABLE     2
#define MCU_GPIO_IOVCC         4
#define MCU_GPIO_TOUCH_RESET   9

// ── DSI / DPI timing (portrait 720×1920) ─────────────────────────────────
// Source: ws_panel_12_3_a_4lane_mode in Waveshare kernel driver.
// htotal = 720 + HFP(10) + HSync(10) + HBP(12) = 752
// vtotal = 1920 + VFP(64) + VSync(18) + VBP(4) = 2006
// P4-Nano has 2 DSI lanes only (confirmed from schematic).
// Lane rate from HX8399_PANEL_BUS_DSI_2CH_CONFIG() in esp_lcd_hx8399.h.
#define WS_DSI_LANE_NUM       2
// 2026-07-12 BREAKTHROUGH: was 950 (esp_lcd_hx8399 component's own 2-lane
// default) — permanently stuck DSI PHY (MIPI_DSI_HOST.phy_status showed
// clock+data lanes parked in LP stop-state forever, confirmed via direct
// host register reads, independent of DPI clock/porches/panel behavior).
// Found via Waveshare's own ESP32-P4-Nano-Kit-D reference BSP
// (waveshareteam/Waveshare-ESP32-components,
// bsp/esp32_p4_platform/display/lcd/esp_lcd_jd9365_10_1,
// JD9365_PANEL_BUS_DSI_2CH_CONFIG()) — 1500 Mbps, combined with
// WS_DPI_CLK_MHZ=80 below, is the first combination all session where
// MIPI_DSI_HOST.phy_status actually shows the PHY leaving stop-state and
// bursting (confirmed with OUR real 720x1920 resolution/porches
// unchanged — see CONTEXT.md "2026-07-12" section for the full
// isolation-test log). Neither value alone (tested independently)
// produced any change — it's the dpi_clock/lane_bit_rate RATIO that
// matters, not either value in isolation.
#define WS_DSI_LANE_MBPS      1500
// 2026-07-12 BREAKTHROUGH: was 75 (see below for the full prior history)
// — paired with WS_DSI_LANE_MBPS=1500 above, this is the first DPI
// clock/lane-rate combination all session that produces real HS bursts
// (MIPI_DSI_HOST.phy_status leaving permanent stop-state). See
// CONTEXT.md "2026-07-12" section.
//
// Prior history: REVERTED 2026-07-11 back to 75 (was dropped to 20 to
// fix DMA underrun, then reverted again). HX8399-C datasheet (Table
// 8.14) specifies Vertical Refresh rate = 60Hz. 75MHz gave ~49.7Hz —
// 20MHz gave only ~13.25Hz, likely outside the panel's valid GIP timing
// range. Pi5 cross-check (confirmed working) ran ~95MHz DPI clock ->
// ~63Hz, consistent with the 60Hz datasheet spec. 80MHz here (with the
// new 1500Mbps lane rate) gives ~53Hz — within the same ballpark as
// both the datasheet spec and the Pi5 reference.
#define WS_DPI_CLK_MHZ        75   // TEMPORARY: re-check pattern-gen (GDMA-independent) bursting at 75 vs 80, with lane rate held at 1500
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
static i2c_master_dev_handle_t  s_mcu_dev = NULL;
static uint16_t                 s_mcu_gpio_state = 0;

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

    esp_err_t draw_ret = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, WS_PANEL_H, WS_PANEL_V, s_rot_buf);
    if (draw_ret != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap FAILED: %s", esp_err_to_name(draw_ret));
    }

#ifdef DSI_HOST_STATUS_TEST
    // Same host-side DSI status read as prv_panel_init(), but sampled DURING
    // real, ongoing frame draws (the earlier pre-first-frame sample showed
    // lanes parked in stop-state / FIFOs empty, which could just mean
    // "hasn't started yet" — this fires on the 5th real flush, well inside
    // the period where "frame N" + underrun spam is already happening, for
    // a fair apples-to-apples read).
    {
        static int flush_count = 0;
        flush_count++;
        ESP_LOGD(TAG, "flush_cb call #%d", flush_count);
#ifdef DSI_TIGHT_POLL_TEST
        // TEMPORARY 2026-07-13: ESP32-P4 TRM 42.4.3.1.4 confirms LP-11/
        // stopstate IS the normal resting state between transactions —
        // "any request must start from and end in this state." Our
        // register polling all session has been every 10-80ms; if real
        // HS bursts genuinely happen but are brief relative to that
        // interval, we'd almost always sample back at idle and wrongly
        // conclude "never bursts". Testing with a tight, no-delay loop —
        // thousands of back-to-back reads, no vTaskDelay at all — right
        // after a real draw_bitmap() call, to see if we ever catch even
        // one non-stopstate/non-empty sample that slower polling missed.
        if (flush_count == 5) {
            uint32_t burst_seen = 0, full_seen = 0;
            uint32_t sample_count = 100000;
            for (uint32_t i = 0; i < sample_count; i++) {
                uint32_t phy = MIPI_DSI_HOST.phy_status.val;
                uint32_t vid = MIPI_DSI_HOST.vid_pkt_status.val;
                if (((phy >> 2) & 1) == 0 || ((phy >> 4) & 1) == 0 || ((phy >> 7) & 1) == 0) {
                    burst_seen++;
                }
                if (((vid >> 17) & 1) == 1) {  // dpi_buff_pld_full
                    full_seen++;
                }
            }
            ESP_LOGW(TAG, "DSI_TIGHT_POLL: %lu samples, burst_seen=%lu times, buffer_full_seen=%lu times",
                     (unsigned long)sample_count, (unsigned long)burst_seen, (unsigned long)full_seen);
        }
#endif
        if (flush_count <= 30) {
            uint32_t phy = MIPI_DSI_HOST.phy_status.val;
            uint32_t vid = MIPI_DSI_HOST.vid_pkt_status.val;
            ESP_LOGW(TAG, "DSI_HOST(flush#%d): phy=0x%08lX(clkstop=%lu d0stop=%lu d1stop=%lu) vid_pkt=0x%08lX",
                     flush_count, (unsigned long)phy, (unsigned long)((phy >> 2) & 1),
                     (unsigned long)((phy >> 4) & 1), (unsigned long)((phy >> 7) & 1),
                     (unsigned long)vid);
#ifdef DSI_BRIDGE_STATUS_TEST
            uint32_t pixel_type = MIPI_DSI_BRIDGE.pixel_type.val;
            uint32_t fifo = MIPI_DSI_BRIDGE.fifo_flow_status.val;
            uint32_t dpi_misc = MIPI_DSI_BRIDGE.dpi_misc_config.val;
            ESP_LOGW(TAG, "DSI_BRIDGE(flush#%d): en=%lu pixel_type=0x%lX raw_buf_depth=%lu dpi_en=%lu isr_count=%lu",
                     flush_count, (unsigned long)MIPI_DSI_BRIDGE.en.val, (unsigned long)pixel_type,
                     (unsigned long)(fifo & 0x3FFF), (unsigned long)(dpi_misc & 1),
                     (unsigned long)s_brg_isr_count);
#endif
        }
        if (flush_count == 1) {
            for (int i = 0; i < 8; i++) {
                uint32_t phy = MIPI_DSI_HOST.phy_status.val;
                uint32_t st0 = MIPI_DSI_HOST.int_st0.val;
                uint32_t st1 = MIPI_DSI_HOST.int_st1.val;
                uint32_t vid = MIPI_DSI_HOST.vid_pkt_status.val;
                ESP_LOGW(TAG, "DSI_HOST(mid-stream)[%d]: phy=0x%08lX(lock=%lu clkstop=%lu d0stop=%lu d1stop=%lu) "
                         "int_st0=0x%08lX int_st1=0x%08lX(hs_tx_to=%lu dpi_under=%lu) vid_pkt=0x%08lX",
                         i, (unsigned long)phy,
                         (unsigned long)(phy & 1), (unsigned long)((phy >> 2) & 1),
                         (unsigned long)((phy >> 4) & 1), (unsigned long)((phy >> 7) & 1),
                         (unsigned long)st0, (unsigned long)st1,
                         (unsigned long)(st1 & 1), (unsigned long)((st1 >> 19) & 1),
                         (unsigned long)vid);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
#endif
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

// ── Display MCU (GPIO expander/regulator, addr 0x45) ─────────────────────
// Protocol reverse-engineered from drivers/regulator/waveshare-panel-regulator.c
// (raspberrypi/linux). Write-only 16-bit GPIO state, split across two
// 8-bit registers — every GPIO change requires rewriting BOTH registers,
// there's no per-bit write. We keep our own shadow (s_mcu_gpio_state)
// since we don't have the kernel's regmap cache.
static esp_err_t prv_mcu_write_state(void)
{
    uint8_t tp_cmd[2]  = { MCU_REG_TP,  (uint8_t)(s_mcu_gpio_state >> 8) };
    uint8_t lcd_cmd[2] = { MCU_REG_LCD, (uint8_t)(s_mcu_gpio_state & 0xFF) };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_mcu_dev, tp_cmd, sizeof(tp_cmd), 1000),
                        TAG, "MCU write REG_TP failed");
    return i2c_master_transmit(s_mcu_dev, lcd_cmd, sizeof(lcd_cmd), 1000);
}

static esp_err_t prv_mcu_set_gpio(int bit, bool level)
{
    if (level) s_mcu_gpio_state |= (uint16_t)(1U << bit);
    else       s_mcu_gpio_state &= (uint16_t)~(1U << bit);
    return prv_mcu_write_state();
}

// Mirrors waveshare_panel_i2c_read() exactly: two FULLY SEPARATE
// transactions (write-only, then read-only), not a single combined
// write+repeated-start+read. Upstream inserts a real 5-10ms gap between
// them (usleep_range(5000, 10000)) — presumably the MCU firmware needs
// that time after seeing the register address before the requested byte
// is actually ready to shift out. A tight repeated-start read (what this
// used before) could race that on some register/timing combination even
// though it hasn't shown a problem so far (ID/SIZE/VERSION reads have
// been byte-exact against the Pi5 reference every time). Matching
// upstream's actual protocol removes that as a variable.
static esp_err_t prv_mcu_read_reg(uint8_t reg, uint8_t *out)
{
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_mcu_dev, &reg, 1, 1000),
                        TAG, "MCU read: register-address write failed");
    vTaskDelay(pdMS_TO_TICKS(8));  // upstream: usleep_range(5000, 10000)
    return i2c_master_receive(s_mcu_dev, out, 1, 1000);
}

// Phase 1: find the MCU and release the GT911 touch reset line. Must run
// before prv_touch_init() — the touch chip is held in reset until this runs.
static esp_err_t prv_mcu_init(void)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MCU_I2C_ADDR,
        .scl_speed_hz    = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev_cfg, &s_mcu_dev),
                        TAG, "MCU i2c_master_bus_add_device failed");

    uint8_t id = 0, size = 0, ver = 0;
    if (prv_mcu_read_reg(MCU_REG_ID, &id) == ESP_OK)
        ESP_LOGI(TAG, "display MCU hw id = 0x%02x", id);
    if (prv_mcu_read_reg(MCU_REG_SIZE, &size) == ESP_OK)
        ESP_LOGI(TAG, "display MCU panel size = %d", size);
    if (prv_mcu_read_reg(MCU_REG_VERSION, &ver) == ESP_OK)
        ESP_LOGI(TAG, "display MCU fw version = 0x%02x", ver);

    // Mirrors waveshare_panel_i2c_probe(): unconditionally sets bits 8+9
    // ("Enable VCC" per upstream's own comment). Bit 9 is the GT911 touch
    // reset line — this is the only place upstream ever touches it, no
    // pulse, just released high once and left alone.
    s_mcu_gpio_state = (uint16_t)((1U << MCU_GPIO_TOUCH_RESET) | (1U << 8));
    ESP_RETURN_ON_ERROR(prv_mcu_write_state(), TAG, "MCU VCC/touch-reset enable failed");
    // Upstream's own msleep(20) here is misleadingly short to copy verbatim:
    // on real Linux, the Goodix driver only actually probes once the kernel's
    // deferred-probe retry mechanism notices this MCU registered as a GPIO
    // provider — that's typically hundreds of ms to seconds of real elapsed
    // time, not 20ms. Our sequential init gets there almost instantly by
    // comparison. Trying a much longer settle time (2026-07-11 experiment,
    // touch was still all-zero at 20ms) before GT911's first register read.
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_LOGI(TAG, "display MCU init complete (touch reset released)");
    return ESP_OK;
}

// Phase 2: power-sequence the LCD panel itself — IOVCC, AVDD, then a reset
// pulse. Mirrors ws_panel_prepare() in panel-waveshare-dsi-v2.c exactly
// (same order, same delays), confirmed working on real hardware (Pi5,
// 2026-07-11). Call this immediately before esp_lcd_panel_reset()/init().
static esp_err_t prv_mcu_panel_power_on(void)
{
    ESP_RETURN_ON_ERROR(prv_mcu_set_gpio(MCU_GPIO_IOVCC, true), TAG, "MCU iovcc failed");
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_RETURN_ON_ERROR(prv_mcu_set_gpio(MCU_GPIO_AVDD, true), TAG, "MCU avdd failed");
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_RETURN_ON_ERROR(prv_mcu_set_gpio(MCU_GPIO_LCD_RESET, false), TAG, "MCU reset assert failed");
    vTaskDelay(pdMS_TO_TICKS(60));
    ESP_RETURN_ON_ERROR(prv_mcu_set_gpio(MCU_GPIO_LCD_RESET, true), TAG, "MCU reset release failed");
    vTaskDelay(pdMS_TO_TICKS(60));

    // Backlight enable line also lives on the MCU (BL_EN, bit 2) — separate
    // from our own GPIO22 LEDC PWM signal (prv_backlight_on()). Both are
    // wired on this panel; enable this one too in case the panel's
    // backlight driver actually gates on it rather than just the PWM duty.
    ESP_RETURN_ON_ERROR(prv_mcu_set_gpio(MCU_GPIO_BL_ENABLE, true), TAG, "MCU backlight enable failed");

    ESP_LOGI(TAG, "display MCU panel power sequence complete");
    return ESP_OK;
}

#ifdef DSI_BRIDGE_STATUS_TEST
// TEMPORARY 2026-07-13: real, dedicated CPU-level interrupt handler for the
// DSI Bridge's own IRQ line (ETS_DSI_BRIDGE_INTR_SOURCE) — mirrors what
// ESP-IDF 5.5.3 added (esp_lcd_panel_dpi.c's mipi_dsi_bridge_isr_handler,
// confirmed absent from our 5.4.2) but installed directly from app code,
// no framework patch needed, since the interrupt source constant is a
// real hardware/interrupt-matrix definition present in 5.4.2 too. Testing
// whether having a genuine, dedicated ISR connected to the bridge's own
// IRQ line (vs. 5.4.2's approach of only reading/clearing its status
// register as a side effect of the unrelated DMA-completion callback)
// changes anything — some peripherals use the interrupt controller
// actually servicing their specific IRQ source as part of an internal
// handshake, not just having status bits cleared from any context.
static void IRAM_ATTR prv_dsi_bridge_isr(void *arg)
{
    (void)arg;
    uint32_t raw = MIPI_DSI_BRIDGE.int_raw.val;
    MIPI_DSI_BRIDGE.int_clr.val = raw;
    s_brg_isr_count++;
}
#endif

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
#ifdef DSI_JD9365_COMBINED_TEST
    // TEMPORARY 2026-07-12 diagnostic: literal, exact copy of
    // JD9365_800_1280_PANEL_60HZ_DPI_CONFIG() from Waveshare's own proven
    // ESP32-P4-Nano-Kit-D BSP (Waveshare-ESP32-components,
    // bsp/esp32_p4_platform/display/lcd/esp_lcd_jd9365_10_1) — testing the
    // FULL combination at once (resolution/DPI-clock/all porches) rather
    // than one variable at a time, in case no single value matters in
    // isolation but the combination does. Deliberately mismatched vs our
    // real 720x1920 HX8399-C panel/init commands — irrelevant here since
    // this only feeds DSI_PATTERN_GEN_TEST (host-generated, panel-
    // independent). Do not expect a real image; only phy_status matters.
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 80,
        .virtual_channel    = 0,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs            = 2,
        .video_timing = {
            // Isolation test: OUR real 720x1920 res/porches, but with the
            // JD9365 reference's 80MHz DPI clock (see dpi_clock_freq_mhz
            // above) + 1500Mbps lane rate (WS_DSI_LANE_MBPS). First combined
            // test (800x1280 res + these values) showed the clock lane
            // briefly leave stop-state (clkstop=0) for the first time all
            // session -- isolating whether that needs the foreign
            // resolution too, or just clock/lane-rate.
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
#else
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
#endif

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

    // Power-sequence AVDD/IOVCC/reset via the display MCU before touching
    // the panel at all — see prv_mcu_panel_power_on(). Without this the
    // panel is left in reset indefinitely and every DSI command hangs.
    ESP_RETURN_ON_ERROR(prv_mcu_panel_power_on(), TAG, "MCU panel power sequence failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Panel reset failed");
    // Display ON is sent from inside s_hx8399_init_cmds (before video streaming
    // starts) — see comment there. Do NOT call esp_lcd_panel_disp_on_off() here.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG, "Panel init failed");

#ifdef DSI_PATTERN_GEN_TEST
    // TEMPORARY 2026-07-12 diagnostic: enable the DSI HOST CONTROLLER's own
    // built-in hardware test-pattern generator (esp_lcd_dpi_panel_set_pattern,
    // esp_lcd_panel_dpi.c:579 -> mipi_dsi_host_ll_dpi_set_pattern_type()).
    // This generates pixels entirely INSIDE the DesignWare DSI Host IP —
    // no GDMA, no PSRAM fetch, no framebuffer at all. Found via Waveshare's
    // own ESP32-P4-NANO-KIT-D reference example ("13_Displaycolorbar",
    // waveshareteam/ESP32-P4-Platform on GitHub — same board family,
    // different panel/JD9365 + GT9271 touch, 2-lane DSI). If HS bursts
    // still never happen with this enabled (check via the
    // DSI_HOST_STATUS_TEST block below/in flush_cb), that rules out our
    // entire pixel pipeline (GDMA/PSRAM/PPA/framebuffer) as the cause —
    // the bug would have to be in DSI host/bridge enable sequencing or
    // video_timing config itself. If it DOES burst, our pixel-feed path
    // is implicated instead.
    {
        esp_err_t pat_ret = esp_lcd_dpi_panel_set_pattern(s_panel, MIPI_DSI_PATTERN_BAR_VERTICAL);
        ESP_LOGW(TAG, "DSI_PATTERN_GEN_TEST: set_pattern(BAR_VERTICAL) -> %s", esp_err_to_name(pat_ret));
        for (int i = 0; i < 15; i++) {
            uint32_t phy = MIPI_DSI_HOST.phy_status.val;
            uint32_t vid = MIPI_DSI_HOST.vid_pkt_status.val;
            ESP_LOGW(TAG, "DSI_PATTERN_GEN_TEST[%d]: phy=0x%08lX(clkstop=%lu d0stop=%lu d1stop=%lu) vid_pkt=0x%08lX",
                     i, (unsigned long)phy, (unsigned long)((phy >> 2) & 1),
                     (unsigned long)((phy >> 4) & 1), (unsigned long)((phy >> 7) & 1),
                     (unsigned long)vid);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        // 2026-07-12: real HS bursts confirmed above (repeated all-3-lane
        // activity), yet nothing visible on the physical screen. Checking
        // whether the PANEL's own internal state shows any sign of having
        // actually received/captured that video data — distinguishes
        // "panel receives it but shows nothing" (implicates color/format)
        // from "panel's HS receiver never captures anything real"
        // (implicates signal integrity — points hard at the scope).
        // GETSCAN (0x45) is the panel's own internal scanline counter —
        // if it's incrementing, the panel's video-timing engine is
        // actively tracking incoming HS data.
        vTaskDelay(pdMS_TO_TICKS(500));
        {
            uint8_t rddpm = 0xAA, scan1[2] = {0xAA, 0xAA}, scan2[2] = {0xAA, 0xAA}, scan3[2] = {0xAA, 0xAA};
            esp_lcd_panel_io_rx_param(io, 0x0A, &rddpm, 1);
            esp_lcd_panel_io_rx_param(io, 0x45, scan1, 2);
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_lcd_panel_io_rx_param(io, 0x45, scan2, 2);
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_lcd_panel_io_rx_param(io, 0x45, scan3, 2);
            ESP_LOGW(TAG, "DSI_PATTERN_GEN_TEST post-burst: RDDPM=0x%02X GETSCAN=%u,%u,%u (50ms apart)",
                     rddpm, (unsigned)((scan1[0] << 8) | scan1[1]),
                     (unsigned)((scan2[0] << 8) | scan2[1]), (unsigned)((scan3[0] << 8) | scan3[1]));
        }
    }
#endif

#ifdef PANEL_DIAG_READ_TEST
    // TEMPORARY 2026-07-12 diagnostic: read back several standard MIPI DCS
    // status registers over the DSI command channel. Unlike every write
    // we've sent so far (which only tells us the ESP32 host-side HAL call
    // returned ESP_OK — NOT that the byte was ever really latched by the
    // panel; see the mipi_dsi_hal patch episode in CONTEXT.md), a DCS
    // *read* requires genuine two-way electrical turnaround (BTA) on the
    // data lane. RDNUMPE (0x05) is the most targeted one here: it reports
    // the panel's own count of DSI parity/ECC errors, which can tell us
    // whether the LP command channel is clean while the HS video lane is
    // actually garbled (the two use different electrical modes on the same
    // physical pins). GETSCAN (0x45) is read twice ~20ms apart: if the
    // internal scanline counter is incrementing, the panel's own video
    // timing engine is actively running.
    {
        uint8_t rddpm = 0xAA, rddsdr = 0xAA, numpe = 0xAA;
        uint8_t rddid[3] = {0xAA, 0xAA, 0xAA};
        uint8_t scan1[2] = {0xAA, 0xAA}, scan2[2] = {0xAA, 0xAA};
        esp_lcd_panel_io_rx_param(io, 0x0A, &rddpm, 1);
        esp_lcd_panel_io_rx_param(io, 0x0F, &rddsdr, 1);
        esp_lcd_panel_io_rx_param(io, 0x05, &numpe, 1);
        esp_lcd_panel_io_rx_param(io, 0x04, rddid, 3);
        esp_lcd_panel_io_rx_param(io, 0x45, scan1, 2);
        vTaskDelay(pdMS_TO_TICKS(20));
        esp_lcd_panel_io_rx_param(io, 0x45, scan2, 2);
        ESP_LOGW(TAG, "DIAG: RDDPM=0x%02X RDDSDR=0x%02X RDNUMPE=0x%02X RDDID=%02X:%02X:%02X "
                 "GETSCAN=%u then %u (+20ms)",
                 rddpm, rddsdr, numpe, rddid[0], rddid[1], rddid[2],
                 (unsigned)((scan1[0] << 8) | scan1[1]), (unsigned)((scan2[0] << 8) | scan2[1]));
    }
#endif

#ifdef DSI_HOST_STATUS_TEST
    // TEMPORARY 2026-07-12 diagnostic: read the ESP32-P4's OWN DSI host
    // controller status registers (chip side, NOT the panel) — these are
    // a completely different hardware path from the LP-mode "Gen" command
    // generator that RDDPM/RDNUMPE/etc. exercised. Video mode is already
    // enabled by this point (hx8399->init() turns it on at the end of
    // esp_lcd_panel_init()), so if HS video is actually leaving the chip,
    // these registers should show it independent of anything the panel
    // self-reports. phy_status bit0=phy_lock, bit2=clk lane stopstate,
    // bit4=D0 stopstate, bit7=D1 stopstate (stopstate=1 means the lane is
    // parked in LP-11 / NOT bursting HS at that instant — expected to
    // toggle low periodically during real video transmission). int_st1
    // bit0=to_hs_tx (HS TX timeout), bit19=dpi_buff_pld_under (HOST-side
    // DPI FIFO underrun, distinct from the LCD-peripheral-side "can't
    // fetch data" error we already see). int_st0/int_st1 are clear-on-read
    // per DesignWare IP convention, so sample repeatedly to catch anything
    // accumulating.
    {
        for (int i = 0; i < 6; i++) {
            uint32_t phy  = MIPI_DSI_HOST.phy_status.val;
            uint32_t st0  = MIPI_DSI_HOST.int_st0.val;
            uint32_t st1  = MIPI_DSI_HOST.int_st1.val;
            uint32_t vid  = MIPI_DSI_HOST.vid_pkt_status.val;
            ESP_LOGW(TAG, "DSI_HOST[%d]: phy=0x%08lX(lock=%lu clkstop=%lu d0stop=%lu d1stop=%lu) "
                     "int_st0=0x%08lX int_st1=0x%08lX(hs_tx_to=%lu dpi_under=%lu) vid_pkt=0x%08lX",
                     i, (unsigned long)phy,
                     (unsigned long)(phy & 1), (unsigned long)((phy >> 2) & 1),
                     (unsigned long)((phy >> 4) & 1), (unsigned long)((phy >> 7) & 1),
                     (unsigned long)st0, (unsigned long)st1,
                     (unsigned long)(st1 & 1), (unsigned long)((st1 >> 19) & 1),
                     (unsigned long)vid);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }
#endif

#ifdef DSI_BRIDGE_STATUS_TEST
    // TEMPORARY 2026-07-13 diagnostic: read the DSI BRIDGE's own registers
    // directly (MIPI_DSI_BRIDGE, soc/mipi_dsi_bridge_struct.h) — a
    // different hardware block from MIPI_DSI_HOST above. Diffing our
    // esp_lcd_panel_dpi.c (ESP-IDF 5.4.2) against a locally-cached 5.5.3
    // copy found ESP-IDF 5.4.2 never calls a "set output color format"
    // step for the bridge — only mipi_dsi_brg_ll_set_input_color_space()
    // is called (esp_lcd_panel_dpi.c ~line 322), no output-side
    // equivalent exists in 5.4.2's LL header at all. Checking pixel_type
    // (input/output format config, default 0=RGB888 per the raw_type
    // field's reset value) and fifo_flow_status.raw_buf_depth (the
    // bridge's OWN internal buffer fill level, independent of
    // MIPI_DSI_HOST's vid_pkt_status) to see whether data ever reaches
    // the bridge's raw buffer at all, or arrives but never drains.
    {
        uint32_t pixel_type = MIPI_DSI_BRIDGE.pixel_type.val;
        uint32_t en = MIPI_DSI_BRIDGE.en.val;
        uint32_t dpi_misc = MIPI_DSI_BRIDGE.dpi_misc_config.val;
        uint32_t fifo = MIPI_DSI_BRIDGE.fifo_flow_status.val;
        ESP_LOGW(TAG, "DSI_BRIDGE: en=0x%lX pixel_type=0x%lX(raw_type=%lu dpi_config=%lu data_in_type=%lu) "
                 "dpi_misc=0x%lX(dpi_en=%lu) raw_buf_depth=%lu",
                 (unsigned long)en, (unsigned long)pixel_type,
                 (unsigned long)(pixel_type & 0xF), (unsigned long)((pixel_type >> 4) & 0x3),
                 (unsigned long)((pixel_type >> 6) & 0x1),
                 (unsigned long)dpi_misc, (unsigned long)(dpi_misc & 1),
                 (unsigned long)(fifo & 0x3FFF));

        // TEST 2026-07-13: mem_clk_ctrl has two "force clock on" bits for
        // the bridge's own FIFO memory (dsi_bridge_mem_clk_force_on bit0,
        // dsi_mem_clk_force_on bit1), both default 0 — meaning the FIFO
        // memory clock relies on automatic/dynamic gating that may not
        // correctly detect activity in our usage pattern (early ECO2
        // silicon; "force on" workaround bits are a common pattern for
        // exactly this class of issue). raw_buf_depth sitting permanently
        // near-full (data arriving, never draining) is consistent with
        // the output-side FIFO memory not actually being clocked. Forcing
        // both bits on directly to test.
        uint32_t mem_clk_before = MIPI_DSI_BRIDGE.mem_clk_ctrl.val;
        MIPI_DSI_BRIDGE.mem_clk_ctrl.val = 0x3;
        ESP_LOGW(TAG, "DSI_BRIDGE: mem_clk_ctrl before=0x%lX after=0x%lX (forced both FIFO clocks on)",
                 (unsigned long)mem_clk_before, (unsigned long)MIPI_DSI_BRIDGE.mem_clk_ctrl.val);

        // Comprehensive dump of remaining unchecked bridge registers:
        // dpi_lcd_ctl (dpishutdn/dpicolorm/dpiupdatecfg — real standard DPI
        // protocol control signals; dpishutdn=1 would tell the display to
        // stop processing, exactly matching our symptom if unexpectedly
        // set), the bridge's own timing registers (should show our real
        // 720x1920 config, not the 800x480-ish hw defaults, if
        // mipi_dsi_brg_ll_set_horizontal/vertical_timing() really landed),
        // credit control (probably irrelevant — comment says "valid only
        // when dsi_bridge as flow controller", and we use DMA as flow
        // controller — but confirming), and interrupt raw/status (only has
        // an underrun bit, checking anyway).
        ESP_LOGW(TAG, "DSI_BRIDGE dpi_lcd_ctl=0x%lX(shutdn=%lu colorm=%lu updatecfg=%lu) host_ctrl=0x%lX",
                 (unsigned long)MIPI_DSI_BRIDGE.dpi_lcd_ctl.val,
                 (unsigned long)(MIPI_DSI_BRIDGE.dpi_lcd_ctl.val & 1),
                 (unsigned long)((MIPI_DSI_BRIDGE.dpi_lcd_ctl.val >> 1) & 1),
                 (unsigned long)((MIPI_DSI_BRIDGE.dpi_lcd_ctl.val >> 2) & 1),
                 (unsigned long)MIPI_DSI_BRIDGE.host_ctrl.val);
        ESP_LOGW(TAG, "DSI_BRIDGE timing: v_cfg0=0x%lX(vdisp=%lu vtotal=%lu) v_cfg1=0x%lX(vsync=%lu vbank=%lu)",
                 (unsigned long)MIPI_DSI_BRIDGE.dpi_v_cfg0.val,
                 (unsigned long)(MIPI_DSI_BRIDGE.dpi_v_cfg0.val & 0xFFF),
                 (unsigned long)((MIPI_DSI_BRIDGE.dpi_v_cfg0.val >> 16) & 0xFFF),
                 (unsigned long)MIPI_DSI_BRIDGE.dpi_v_cfg1.val,
                 (unsigned long)(MIPI_DSI_BRIDGE.dpi_v_cfg1.val & 0xFFF),
                 (unsigned long)((MIPI_DSI_BRIDGE.dpi_v_cfg1.val >> 16) & 0xFFF));
        ESP_LOGW(TAG, "DSI_BRIDGE timing: h_cfg0=0x%lX(hdisp=%lu htotal=%lu) h_cfg1=0x%lX(hsync=%lu hbank=%lu)",
                 (unsigned long)MIPI_DSI_BRIDGE.dpi_h_cfg0.val,
                 (unsigned long)(MIPI_DSI_BRIDGE.dpi_h_cfg0.val & 0xFFF),
                 (unsigned long)((MIPI_DSI_BRIDGE.dpi_h_cfg0.val >> 16) & 0xFFF),
                 (unsigned long)MIPI_DSI_BRIDGE.dpi_h_cfg1.val,
                 (unsigned long)(MIPI_DSI_BRIDGE.dpi_h_cfg1.val & 0xFFF),
                 (unsigned long)((MIPI_DSI_BRIDGE.dpi_h_cfg1.val >> 16) & 0xFFF));
        ESP_LOGW(TAG, "DSI_BRIDGE raw_num_cfg=0x%lX credit_ctl=0x%lX int_raw=0x%lX int_st=0x%lX int_ena=0x%lX dma_req_cfg=0x%lX",
                 (unsigned long)MIPI_DSI_BRIDGE.raw_num_cfg.val,
                 (unsigned long)MIPI_DSI_BRIDGE.raw_buf_credit_ctl.val,
                 (unsigned long)MIPI_DSI_BRIDGE.int_raw.val,
                 (unsigned long)MIPI_DSI_BRIDGE.int_st.val,
                 (unsigned long)MIPI_DSI_BRIDGE.int_ena.val,
                 (unsigned long)MIPI_DSI_BRIDGE.dma_req_cfg.val);

        // Install a real, dedicated CPU-level interrupt handler for the
        // bridge's own IRQ line — see prv_dsi_bridge_isr() comment above.
        intr_handle_t brg_intr = NULL;
        esp_err_t intr_ret = esp_intr_alloc(ETS_DSI_BRIDGE_INTR_SOURCE,
                ESP_INTR_FLAG_LOWMED, prv_dsi_bridge_isr, NULL, &brg_intr);
        ESP_LOGW(TAG, "DSI_BRIDGE: esp_intr_alloc(ETS_DSI_BRIDGE_INTR_SOURCE) -> %s",
                 esp_err_to_name(intr_ret));
    }
#endif

#ifdef PANEL_BIST_TEST
    // TEMPORARY 2026-07-12 diagnostic, RETRY 2: enable the HX8399-C's own
    // internal BIST free-running pattern generator (datasheet SETDISP/B2h,
    // Bank1, DISP_BIST_EN bit). First attempt used the WRONG SETEXTC
    // unlock key ({0xFF,0x83,0x99}, copied from the generic Espressif
    // reference driver) — this panel's real unlock key, confirmed working
    // in s_hx8399_init_cmds above, is {0x83,0x10,0x2E}. Also dropping the
    // unverified BDh "bank select" (never used anywhere in this panel's
    // real init sequence — it was only ever seen paired with an unrelated
    // command, D8h, in the generic reference table) in favor of a single
    // continuous B2h write covering both Bank0 (10 bytes, the SAME known-
    // good calibration values already in s_hx8399_init_cmds) and Bank1 (9
    // bytes, with DISP_BIST_EN set) in one packet — matching how Himax
    // manufacturer commands document "Bank0"/"Bank1" as one continuous
    // parameter stream, not a separately page-selected register space.
    {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xB9,
                (uint8_t[]){0x83, 0x10, 0x2E}, 3), TAG, "BIST: SETEXTC failed");
        esp_err_t bist_ret = esp_lcd_panel_io_tx_param(io, 0xB2, (uint8_t[]){
                // Bank0 (10 bytes) — identical to s_hx8399_init_cmds's known-good B2h
                0x00, 0x02, 0x00, 0x90, 0x14, 0x0C, 0x02, 0x0C, 0x02, 0x37,
                // Bank1 (9 bytes) — byte1: FRM_PATTERN_CYCLE=0000 | DISP_BIST_EN=1 |
                // FRM_SCAN_CYCLE=000 = 0x08. byte2 low nibble: PTN_1ST_NUM=0000=White.
                0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            }, 19);
        ESP_LOGW(TAG, "PANEL_BIST_TEST retry2: B2h 19-byte (Bank0+Bank1) DISP_BIST_EN=1 (white) -> %s",
                 esp_err_to_name(bist_ret));
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xB9,
                (uint8_t[]){0x00}, 1), TAG, "BIST: page close failed");
    }
#endif

#ifdef SOLID_FILL_TEST
    // TEMPORARY 2026-07-12 diagnostic: bypass LVGL/PPA rotation entirely and
    // push one solid white frame straight into the panel via a single raw
    // esp_lcd_panel_draw_bitmap() call, full native panel bounds, no
    // coordinate transform of any kind. Tests whether the DSI/panel/MCU/
    // cable chain can display ANY real pixel data at all, independent of
    // our own LVGL/PPA rotation pipeline (which has been suspected of a
    // possible coordinate/off-screen bug — see CONTEXT.md). If this shows
    // up, the chain works and the bug is in our rotation code. If not, the
    // problem is upstream of our own pixel formatting entirely.
    {
        size_t fill_bytes = (size_t)WS_PANEL_H * WS_PANEL_V * sizeof(uint16_t);
        uint16_t *fill_buf = heap_caps_aligned_alloc(128, fill_bytes,
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (fill_buf) {
            for (size_t i = 0; i < fill_bytes / sizeof(uint16_t); i++) {
                fill_buf[i] = 0xFFFF;  // solid white, RGB565
            }
            esp_cache_msync(fill_buf, fill_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            esp_err_t fill_ret = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, WS_PANEL_H, WS_PANEL_V, fill_buf);
            ESP_LOGW(TAG, "SOLID_FILL_TEST: draw_bitmap(0,0,%d,%d) solid white -> %s",
                     WS_PANEL_H, WS_PANEL_V, esp_err_to_name(fill_ret));
        } else {
            ESP_LOGE(TAG, "SOLID_FILL_TEST: fill_buf alloc failed");
        }
    }
#endif

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

// ── Touch-only diagnostic ─────────────────────────────────────────────────
// Bypasses the DSI panel entirely (prv_panel_init() hangs forever on the
// current hardware) so touch can be tested independent of the display.
// Never returns — call instead of, not before, the normal init/app_main path.
void ws_touch_diag_run(void)
{
    ESP_LOGI(TAG, "touch-diag: init: display 5V rail enable GPIO%d", HAT_DISP_PWR_EN);
    prv_display_power_enable();

    ESP_LOGI(TAG, "touch-diag: init: I2C SDA=GPIO%d SCL=GPIO%d", WS_I2C_SDA, WS_I2C_SCL);
    ESP_ERROR_CHECK(prv_i2c_init());

    ESP_LOGI(TAG, "touch-diag: init: display MCU (addr 0x%02X)", MCU_I2C_ADDR);
    ESP_ERROR_CHECK(prv_mcu_init());

    ESP_LOGI(TAG, "touch-diag: init: GT911 touch");
    esp_err_t ret = prv_touch_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "touch-diag: GT911 init failed (0x%x) — nothing to poll", ret);
        return;
    }

    ESP_LOGI(TAG, "touch-diag: touch the panel now — polling every 100ms");
    bool was_pressed = false;
    while (1) {
        esp_lcd_touch_read_data(s_touch);

        esp_lcd_touch_point_data_t pt = {};
        uint8_t cnt = 0;
        esp_lcd_touch_get_data(s_touch, &pt, &cnt, 1);

        if (cnt > 0) {
            ESP_LOGI(TAG, "touch-diag: PRESSED  x=%d y=%d", pt.x, pt.y);
            was_pressed = true;
        } else if (was_pressed) {
            ESP_LOGI(TAG, "touch-diag: RELEASED");
            was_pressed = false;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
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

    // Display MCU (addr 0x45) must run before touch init below — it's what
    // releases the GT911's reset line. See prv_mcu_init() for provenance.
    ESP_LOGI(TAG, "init: display MCU (addr 0x%02X)", MCU_I2C_ADDR);
    if ((ret = prv_mcu_init()) != ESP_OK) return ret;

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
