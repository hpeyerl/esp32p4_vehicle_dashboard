// =============================================================
//  tab5_display.c — M5Stack Tab5 Display + Touch Implementation
//
//  See include/tab5_display.h for public API.
//
//  Hardware (from M5Stack UserDemo + schematic):
//    - ST7123 MIPI-DSI panel: portrait 720×1280
//    - DSI: 2 lanes, 965 Mbps, 70 MHz DPI
//    - Timing: hpw=2 hbp=40 hfp=40 vpw=2 vbp=8 vfp=220
//    - I2C: SDA=GPIO31 SCL=GPIO32 (single bus for all peripherals)
//    - PI4IOE5V6416 IO expander at 0x43 controls LCD/touch/camera resets
//    - LDO ch3 at 2500mV powers MIPI DSI PHY
//    - Backlight: GPIO22 via LEDC PWM
//    - Touch: ST7123 at I2C addr 0x55, INT=GPIO23
//
//  LVGL integration:
//    - LVGL sees 1280×720 landscape
//    - Flush callback SW-rotates 90° CCW into DPI double framebuffers
//    - on_color_trans_done semaphore prevents overwriting active buffer
// =============================================================

#include "tab5_display.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ldo_regulator.h"
#include <string.h>
#include "driver/ppa.h"
#include "esp_cache.h"

#include "esp_lcd_ili9881c.h"
#include "esp_lcd_st7123.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_st7123.h"

static const char *TAG = "tab5_disp";

// ── Pin / bus configuration ───────────────────────────────────────────────
#define TAB5_I2C_SDA            31
#define TAB5_I2C_SCL            32
#define TAB5_TOUCH_INT          23
#define TAB5_BACKLIGHT_GPIO     22
#define TAB5_LDO_CHAN           3
#define TAB5_LDO_MV             2500
#define TAB5_PI4IOE1_ADDR       0x43

// PI4IOE5V6416 register addresses
#define PI4IO_REG_CHIP_RESET    0x01
#define PI4IO_REG_IO_DIR        0x03
#define PI4IO_REG_OUT_H_IM      0x07
#define PI4IO_REG_PULL_SEL      0x0D
#define PI4IO_REG_PULL_EN       0x0B
#define PI4IO_REG_OUT_SET       0x05

// ── DSI / DPI timing (portrait 720×1280) ─────────────────────────────────
#define TAB5_DSI_LANE_NUM       2
#define TAB5_DSI_LANE_MBPS      965
#define TAB5_DPI_CLK_MHZ        70
#define TAB5_HSYNC_PULSE_WIDTH  2
#define TAB5_HSYNC_BACK_PORCH   40
#define TAB5_HSYNC_FRONT_PORCH  40
#define TAB5_VSYNC_PULSE_WIDTH  2
#define TAB5_VSYNC_BACK_PORCH   8
#define TAB5_VSYNC_FRONT_PORCH  220

// ── Module-private handles ────────────────────────────────────────────────
static esp_lcd_panel_handle_t   s_panel       = NULL;
static esp_lcd_touch_handle_t   s_touch       = NULL;
static i2c_master_bus_handle_t  s_i2c         = NULL;
static i2c_master_dev_handle_t  s_pi4ioe1     = NULL;
static esp_lcd_dsi_bus_handle_t s_dsi_bus     = NULL;
static esp_ldo_channel_handle_t s_ldo         = NULL;
static ppa_client_handle_t      s_ppa         = NULL;
static lv_display_t            *s_lvgl_disp   = NULL;

// Called from DPI ISR at vsync — signals LVGL flush complete
static bool IRAM_ATTR prv_dpi_trans_done_cb(esp_lcd_panel_handle_t panel,
                                             esp_lcd_dpi_panel_event_data_t *edata,
                                             void *user_ctx)
{
    lv_display_flush_ready(s_lvgl_disp);
    return false;
}

// Output rotation buffer — 720×1280 RGB565, 128-byte aligned for PPA+DMA
static void *s_rot_buf  = NULL;

