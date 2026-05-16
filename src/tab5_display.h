/*
 * tab5_display.h  —  M5Stack Tab5 Display + Touch Initialisation
 *
 * Based directly on M5Stack UserDemo source:
 *   platforms/tab5/components/m5stack_tab5/m5stack_tab5.c
 *   platforms/tab5/components/m5stack_tab5/include/bsp/display.h
 *   platforms/tab5/components/m5stack_tab5/priv_include/esp_lcd_st7123.h
 *
 * KEY FACTS:
 *   - Panel is PORTRAIT: h_size=720, v_size=1280 in DPI config
 *   - LVGL is configured 1280x720 and we use swap_xy to get landscape
 *   - Lane bit rate: 965 Mbps
 *   - DPI clock: 70 MHz
 *   - bits_per_pixel: 24 in panel_dev_config (even though pixel format is RGB565)
 *   - data_endian: LCD_RGB_DATA_ENDIAN_LITTLE
 *   - flags.use_dma2d = true
 *   - Custom init commands (different from generic esp_lcd_st7123 defaults)
 *   - LDO channel 3 at 2500mV powers MIPI DSI PHY
 *   - ALL I2C on GPIO31(SDA)/GPIO32(SCL), single bus
 *   - PI4IOE at 0x43: register 0x05 = 0b01110110 releases LCD/touch/camera reset
 *   - Backlight via LEDC PWM on GPIO22 (not simple GPIO)
 */

#pragma once

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
#include "lvgl.h"
#include "esp_heap_caps.h"
#include <string.h>
#include "freertos/semphr.h"

#include "esp_lcd_ili9881c.h"
#include "esp_lcd_st7123.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_st7123.h"

static const char *TAG_DISP = "tab5_disp";

// ── Resolution — LVGL sees landscape, panel is physically portrait ────────
// DPI video_timing uses 720x1280 (portrait), LVGL uses 1280x720 (landscape)
#ifndef TAB5_DISPLAY_ILI9881C
  #define TAB5_DISPLAY_ILI9881C  0
#endif

// ── DSI/DPI timing — from bsp_display_new_with_handles_to_st7123() ───────
#define TAB5_DSI_LANE_NUM        2
#define TAB5_DSI_LANE_MBPS       965
#define TAB5_DPI_CLK_MHZ         70
// Portrait timing (h=720, v=1280)
#define TAB5_HSYNC_PULSE_WIDTH   2
#define TAB5_HSYNC_BACK_PORCH    40
#define TAB5_HSYNC_FRONT_PORCH   40
#define TAB5_VSYNC_PULSE_WIDTH   2
#define TAB5_VSYNC_BACK_PORCH    8
#define TAB5_VSYNC_FRONT_PORCH   220

// ── Hardware pins ─────────────────────────────────────────────────────────
#define TAB5_I2C_SDA         31
#define TAB5_I2C_SCL         32
#define TAB5_TOUCH_INT       23
#define TAB5_BACKLIGHT_GPIO  22
#define TAB5_LDO_CHAN        3
#define TAB5_LDO_MV          2500
#define TAB5_PI4IOE1_ADDR    0x43

// PI4IOE register map (PI4IOE5V6416)
#define PI4IO_REG_CHIP_RESET  0x01
#define PI4IO_REG_IO_DIR      0x03
#define PI4IO_REG_OUT_H_IM    0x07
#define PI4IO_REG_PULL_SEL    0x0D
#define PI4IO_REG_PULL_EN     0x0B
#define PI4IO_REG_OUT_SET     0x05

// ── Module handles ────────────────────────────────────────────────────────
static esp_lcd_panel_handle_t    s_panel   = NULL;
static esp_lcd_touch_handle_t    s_touch   = NULL;
static i2c_master_bus_handle_t   s_i2c     = NULL;
static esp_lcd_dsi_bus_handle_t  s_dsi_bus = NULL;
static SemaphoreHandle_t          s_refresh_sem = NULL;
static esp_ldo_channel_handle_t  s_ldo     = NULL;
static i2c_master_dev_handle_t   s_pi4ioe1 = NULL;

