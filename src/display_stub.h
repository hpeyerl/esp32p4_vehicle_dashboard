// =============================================================
//  display_stub.h — Headless display backend
//  Used when DISPLAY_STUB=1 in platformio.ini build_flags.
// =============================================================

#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t              display_stub_init(lv_display_t **disp_out);
esp_lcd_panel_handle_t display_stub_get_panel(void);

// Returns pointer to the most recently completed RGB565 snapshot.
// Always tear-free — copied from LVGL render buffer under mutex.
// out_bytes is set to LCD_H_RES * LCD_V_RES * 2.
void *display_stub_get_fb(size_t *out_bytes);

// Lock/unlock the snapshot buffer for MJPEG encoding.
// Hold the lock for the duration of jpeg_encoder_process() to
// ensure the framebuffer doesn't change mid-encode.
void display_stub_lock(void);
void display_stub_unlock(void);

#ifdef __cplusplus
}
#endif
