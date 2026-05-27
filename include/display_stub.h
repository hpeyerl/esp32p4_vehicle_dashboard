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

// Returns pointer to the most recently flushed RGB565 framebuffer.
// out_bytes is set to LCD_H_RES * LCD_V_RES * 2.
// Used by MJPEG streamer.
void *display_stub_get_fb(size_t *out_bytes);

#ifdef __cplusplus
}
#endif