// ── Custom ST7123 init commands — from M5Stack UserDemo m5stack_tab5.c ────
// These differ from the generic esp_lcd_st7123 component defaults.
// Using these is required for the Tab5 ST7123 panel.
static const st7123_lcd_init_cmd_t s_tab5_st7123_init_cmds[] = {
    {0x60, (uint8_t[]){0x71, 0x23, 0xa2}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa3}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa4}, 3, 0},
    {0xA4, (uint8_t[]){0x31}, 1, 0},
    {0xD7, (uint8_t[]){0x10, 0x0A, 0x10, 0x2A, 0x80, 0x80}, 6, 0},
    {0x90, (uint8_t[]){0x71, 0x23, 0x5A, 0x20, 0x24, 0x09, 0x09}, 7, 0},
    {0xA3, (uint8_t[]){0x80, 0x01, 0x88, 0x30, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00,
                       0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x4F, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
                       0x00, 0x00, 0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x6F, 0x58, 0x00, 0x00, 0x00, 0xFF},
     40, 0},
    {0xA6, (uint8_t[]){0x03, 0x00, 0x24, 0x55, 0x36, 0x00, 0x39, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24,
                       0x55, 0x38, 0x00, 0x37, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24, 0x11, 0x00, 0x00,
                       0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0xEC, 0x11, 0x00, 0x03, 0x00, 0x03, 0x6E,
                       0x6E, 0xFF, 0xFF, 0x00, 0x08, 0x80, 0x08, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00},
     55, 0},
    {0xA7, (uint8_t[]){0x19, 0x19, 0x80, 0x64, 0x40, 0x07, 0x16, 0x40, 0x00, 0x44, 0x03, 0x6E, 0x6E, 0x91, 0xFF,
                       0x08, 0x80, 0x64, 0x40, 0x25, 0x34, 0x40, 0x00, 0x02, 0x01, 0x6E, 0x6E, 0x91, 0xFF, 0x08,
                       0x80, 0x64, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x08, 0x80,
                       0x64, 0x40, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x6E, 0x6E, 0x84, 0xFF, 0x08, 0x80, 0x44},
     60, 0},
    {0xAC, (uint8_t[]){0x03, 0x19, 0x19, 0x18, 0x18, 0x06, 0x13, 0x13, 0x11, 0x11, 0x08, 0x08, 0x0A, 0x0A, 0x1C,
                       0x1C, 0x07, 0x07, 0x00, 0x00, 0x02, 0x02, 0x01, 0x19, 0x19, 0x18, 0x18, 0x06, 0x12, 0x12,
                       0x10, 0x10, 0x09, 0x09, 0x0B, 0x0B, 0x1C, 0x1C, 0x07, 0x07, 0x03, 0x03, 0x01, 0x01},
     44, 0},
    {0xAD, (uint8_t[]){0xF0, 0x00, 0x46, 0x00, 0x03, 0x50, 0x50, 0xFF, 0xFF, 0xF0, 0x40, 0x06, 0x01,
                       0x07, 0x42, 0x42, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF},
     25, 0},
    {0xAE, (uint8_t[]){0xFE, 0x3F, 0x3F, 0xFE, 0x3F, 0x3F, 0x00}, 7, 0},
    {0xB2, (uint8_t[]){0x15, 0x19, 0x05, 0x23, 0x49, 0xAF, 0x03, 0x2E, 0x5C, 0xD2, 0xFF, 0x10, 0x20, 0xFD, 0x20, 0xC0, 0x00},
     17, 0},
    {0xE8, (uint8_t[]){0x20, 0x6F, 0x04, 0x97, 0x97, 0x3E, 0x04, 0xDC, 0xDC, 0x3E, 0x06, 0xFA, 0x26, 0x3E}, 15, 0},
    {0x75, (uint8_t[]){0x03, 0x04}, 2, 0},
    {0xE7, (uint8_t[]){0x3B, 0x00, 0x00, 0x7C, 0xA1, 0x8C, 0x20, 0x1A, 0xF0, 0xB1, 0x50, 0x00,
                       0x50, 0xB1, 0x50, 0xB1, 0x50, 0xD8, 0x00, 0x55, 0x00, 0xB1, 0x00, 0x45,
                       0xC9, 0x6A, 0xFF, 0x5A, 0xD8, 0x18, 0x88, 0x15, 0xB1, 0x01, 0x01, 0x77},
     36, 0},
    {0xEA, (uint8_t[]){0x13, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x2C}, 8, 0},
    {0xB0, (uint8_t[]){0x22, 0x43, 0x11, 0x61, 0x25, 0x43, 0x43}, 7, 0},
    {0xb7, (uint8_t[]){0x00, 0x00, 0x73, 0x73}, 4, 0},
    {0xBF, (uint8_t[]){0xA6, 0xAA}, 2, 0},
    {0xA9, (uint8_t[]){0x00, 0x00, 0x73, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03}, 10, 0},
    {0xC8, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0xC9, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},  // MADCTL: portrait (SW rotation in flush)
    {0x11, (uint8_t[]){0x00}, 1, 100},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 100},
};

