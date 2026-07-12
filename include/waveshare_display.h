// =============================================================
//  waveshare_display.h — Waveshare 12.3" Display + GT911 Touch
//
//  Internal backend — do NOT include this from app code.
//  Include display_driver.h instead.
//
//  Panel:  Himax HX8399-C, 4-lane MIPI-DSI, portrait 720×1920
//  Touch:  Goodix GT911, I2C
//  LVGL:   landscape 1920×720 (PPA-rotated in flush cb)
// =============================================================

#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t              ws_display_init(lv_display_t **disp_out);
esp_lcd_panel_handle_t ws_get_panel(void);

// Standalone touch test — bypasses the DSI panel entirely. Never returns.
void ws_touch_diag_run(void);

#ifdef __cplusplus
}
#endif
