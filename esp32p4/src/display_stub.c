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
//  LVGL renders normally; flush callback copies the completed
//  frame to a snapshot buffer protected by a mutex, so the
//  MJPEG streamer always reads a complete tear-free frame.
// =============================================================

#include "display_stub.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "disp_stub";

static void            *s_fb1       = NULL;
static void            *s_fb2       = NULL;
static void            *s_fb_snap   = NULL;   // complete rendered snapshot
static SemaphoreHandle_t s_snap_mutex = NULL;  // protects s_fb_snap

static void prv_flush_cb(lv_display_t *disp,
                          const lv_area_t *area,
                          uint8_t *px_map)
{
    // Copy completed frame into snapshot buffer under mutex
    // so MJPEG streamer never reads a partially-rendered frame.
    size_t fb_bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    if (s_snap_mutex && s_fb_snap) {
        xSemaphoreTake(s_snap_mutex, portMAX_DELAY);
        memcpy(s_fb_snap, px_map, fb_bytes);
        xSemaphoreGive(s_snap_mutex);
    }
    lv_display_flush_ready(disp);
}

esp_err_t display_stub_init(lv_display_t **disp_out)
{
    size_t fb_bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);

    s_fb1 = heap_caps_aligned_alloc(128, fb_bytes,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_fb2 = heap_caps_aligned_alloc(128, fb_bytes,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_fb_snap = heap_caps_aligned_alloc(128, fb_bytes,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!s_fb1 || !s_fb2 || !s_fb_snap) {
        ESP_LOGE(TAG, "framebuffer alloc failed");
        return ESP_ERR_NO_MEM;
    }
    memset(s_fb1,    0, fb_bytes);
    memset(s_fb2,    0, fb_bytes);
    memset(s_fb_snap, 0, fb_bytes);

    s_snap_mutex = xSemaphoreCreateMutex();
    if (!s_snap_mutex) {
        ESP_LOGE(TAG, "mutex create failed");
        return ESP_ERR_NO_MEM;
    }

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
    return s_fb_snap;
}

// Take/give snapshot mutex — called by MJPEG streamer to hold
// a stable frame for the duration of JPEG encoding.
void display_stub_lock(void)
{
    if (s_snap_mutex)
        xSemaphoreTake(s_snap_mutex, portMAX_DELAY);
}

void display_stub_unlock(void)
{
    if (s_snap_mutex)
        xSemaphoreGive(s_snap_mutex);
}