// ── LVGL flush callback ───────────────────────────────────────────────────
// on_refresh_done fires from ISR when the DPI controller finishes
// scanning the frame buffer to the screen (end of vsync period).
// Must be in IRAM.
static bool IRAM_ATTR tab5_refresh_done_cb(esp_lcd_panel_handle_t panel,
                                            esp_lcd_dpi_panel_event_data_t *edata,
                                            void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    xSemaphoreGiveFromISR(sem, &need_yield);
    return need_yield == pdTRUE;
}

// Rotate landscape 1280x720 → portrait 720x1280 (90° CCW)
// Writes directly into a DPI panel framebuffer (internal SRAM, DMA-safe)
static IRAM_ATTR void sw_rotate_ccw(const uint16_t *src, uint16_t *dst,
                                     int src_w, int src_h)
{
    // 90° CCW: src(x,y) → dst(y, src_w-1-x)
    // dst stride = src_h = 720
    for (int y = 0; y < src_h; y++) {
        const uint16_t *src_row = src + y * src_w;
        for (int x = 0; x < src_w; x++) {
            dst[y + (src_w - 1 - x) * src_h] = src_row[x];
        }
    }
}

static void *s_dpi_fb[2] = {NULL, NULL};  // DPI panel's own framebuffers
static int   s_dpi_fb_idx = 0;

static void tab5_lvgl_flush_cb(lv_display_t *disp,
                                const lv_area_t *area,
                                uint8_t *px_map)
{
    if (s_panel == NULL || px_map == NULL) {
        lv_display_flush_ready(disp);
        return;
    }

    // Get DPI framebuffers on first call
    if (s_dpi_fb[0] == NULL) {
        esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &s_dpi_fb[0], &s_dpi_fb[1]);
        ESP_LOGI(TAG_DISP, "DPI fbs: %p %p", s_dpi_fb[0], s_dpi_fb[1]);
    }

    // Rotate LVGL landscape buffer into the next DPI framebuffer
    uint16_t *dst = (uint16_t *)s_dpi_fb[s_dpi_fb_idx];
    sw_rotate_ccw((const uint16_t *)px_map, dst, LCD_H_RES, LCD_V_RES);

    // Trigger display of the rotated framebuffer
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_V_RES, LCD_H_RES, dst);
    s_dpi_fb_idx = 1 - s_dpi_fb_idx;

    xSemaphoreTake(s_refresh_sem, portMAX_DELAY);
    lv_display_flush_ready(disp);
}

