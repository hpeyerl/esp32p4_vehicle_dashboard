// =============================================================
//  display_driver.h — Unified Display + Touch Public API
//
//  Backend selected at compile time via -DDISPLAY_TARGET=<n>:
//
//    DISPLAY_TARGET 1  →  M5Stack Tab5   (ST7123,  720×1280)
//    DISPLAY_TARGET 2  →  Waveshare 12.3 (HX8399-C, 720×1920)
//
//  Or for headless/stub mode (no display hardware required):
//    DISPLAY_STUB 1    →  Software framebuffer only
//                         Use display_stub_get_fb() for MJPEG
//
//  Usage (unchanged regardless of target):
//    lv_init();
//    lv_display_t *disp;
//    ESP_ERROR_CHECK(display_init(&disp));
// =============================================================

#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifndef DISPLAY_STUB
  #define DISPLAY_STUB 0
#endif

#if !DISPLAY_STUB
  #ifndef DISPLAY_TARGET
    #error "Set -DDISPLAY_TARGET=1 (Tab5), =2 (Waveshare) or -DDISPLAY_STUB=1"
  #endif
  #define DISPLAY_TARGET_TAB5       1
  #define DISPLAY_TARGET_WAVESHARE  2
#endif

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t              display_init(lv_display_t **disp_out);
esp_lcd_panel_handle_t display_get_panel(void);

#ifdef __cplusplus
}
#endif