// ── Custom ST7123 init commands (from M5Stack UserDemo) ───────────────────
static const st7123_lcd_init_cmd_t s_st7123_init_cmds[] = {
    {0x60, (uint8_t[]){0x71, 0x23, 0xa2}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa3}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa4}, 3, 0},
    {0xA4, (uint8_t[]){0x31}, 1, 0},
    {0xD7, (uint8_t[]){0x10, 0x0A, 0x10, 0x2A, 0x80, 0x80}, 6, 0},
    {0x90, (uint8_t[]){0x71, 0x23, 0x5A, 0x20, 0x24, 0x09, 0x09}, 7, 0},
    {0xA3, (uint8_t[]){0x80,0x01,0x88,0x30,0x05,0x00,0x00,0x00,0x00,0x00,0x46,0x00,0x00,
                       0x1E,0x5C,0x1E,0x80,0x00,0x4F,0x05,0x00,0x00,0x00,0x00,0x00,0x46,
                       0x00,0x00,0x1E,0x5C,0x1E,0x80,0x00,0x6F,0x58,0x00,0x00,0x00,0xFF}, 40, 0},
    {0xA6, (uint8_t[]){0x03,0x00,0x24,0x55,0x36,0x00,0x39,0x00,0x6E,0x6E,0x91,0xFF,0x00,0x24,
                       0x55,0x38,0x00,0x37,0x00,0x6E,0x6E,0x91,0xFF,0x00,0x24,0x11,0x00,0x00,
                       0x00,0x00,0x6E,0x6E,0x91,0xFF,0x00,0xEC,0x11,0x00,0x03,0x00,0x03,0x6E,
                       0x6E,0xFF,0xFF,0x00,0x08,0x80,0x08,0x80,0x06,0x00,0x00,0x00,0x00}, 55, 0},
    {0xA7, (uint8_t[]){0x19,0x19,0x80,0x64,0x40,0x07,0x16,0x40,0x00,0x44,0x03,0x6E,0x6E,0x91,0xFF,
                       0x08,0x80,0x64,0x40,0x25,0x34,0x40,0x00,0x02,0x01,0x6E,0x6E,0x91,0xFF,0x08,
                       0x80,0x64,0x40,0x00,0x00,0x40,0x00,0x00,0x00,0x6E,0x6E,0x91,0xFF,0x08,0x80,
                       0x64,0x40,0x00,0x00,0x00,0x00,0x20,0x00,0x6E,0x6E,0x84,0xFF,0x08,0x80,0x44}, 60, 0},
    {0xAC, (uint8_t[]){0x03,0x19,0x19,0x18,0x18,0x06,0x13,0x13,0x11,0x11,0x08,0x08,0x0A,0x0A,0x1C,
                       0x1C,0x07,0x07,0x00,0x00,0x02,0x02,0x01,0x19,0x19,0x18,0x18,0x06,0x12,0x12,
                       0x10,0x10,0x09,0x09,0x0B,0x0B,0x1C,0x1C,0x07,0x07,0x03,0x03,0x01,0x01}, 44, 0},
    {0xAD, (uint8_t[]){0xF0,0x00,0x46,0x00,0x03,0x50,0x50,0xFF,0xFF,0xF0,0x40,0x06,0x01,
                       0x07,0x42,0x42,0xFF,0xFF,0x01,0x00,0x00,0xFF,0xFF,0xFF,0xFF}, 25, 0},
    {0xAE, (uint8_t[]){0xFE,0x3F,0x3F,0xFE,0x3F,0x3F,0x00}, 7, 0},
    {0xB2, (uint8_t[]){0x15,0x19,0x05,0x23,0x49,0xAF,0x03,0x2E,0x5C,0xD2,0xFF,0x10,0x20,0xFD,0x20,0xC0,0x00}, 17, 0},
    {0xE8, (uint8_t[]){0x20,0x6F,0x04,0x97,0x97,0x3E,0x04,0xDC,0xDC,0x3E,0x06,0xFA,0x26,0x3E}, 15, 0},
    {0x75, (uint8_t[]){0x03,0x04}, 2, 0},
    {0xE7, (uint8_t[]){0x3B,0x00,0x00,0x7C,0xA1,0x8C,0x20,0x1A,0xF0,0xB1,0x50,0x00,
                       0x50,0xB1,0x50,0xB1,0x50,0xD8,0x00,0x55,0x00,0xB1,0x00,0x45,
                       0xC9,0x6A,0xFF,0x5A,0xD8,0x18,0x88,0x15,0xB1,0x01,0x01,0x77}, 36, 0},
    {0xEA, (uint8_t[]){0x13,0x00,0x04,0x00,0x00,0x00,0x00,0x2C}, 8, 0},
    {0xB0, (uint8_t[]){0x22,0x43,0x11,0x61,0x25,0x43,0x43}, 7, 0},
    {0xb7, (uint8_t[]){0x00,0x00,0x73,0x73}, 4, 0},
    {0xBF, (uint8_t[]){0xA6,0xAA}, 2, 0},
    {0xA9, (uint8_t[]){0x00,0x00,0x73,0xFF,0x00,0x00,0x03,0x00,0x00,0x03}, 10, 0},
    {0xC8, (uint8_t[]){0x00,0x00,0x10,0x1F,0x36,0x00,0x5D,0x04,0x9D,0x05,0x10,0xF2,0x06,
                       0x60,0x03,0x11,0xAD,0x00,0xEF,0x01,0x22,0x2E,0x0E,0x74,0x08,0x32,
                       0xDC,0x09,0x33,0x0F,0xF3,0x77,0x0D,0xB0,0xDC,0x03,0xFF}, 37, 0},
    {0xC9, (uint8_t[]){0x00,0x00,0x10,0x1F,0x36,0x00,0x5D,0x04,0x9D,0x05,0x10,0xF2,0x06,
                       0x60,0x03,0x11,0xAD,0x00,0xEF,0x01,0x22,0x2E,0x0E,0x74,0x08,0x32,
                       0xDC,0x09,0x33,0x0F,0xF3,0x77,0x0D,0xB0,0xDC,0x03,0xFF}, 37, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},   // MADCTL: portrait (SW rotation in flush cb)
    {0x11, (uint8_t[]){0x00}, 1, 100}, // Sleep out + 100ms
    {0x29, (uint8_t[]){0x00}, 1, 0},   // Display on
    {0x35, (uint8_t[]){0x00}, 1, 100}, // Tearing effect line on
};