// ── LVGL touch callback ───────────────────────────────────────────────────
static void tab5_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (!s_touch) return;
    esp_lcd_touch_read_data(s_touch);
    esp_lcd_touch_point_data_t points[1] = {};
    uint8_t cnt = 0;
    esp_lcd_touch_get_data(s_touch, points, &cnt, 1);
    if (cnt > 0) {
        data->point.x = (lv_coord_t)points[0].x;
        data->point.y = (lv_coord_t)points[0].y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── LDO init ──────────────────────────────────────────────────────────────
static esp_err_t tab5_ldo_init(void)
{
    esp_ldo_channel_config_t cfg = {};
    cfg.chan_id    = TAB5_LDO_CHAN;
    cfg.voltage_mv = TAB5_LDO_MV;
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&cfg, &s_ldo),
                        TAG_DISP, "LDO init failed");
    ESP_LOGI(TAG_DISP, "LDO ch%d at %dmV OK", TAB5_LDO_CHAN, TAB5_LDO_MV);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

// ── I2C init ──────────────────────────────────────────────────────────────
static esp_err_t tab5_i2c_init(void)
{
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port    = I2C_NUM_0;
    cfg.sda_io_num  = (gpio_num_t)TAB5_I2C_SDA;
    cfg.scl_io_num  = (gpio_num_t)TAB5_I2C_SCL;
    cfg.clk_source  = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&cfg, &s_i2c);
}

// ── PI4IOE full init — matches bsp_io_expander_pi4ioe_init() exactly ──────
static esp_err_t tab5_pi4ioe_init(void)
{
    uint8_t buf[2];

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = TAB5_PI4IOE1_ADDR;
    dev_cfg.scl_speed_hz    = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev_cfg, &s_pi4ioe1),
                        TAG_DISP, "PI4IOE add device failed");

    // Software reset
    buf[0] = PI4IO_REG_CHIP_RESET; buf[1] = 0xFF;
    i2c_master_transmit(s_pi4ioe1, buf, 2, 50);

    // Direction: P7=input, all others output
    buf[0] = PI4IO_REG_IO_DIR; buf[1] = 0b01111111;
    i2c_master_transmit(s_pi4ioe1, buf, 2, 50);

    // High-impedance: disable for used pins
    buf[0] = PI4IO_REG_OUT_H_IM; buf[1] = 0b00000000;
    i2c_master_transmit(s_pi4ioe1, buf, 2, 50);

    // Pull select: pull-up for P0-P6
    buf[0] = PI4IO_REG_PULL_SEL; buf[1] = 0b01111111;
    i2c_master_transmit(s_pi4ioe1, buf, 2, 50);

    // Pull enable
    buf[0] = PI4IO_REG_PULL_EN; buf[1] = 0b01111111;
    i2c_master_transmit(s_pi4ioe1, buf, 2, 50);

    // Output: P1(SPK_EN), P2(EXT5V_EN), P4(LCD_RST), P5(TP_RST), P6(CAM_RST) high
    // 0b01110110 = bits 1,2,4,5,6 set
    buf[0] = PI4IO_REG_OUT_SET; buf[1] = 0b01110110;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_pi4ioe1, buf, 2, 50),
                        TAG_DISP, "PI4IOE output set failed");

    ESP_LOGI(TAG_DISP, "PI4IOE init OK — LCD/touch/camera released from reset");
    vTaskDelay(pdMS_TO_TICKS(120));  // ST7123 needs 120ms after reset
    return ESP_OK;
}

// ── Backlight via LEDC PWM — matches bsp_display_brightness_init() ────────
static void tab5_backlight_on(void)
{
    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num       = LEDC_TIMER_1;
    timer.freq_hz         = 5000;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {};
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = LEDC_CHANNEL_1;
    ch.timer_sel  = LEDC_TIMER_1;
    ch.intr_type  = LEDC_INTR_DISABLE;
    ch.gpio_num   = TAB5_BACKLIGHT_GPIO;
    ch.duty       = (1 << 10) - 1;  // 100% brightness
    ch.hpoint     = 0;
    ledc_channel_config(&ch);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    ESP_LOGI(TAG_DISP, "backlight ON (GPIO%d LEDC)", TAB5_BACKLIGHT_GPIO);
}

