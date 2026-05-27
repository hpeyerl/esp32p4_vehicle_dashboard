// =============================================================
//  display_stub.c — Headless display backend
//
//  Used when DISPLAY_STUB=1 is set in platformio.ini.
//  Provides a functional LVGL display object backed by a
//  malloc'd framebuffer — no DSI hardware required.
//
//  The framebuffer is accessible via display_stub_get_fb()
//  for MJPEG streaming or other uses.
//
//  LVGL renders normally; flush callback is a no-op that
//  immediately signals flush_ready.
// =============================================================

#include "display_stub.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "disp_stub";

// Framebuffer — landscape LCD_H_RES × LCD_V_RES RGB565
// Allocated in PSRAM, 128-byte aligned for PPA/DMA compatibility
static void *s_fb1     = NULL;
static void *s_fb2     = NULL;
static void *s_fb_live = NULL;  // most recently flushed buffer

static void prv_flush_cb(lv_display_t *disp,
                          const lv_area_t *area,
                          uint8_t *px_map)
{
    // Keep a reference to the last rendered buffer for MJPEG
    s_fb_live = px_map;
    lv_display_flush_ready(disp);
}

esp_err_t display_stub_init(lv_display_t **disp_out)
{
    size_t fb_bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);

    s_fb1 = heap_caps_aligned_alloc(128, fb_bytes,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_fb2 = heap_caps_aligned_alloc(128, fb_bytes,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb1 || !s_fb2) {
        ESP_LOGE(TAG, "framebuffer alloc failed");
        return ESP_ERR_NO_MEM;
    }
    memset(s_fb1, 0, fb_bytes);
    memset(s_fb2, 0, fb_bytes);
    s_fb_live = s_fb1;

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_fb1, s_fb2, fb_bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, prv_flush_cb);

    *disp_out = disp;
    ESP_LOGI(TAG, "display stub ready  %dx%d  fb=%zu B x2 (PSRAM)",
             LCD_H_RES, LCD_V_RES, fb_bytes);
    return ESP_OK;
}

esp_lcd_panel_handle_t display_stub_get_panel(void)
{
    return NULL;
}

void *display_stub_get_fb(size_t *out_bytes)
{
    if (out_bytes)
        *out_bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    return s_fb_live;
}
