// =============================================================
//  tab5_display.h — M5Stack Tab5 Display + Touch Public API
//
//  Initialises:
//    - LDO ch3 (2500mV) for MIPI DSI PHY
//    - I2C bus (GPIO31/32)
//    - PI4IOE IO expander (resets LCD/touch/camera)
//    - LEDC backlight (GPIO22)
//    - ST7123 MIPI-DSI panel (965 Mbps, 70 MHz DPI, portrait 720×1280)
//    - ST7123 touch controller
//    - LVGL display + touch indev
//
//  LVGL is configured landscape 1280×720. The flush callback
//  software-rotates 90° CCW into the DPI panel's double framebuffers.
//
//  Usage:
//    lv_init();
//    lv_display_t *disp;
//    ESP_ERROR_CHECK(tab5_display_init(&disp));
//    // allocate LVGL draw buffers, set on disp, then lv_timer_handler() loop
// =============================================================

#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise display, touch, and LVGL display object.
// On success *disp_out is a ready-to-use lv_display_t.
esp_err_t tab5_display_init(lv_display_t **disp_out);

// Returns the raw ESP-LCD panel handle (needed by main.cpp to get
// DPI framebuffers via esp_lcd_dpi_panel_get_frame_buffer).
esp_lcd_panel_handle_t tab5_get_panel(void);

#ifdef __cplusplus
}
#endif