// ── PPA hardware rotation: landscape 1280×720 → portrait 720×1280 ─────────
// Uses ESP32-P4 PPA SRM (Scale/Rotate/Mirror) engine — no CPU pixel loop.
// Input:  LVGL PSRAM buffer [RGB565, 1280×720]
// Output: DPI framebuffer   [RGB565,  720×1280] (internal SRAM, DMA-safe)
// 90° CCW = PPA_SRM_ROTATION_ANGLE_270
static void prv_ppa_rotate(const void *src, void *dst,
                             uint16_t src_w, uint16_t src_h)
{
    // Writeback PSRAM cache so PPA DMA sees current pixel data
    esp_cache_msync((void *)src,
                    (size_t)src_w * src_h * sizeof(uint16_t),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    ppa_srm_oper_config_t cfg = {
        .in.buffer          = src,
        .in.pic_w           = src_w,
        .in.pic_h           = src_h,
        .in.block_w         = src_w,
        .in.block_h         = src_h,
        .in.block_offset_x  = 0,
        .in.block_offset_y  = 0,
        .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer         = dst,
        .out.buffer_size    = (size_t)src_w * src_h * sizeof(uint16_t),
        .out.pic_w          = src_h,   // portrait width  = landscape height
        .out.pic_h          = src_w,   // portrait height = landscape width
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

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(s_ppa, &cfg));
}

// ── LVGL flush callback ───────────────────────────────────────────────────
// PPA rotates LVGL landscape buffer → portrait rotation buffer.
// draw_bitmap triggers DMA to the panel.
// on_color_trans_done ISR calls lv_display_flush_ready when DMA completes.
static void prv_lvgl_flush_cb(lv_display_t *disp,
                               const lv_area_t *area,
                               uint8_t *px_map)
{
    if (!s_panel || !px_map || !s_rot_buf) {
        lv_display_flush_ready(disp);
        return;
    }

    // Writeback PSRAM cache so PPA DMA sees current pixels
    esp_cache_msync(px_map,
                    (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    // PPA: rotate landscape 1280×720 → portrait 720×1280
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
        .out.buffer_size    = (size_t)720 * 1280 * sizeof(uint16_t),
        .out.pic_w          = 720,
        .out.pic_h          = 1280,
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

    // PPA output is in s_rot_buf — send directly to panel
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, 720, 1280, s_rot_buf);
    // flush_ready called from on_color_trans_done ISR
}

// ── LVGL touch callback ───────────────────────────────────────────────────
static void prv_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (!s_touch) return;
    esp_lcd_touch_read_data(s_touch);
    esp_lcd_touch_point_data_t pt = {};
    uint8_t cnt = 0;
    esp_lcd_touch_get_data(s_touch, &pt, &cnt, 1);
    if (cnt > 0) {
        data->point.x = (lv_coord_t)pt.x;
        data->point.y = (lv_coord_t)pt.y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── LDO init ─────────────────────────────────────────────────────────────
static esp_err_t prv_ldo_init(void)
{
    esp_ldo_channel_config_t cfg = {
        .chan_id    = TAB5_LDO_CHAN,
        .voltage_mv = TAB5_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&cfg, &s_ldo),
                        TAG, "LDO init failed");
    ESP_LOGI(TAG, "LDO ch%d at %dmV OK", TAB5_LDO_CHAN, TAB5_LDO_MV);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

// ── I2C bus init ──────────────────────────────────────────────────────────
static esp_err_t prv_i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port    = I2C_NUM_0,
        .sda_io_num  = (gpio_num_t)TAB5_I2C_SDA,
        .scl_io_num  = (gpio_num_t)TAB5_I2C_SCL,
        .clk_source  = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c);
}

// ── PI4IOE IO expander init ───────────────────────────────────────────────
static esp_err_t prv_pi4ioe_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_pi4ioe1, buf, 2, 50);
}

