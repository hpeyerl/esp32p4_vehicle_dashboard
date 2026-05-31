// =============================================================
//  mjpeg_stream.c — MJPEG HTTP stream for debug monitoring
//
//  GET /stream → multipart/x-mixed-replace MJPEG stream
//
//  Both input (RGB888) and output (JPEG) buffers must be
//  allocated with jpeg_alloc_encoder_mem() — the hardware
//  encoder requires specific DMA alignment that only this
//  function guarantees.
// =============================================================

#include "mjpeg_stream.h"
#include "display_stub.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "driver/jpeg_encode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>
#include <stdint.h>

static const char *TAG = "mjpeg";

#define MJPEG_FPS_MAX    5
#define MJPEG_FRAME_MS   (1000 / MJPEG_FPS_MAX)
#define MJPEG_QUALITY    70
#define MJPEG_BOUNDARY   "mjpeg_boundary"

// Dimensions must be multiples of 16 (MCU block size)
#define ENC_W  ((LCD_H_RES / 16) * 16)
#define ENC_H  ((LCD_V_RES / 16) * 16)

// ── RGB565 → RGB888 ───────────────────────────────────────────────────────────
static void prv_rgb565_to_rgb888(const uint16_t *src, uint8_t *dst, int w, int h)
{
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        uint16_t px = src[i];
        // LVGL stores RGB565 big-endian on ESP32 — swap bytes
        px = (px >> 8) | (px << 8);
        uint8_t r = (px >> 11) & 0x1F;
        uint8_t g = (px >>  5) & 0x3F;
        uint8_t b = (px      ) & 0x1F;
        dst[i*3 + 0] = (r << 3) | (r >> 2);
        dst[i*3 + 1] = (g << 2) | (g >> 4);
        dst[i*3 + 2] = (b << 3) | (b >> 2);
    }
}

// ── Stream handler ────────────────────────────────────────────────────────────
esp_err_t mjpeg_stream_handler(httpd_req_t *req)
{
    const size_t n_pixels    = (size_t)ENC_W * ENC_H;
    const size_t rgb_raw_sz  = n_pixels * 3;
    const size_t jpeg_raw_sz = n_pixels * 3;  // upper bound

    // Both buffers MUST use jpeg_alloc_encoder_mem for hardware DMA alignment
    jpeg_encode_memory_alloc_cfg_t in_cfg  = { .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER  };
    jpeg_encode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };

    size_t   rgb_alloc  = 0;
    size_t   jpeg_alloc = 0;
    uint8_t *rgb888     = jpeg_alloc_encoder_mem(rgb_raw_sz,  &in_cfg,  &rgb_alloc);
    uint8_t *jpeg_buf   = jpeg_alloc_encoder_mem(jpeg_raw_sz, &out_cfg, &jpeg_alloc);

    if (!rgb888 || !jpeg_buf) {
        ESP_LOGE(TAG, "alloc failed  rgb=%p jpeg=%p", rgb888, jpeg_buf);
        if (rgb888)   free(rgb888);
        if (jpeg_buf) free(jpeg_buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "alloc ok  rgb=%zu jpeg=%zu bytes", rgb_alloc, jpeg_alloc);

    // Create encoder engine
    jpeg_encoder_handle_t enc = NULL;
    jpeg_encode_engine_cfg_t eng_cfg = { .timeout_ms = 2000 };
    esp_err_t err = jpeg_new_encoder_engine(&eng_cfg, &enc);
    if (err != ESP_OK) {
        free(rgb888); free(jpeg_buf);
        ESP_LOGE(TAG, "new encoder: 0x%x", err);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // MJPEG headers
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" MJPEG_BOUNDARY);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection",    "close");

    ESP_LOGI(TAG, "stream connected  %dx%d  q=%d  fps=%d",
             ENC_W, ENC_H, MJPEG_QUALITY, MJPEG_FPS_MAX);


    char     hdr[160];
    int      frames    = 0;
    int      enc_fails = 0;

    jpeg_encode_cfg_t enc_cfg = {
        .src_type      = JPEG_ENCODE_IN_FORMAT_RGB888,
        .sub_sample    = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = MJPEG_QUALITY,
        .width         = ENC_W,
        .height        = ENC_H,
    };

    while (1) {
        TickType_t t0 = xTaskGetTickCount();

        size_t fb_bytes = 0;
        display_stub_lock();
        const uint16_t *fb = display_stub_get_fb(&fb_bytes);
        if (!fb) {
            display_stub_unlock();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        prv_rgb565_to_rgb888(fb, rgb888, ENC_W, ENC_H);
        display_stub_unlock();

        uint32_t bytes_out = 0;
        err = jpeg_encoder_process(enc, &enc_cfg,
                                   rgb888,   (uint32_t)rgb_alloc,
                                   jpeg_buf, (uint32_t)jpeg_alloc,
                                   &bytes_out);
        if (err != ESP_OK || bytes_out == 0) {
            ESP_LOGW(TAG, "encode fail #%d: 0x%x", ++enc_fails, err);
            if (enc_fails > 5) { ESP_LOGE(TAG, "giving up"); break; }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        enc_fails = 0;

        int hlen = snprintf(hdr, sizeof(hdr),
            "--" MJPEG_BOUNDARY "\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %" PRIu32 "\r\n\r\n",
            bytes_out);

        if (httpd_resp_send_chunk(req, hdr,            hlen)       != ESP_OK) break;
        if (httpd_resp_send_chunk(req, (char*)jpeg_buf, bytes_out) != ESP_OK) break;
        if (httpd_resp_send_chunk(req, "\r\n",          2)          != ESP_OK) break;

        frames++;
        if (frames % 30 == 0)
            ESP_LOGD(TAG, "frame %d  %" PRIu32 " B", frames, bytes_out);

        TickType_t elapsed = xTaskGetTickCount() - t0;
        TickType_t target  = pdMS_TO_TICKS(MJPEG_FRAME_MS);
        if (elapsed < target) vTaskDelay(target - elapsed);
    }

    jpeg_del_encoder_engine(enc);
    free(rgb888);
    free(jpeg_buf);

    ESP_LOGI(TAG, "stream closed  %d frames", frames);
    return ESP_OK;
}

// ── Dedicated stream server on port 81 ───────────────────────
// Runs independently from main httpd so MJPEG never blocks OTA/nav.

#include "esp_http_server.h"

static httpd_handle_t s_stream_server = NULL;

esp_err_t mjpeg_server_start(void)
{
    httpd_config_t cfg    = HTTPD_DEFAULT_CONFIG();
    cfg.server_port       = 81;
    cfg.ctrl_port         = 32769;  // must differ from main server (32768)
    cfg.max_uri_handlers  = 2;
    cfg.max_open_sockets  = 2;      // only need 1 stream + 1 spare
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 60;

    ESP_ERROR_CHECK(httpd_start(&s_stream_server, &cfg));

    httpd_uri_t stream = { .uri="/stream", .method=HTTP_GET,
                           .handler=mjpeg_stream_handler };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_stream_server, &stream));

    ESP_LOGI("mjpeg", "Stream server on port 81");
    return ESP_OK;
}
