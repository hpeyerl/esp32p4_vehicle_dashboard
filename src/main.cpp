// =============================================================
//  main.cpp — EV Dashboard Application Entry Point
//
//  Target:  M5Stack Tab5 (ESP32-P4, ECO2/v1.3 silicon)
//  Display: ST7123 5" MIPI-DSI 720×1280 portrait
//           (LVGL landscape 1280×720, SW-rotated in flush cb)
//  CAN:     TWAI 500 kbps via IS3050G on GPIO_EXT (TX=53, RX=54)
//  UI:      LVGL v9
//  IDF:     5.4.2 (pioarduino platform)
// =============================================================

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/twai.h"
#include "driver/gpio.h"
#include "lvgl.h"

#include "can_signals.h"
#include "can_parser.h"
#include "dashboard_ui.h"
#include "tab5_display.h"

static const char *TAG = "ev_dash";

// ── TWAI pin defaults (override via platformio.ini build_flags) ───────────
#ifndef TWAI_TX_PIN
  #define TWAI_TX_PIN  53
#endif
#ifndef TWAI_RX_PIN
  #define TWAI_RX_PIN  54
#endif

// ── Globals ───────────────────────────────────────────────────────────────
static lv_display_t      *g_disp       = NULL;
static SemaphoreHandle_t  g_dash_mutex = NULL;

// ── TWAI (CAN) init ───────────────────────────────────────────────────────
static esp_err_t twai_init(void)
{
    const twai_general_config_t gcfg = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)TWAI_TX_PIN, (gpio_num_t)TWAI_RX_PIN, TWAI_MODE_NORMAL);
    const twai_timing_config_t  tcfg = TWAI_TIMING_CONFIG_500KBITS();
    const twai_filter_config_t  fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&gcfg, &tcfg, &fcfg));
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "TWAI started  TX=GPIO%d  RX=GPIO%d  500 kbps",
             TWAI_TX_PIN, TWAI_RX_PIN);
    return ESP_OK;
}

// ── CAN receive task (core 0, priority 10) ────────────────────────────────
static void can_rx_task(void *arg)
{
    twai_message_t msg;
    while (1) {
        if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
            uint8_t data[8] = {};
            uint8_t dlc = msg.data_length_code < 8 ? msg.data_length_code : 8;
            memcpy(data, msg.data, dlc);
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            xSemaphoreTake(g_dash_mutex, portMAX_DELAY);
            parse_can_frame(msg.identifier, data, now_ms);
            xSemaphoreGive(g_dash_mutex);
        }
    }
}

// ── Display + LVGL init ───────────────────────────────────────────────────
static void display_init(void)
{
    lv_init();

    ESP_ERROR_CHECK(tab5_display_init(&g_disp));

    // LVGL renders landscape 1280×720 RGB565.
    // The flush callback SW-rotates 90° CCW into the DPI panel's
    // double framebuffers (720×1280 portrait, internal SRAM).
    size_t buf_bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) { ESP_LOGE(TAG, "LVGL buf alloc failed"); abort(); }

    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(g_disp, buf1, buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);

    ESP_LOGI(TAG, "display_init complete  %d×%d  buf=%zu B ×2 (PSRAM)",
             LCD_H_RES, LCD_V_RES, buf_bytes);
}

// ── UI task (core 1, priority 5) ─────────────────────────────────────────
static void ui_task(void *arg)
{
    dashboard_ui_create(g_disp);

    const TickType_t period = pdMS_TO_TICKS(66);  // ~15 fps
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        DashData snap;
        xSemaphoreTake(g_dash_mutex, portMAX_DELAY);
        memcpy(&snap, &g_dash, sizeof(DashData));
        xSemaphoreGive(g_dash_mutex);

        dashboard_ui_update(&snap);
        lv_timer_handler();

        vTaskDelayUntil(&last_wake, period);
    }
}

// ── Entry point ───────────────────────────────────────────────────────────
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "EV Dashboard — M5Stack Tab5");
    ESP_LOGI(TAG, "IDF %s  |  display: %s  |  units: %s",
             esp_get_idf_version(),
             TAB5_DISPLAY_ILI9881C ? "ILI9881C" : "ST7123",
             UNITS_SPEED_LABEL);

    g_dash_mutex = xSemaphoreCreateMutex();
    if (!g_dash_mutex) { ESP_LOGE(TAG, "mutex create failed"); abort(); }

    display_init();
    twai_init();

    xTaskCreatePinnedToCore(can_rx_task, "can_rx", 4096,  NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(ui_task,     "ui",     12288, NULL,  5, NULL, 1);

    ESP_LOGI(TAG, "tasks running");
}