// ── DSI panel init ────────────────────────────────────────────────────────
static esp_err_t tab5_dsi_panel_init(void)
{
    // DSI bus
    esp_lcd_dsi_bus_config_t bus_cfg = {};
    bus_cfg.bus_id             = 0;
    bus_cfg.num_data_lanes     = TAB5_DSI_LANE_NUM;
    bus_cfg.phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_cfg.lane_bit_rate_mbps = TAB5_DSI_LANE_MBPS;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus),
                        TAG_DISP, "DSI bus failed");

    // Panel IO (DBI)
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t io_cfg = {};
    io_cfg.virtual_channel = 0;
    io_cfg.lcd_cmd_bits    = 8;
    io_cfg.lcd_param_bits  = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &io_cfg, &io),
                        TAG_DISP, "Panel IO failed");

    // DPI config — portrait orientation: h=720, v=1280
    esp_lcd_dpi_panel_config_t dpi_cfg = {};
    dpi_cfg.dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_cfg.dpi_clock_freq_mhz = TAB5_DPI_CLK_MHZ;
    dpi_cfg.virtual_channel    = 0;
    dpi_cfg.pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi_cfg.num_fbs            = 2;   // two framebuffers — LVGL writes to one while DPI displays the other
    dpi_cfg.video_timing.h_size            = 720;   // portrait width
    dpi_cfg.video_timing.v_size            = 1280;  // portrait height
    dpi_cfg.video_timing.hsync_pulse_width = TAB5_HSYNC_PULSE_WIDTH;
    dpi_cfg.video_timing.hsync_back_porch  = TAB5_HSYNC_BACK_PORCH;
    dpi_cfg.video_timing.hsync_front_porch = TAB5_HSYNC_FRONT_PORCH;
    dpi_cfg.video_timing.vsync_pulse_width = TAB5_VSYNC_PULSE_WIDTH;
    dpi_cfg.video_timing.vsync_back_porch  = TAB5_VSYNC_BACK_PORCH;
    dpi_cfg.video_timing.vsync_front_porch = TAB5_VSYNC_FRONT_PORCH;
    dpi_cfg.flags.use_dma2d = true;

    // Panel device config
    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = -1;
    panel_cfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE;
    panel_cfg.bits_per_pixel = 24;  // M5Stack BSP uses 24 even for RGB565

    st7123_vendor_config_t vendor_cfg = {};
    vendor_cfg.init_cmds      = s_tab5_st7123_init_cmds;
    vendor_cfg.init_cmds_size = sizeof(s_tab5_st7123_init_cmds) /
                                sizeof(s_tab5_st7123_init_cmds[0]);
    vendor_cfg.mipi_config.dsi_bus    = s_dsi_bus;
    vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
    panel_cfg.vendor_config = &vendor_cfg;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7123(io, &panel_cfg, &s_panel),
                        TAG_DISP, "ST7123 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel),              TAG_DISP, "Reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),               TAG_DISP, "Init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true),  TAG_DISP, "Display on failed");

    // Create refresh semaphore — pre-give so first draw_bitmap proceeds immediately
    s_refresh_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(s_refresh_sem);

    // Register on_refresh_done callback — fires at end of each frame scan (vsync)
    // Callback MUST be in IRAM (enforced by driver)
    esp_lcd_dpi_panel_event_callbacks_t dpi_cbs = {};
    dpi_cbs.on_color_trans_done = tab5_refresh_done_cb;
    ESP_RETURN_ON_ERROR(
        esp_lcd_dpi_panel_register_event_callbacks(s_panel, &dpi_cbs, s_refresh_sem),
        TAG_DISP, "Register DPI callbacks failed");
    ESP_LOGI(TAG_DISP, "color_trans_done semaphore registered");

    // Orientation: set via 0x36 in init_cmds (driver warns but does not override)


    ESP_LOGI(TAG_DISP, "ST7123 panel ready (portrait 720x1280, LVGL landscape %dx%d)",
             LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

// ── Touch init ────────────────────────────────────────────────────────────
static esp_err_t tab5_touch_init(void)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
    tp_io_cfg.dev_addr            = 0x55;
    tp_io_cfg.scl_speed_hz        = 400000;
    tp_io_cfg.control_phase_bytes = 1;
    tp_io_cfg.dc_bit_offset       = 0;
    tp_io_cfg.lcd_cmd_bits        = 16;
    tp_io_cfg.flags.disable_control_phase = 1;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c, &tp_io_cfg, &tp_io),
                        TAG_DISP, "Touch IO failed");

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max        = LCD_H_RES;
    tp_cfg.y_max        = LCD_V_RES;
    tp_cfg.int_gpio_num = (gpio_num_t)TAB5_TOUCH_INT;
    tp_cfg.rst_gpio_num = (gpio_num_t)-1;
    tp_cfg.levels.reset     = 0;
    tp_cfg.levels.interrupt = 0;

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_st7123(tp_io, &tp_cfg, &s_touch),
                        TAG_DISP, "ST7123 touch failed");
    ESP_LOGI(TAG_DISP, "ST7123 touch ready");
    return ESP_OK;
}

