// =============================================================
//  display_driver.h — Unified Display + Touch Public API
//
//  This is the ONLY display header main.cpp (or any app code)
//  should ever include.  The backend is selected at compile time
//  via -DDISPLAY_TARGET=<n> in platformio.ini:
//
//    DISPLAY_TARGET 1  →  M5Stack Tab5   (ST7123,  2-lane DSI, 720×1280)
//    DISPLAY_TARGET 2  →  Waveshare 12.3 (HX8399-C, 4-lane DSI, 720×1920)
//
//  Adding a new display:
//    1. Create foo_display.c / foo_display.h following the same
//       internal pattern (prv_* statics, one public init function).
//    2. Add DISPLAY_TARGET N to the #elif chain in display_driver.c.
//    3. Add a new [env:...] block in platformio.ini.
//    4. Declare any new IDF component deps in idf_component.yml.
//
//  Usage (unchanged regardless of target):
//    lv_init();
//    lv_display_t *disp;
//    ESP_ERROR_CHECK(display_init(&disp));
//    // allocate LVGL draw buffers on disp, then lv_timer_handler() loop
// =============================================================

#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

// Compile-time target validation
#ifndef DISPLAY_TARGET
  #error "DISPLAY_TARGET not defined. Set -DDISPLAY_TARGET=1 (Tab5) or =2 (Waveshare 12.3) in platformio.ini"
#endif

#define DISPLAY_TARGET_TAB5        1
#define DISPLAY_TARGET_WAVESHARE   2

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the display, touch controller, and LVGL display object.
// On success *disp_out is a fully configured lv_display_t ready for
// buffer assignment and lv_timer_handler().
esp_err_t display_init(lv_display_t **disp_out);

// Returns the underlying esp-lcd panel handle.
// Needed if you want DPI framebuffer pointers via
// esp_lcd_dpi_panel_get_frame_buffer().
esp_lcd_panel_handle_t display_get_panel(void);

#ifdef __cplusplus
}
#endif