static esp_err_t prv_pi4ioe_init(void)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TAB5_PI4IOE1_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev_cfg, &s_pi4ioe1),
                        TAG, "PI4IOE add device failed");

    prv_pi4ioe_write(PI4IO_REG_CHIP_RESET, 0xFF);          // soft reset
    prv_pi4ioe_write(PI4IO_REG_IO_DIR,     0b01111111);    // P7=input
    prv_pi4ioe_write(PI4IO_REG_OUT_H_IM,   0b00000000);    // no high-impedance
    prv_pi4ioe_write(PI4IO_REG_PULL_SEL,   0b01111111);    // pull-up P0-P6
    prv_pi4ioe_write(PI4IO_REG_PULL_EN,    0b01111111);    // enable pulls
    // Release LCD(P4), touch(P5), camera(P6), speaker(P1), ext5V(P2) from reset
    ESP_RETURN_ON_ERROR(
        prv_pi4ioe_write(PI4IO_REG_OUT_SET, 0b01110110),
        TAG, "PI4IOE output set failed");

    ESP_LOGI(TAG, "PI4IOE: LCD/touch/camera released from reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
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
        .gpio_num   = TAB5_BACKLIGHT_GPIO,
        .duty       = (1 << 10) - 1,  // 100%
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    ESP_LOGI(TAG, "backlight ON (GPIO%d LEDC 100%%)", TAB5_BACKLIGHT_GPIO);
}