// ── Panel accessor for main.cpp ──────────────────────────────────────────
static inline esp_lcd_panel_handle_t tab5_get_panel(void) { return s_panel; }

// ── Public entry point ────────────────────────────────────────────────────
static esp_err_t tab5_display_init(lv_display_t **disp_out)
{
    esp_err_t ret;

    ESP_LOGI(TAG_DISP, "step 1: LDO ch%d at %dmV...", TAB5_LDO_CHAN, TAB5_LDO_MV);
    ret = tab5_ldo_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG_DISP, "LDO FAILED: %s", esp_err_to_name(ret)); return ret; }

    ESP_LOGI(TAG_DISP, "step 2: I2C init (SDA=GPIO%d SCL=GPIO%d)...", TAB5_I2C_SDA, TAB5_I2C_SCL);
    ret = tab5_i2c_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG_DISP, "I2C FAILED: %s", esp_err_to_name(ret)); return ret; }

    ESP_LOGI(TAG_DISP, "step 3: PI4IOE init + release resets...");
    ret = tab5_pi4ioe_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG_DISP, "PI4IOE FAILED: %s", esp_err_to_name(ret)); return ret; }

    ESP_LOGI(TAG_DISP, "step 4: backlight on...");
    tab5_backlight_on();

    ESP_LOGI(TAG_DISP, "step 5: DSI panel init (%d Mbps, %dMHz DPI)...",
             TAB5_DSI_LANE_MBPS, TAB5_DPI_CLK_MHZ);
    ret = tab5_dsi_panel_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG_DISP, "DSI FAILED: %s", esp_err_to_name(ret)); return ret; }

    ESP_LOGI(TAG_DISP, "step 6: touch init...");
    ret = tab5_touch_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG_DISP, "touch FAILED: %s", esp_err_to_name(ret)); return ret; }

    ESP_LOGI(TAG_DISP, "step 7: LVGL display create (%dx%d)...", LCD_H_RES, LCD_V_RES);
    // LVGL landscape 1280x720; flush cb rotates to portrait 720x1280 for DPI panel
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, tab5_lvgl_flush_cb);

    // Note: panel is physically portrait 720x1280 but DPI config sends
    // portrait data and LVGL is configured 1280x720. Rotation is handled
    // by the ST7123 MADCTL command (0x36) in the init sequence.
    // lv_display_set_rotation() removed — caused heap corruption crashes.

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, tab5_lvgl_touch_cb);

    *disp_out = disp;
    ESP_LOGI(TAG_DISP, "all done!");
    return ESP_OK;
}