// ── ST7123 DSI panel init ─────────────────────────────────────────────────
static esp_err_t prv_panel_init(void)
{
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = TAB5_DSI_LANE_NUM,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = TAB5_DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus),
                        TAG, "DSI bus failed");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t io_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &io_cfg, &io),
                        TAG, "Panel IO failed");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = TAB5_DPI_CLK_MHZ,
        .virtual_channel    = 0,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs            = 2,
        .video_timing = {
            .h_size            = 720,
            .v_size            = 1280,
            .hsync_pulse_width = TAB5_HSYNC_PULSE_WIDTH,
            .hsync_back_porch  = TAB5_HSYNC_BACK_PORCH,
            .hsync_front_porch = TAB5_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = TAB5_VSYNC_PULSE_WIDTH,
            .vsync_back_porch  = TAB5_VSYNC_BACK_PORCH,
            .vsync_front_porch = TAB5_VSYNC_FRONT_PORCH,
        },
        .flags.use_dma2d = true,
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 24,
    };

    st7123_vendor_config_t vendor_cfg = {
        .init_cmds      = s_st7123_init_cmds,
        .init_cmds_size = sizeof(s_st7123_init_cmds) / sizeof(s_st7123_init_cmds[0]),
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };
    panel_cfg.vendor_config = &vendor_cfg;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7123(io, &panel_cfg, &s_panel),
                        TAG, "ST7123 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Display on failed");

    // Initialise PPA SRM client for hardware rotation
    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_RETURN_ON_ERROR(ppa_register_client(&ppa_cfg, &s_ppa),
                        TAG, "PPA client register failed");

    // Allocate rotation output buffer: portrait 720×1280 RGB565
    // Must be 128-byte aligned for PPA DMA and esp_cache_msync
    s_rot_buf = heap_caps_aligned_alloc(128,
                    720 * 1280 * sizeof(uint16_t),
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_ERROR(s_rot_buf ? ESP_OK : ESP_ERR_NO_MEM,
                        TAG, "rotation buffer alloc failed");
    ESP_LOGI(TAG, "PPA SRM ready, rot_buf=%p", s_rot_buf);

    ESP_LOGI(TAG, "ST7123 panel ready — portrait 720×1280, LVGL %d×%d landscape",
             LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

// ── Touch controller init ─────────────────────────────────────────────────
static esp_err_t prv_touch_init(void)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr            = 0x55,
        .scl_speed_hz        = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset       = 0,
        .lcd_cmd_bits        = 16,
        .flags.disable_control_phase = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c, &tp_io_cfg, &tp_io),
                        TAG, "Touch IO failed");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .int_gpio_num = (gpio_num_t)TAB5_TOUCH_INT,
        .rst_gpio_num = (gpio_num_t)-1,
        .levels = { .reset = 0, .interrupt = 0 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_st7123(tp_io, &tp_cfg, &s_touch),
                        TAG, "ST7123 touch init failed");
    ESP_LOGI(TAG, "ST7123 touch ready");
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────
esp_lcd_panel_handle_t tab5_get_panel(void)
{
    return s_panel;
}

esp_err_t tab5_display_init(lv_display_t **disp_out)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "init: LDO ch%d at %dmV", TAB5_LDO_CHAN, TAB5_LDO_MV);
    if ((ret = prv_ldo_init()) != ESP_OK) return ret;

    ESP_LOGI(TAG, "init: I2C SDA=%d SCL=%d", TAB5_I2C_SDA, TAB5_I2C_SCL);
    if ((ret = prv_i2c_init()) != ESP_OK) return ret;

    ESP_LOGI(TAG, "init: PI4IOE — releasing resets");
    if ((ret = prv_pi4ioe_init()) != ESP_OK) return ret;

    ESP_LOGI(TAG, "init: backlight");
    prv_backlight_on();

    ESP_LOGI(TAG, "init: ST7123 DSI panel (%d Mbps, %d MHz DPI)",
             TAB5_DSI_LANE_MBPS, TAB5_DPI_CLK_MHZ);
    if ((ret = prv_panel_init()) != ESP_OK) return ret;

    ESP_LOGI(TAG, "init: touch");
    if ((ret = prv_touch_init()) != ESP_OK) return ret;

    ESP_LOGI(TAG, "init: LVGL display %d×%d", LCD_H_RES, LCD_V_RES);
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    s_lvgl_disp = disp;
    lv_display_set_flush_cb(disp, prv_lvgl_flush_cb);
    // Register on_refresh_done — fires at vsync with num_fbs=2
    esp_lcd_dpi_panel_event_callbacks_t dpi_cbs = {
        .on_refresh_done = prv_dpi_trans_done_cb,
    };
    ESP_ERROR_CHECK(
        esp_lcd_dpi_panel_register_event_callbacks(s_panel, &dpi_cbs, NULL));
    ESP_LOGI(TAG, "DPI on_refresh_done + flush_wait_cb registered");

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, prv_lvgl_touch_cb);

    *disp_out = disp;
    ESP_LOGI(TAG, "tab5_display_init complete");
    return ESP_OK;
}
