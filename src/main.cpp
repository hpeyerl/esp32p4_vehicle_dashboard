// =============================================================
//  main.cpp — EV Dashboard Application Entry Point
//
//  Display backend selected at compile time via DISPLAY_TARGET:
//    1 = M5Stack Tab5   (ST7123,  1280×720 landscape)
//    2 = Waveshare 12.3 (HX8399-C, 1920×720 landscape)
//  Set in platformio.ini per-env. See display_driver.h.
// =============================================================

#include <stdio.h>
#include <math.h>
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
#include "display_driver.h"   // ← unified API; never include a backend header directly
#include "ota_server.h"
#include "vss_sensor.h"
#include "sdo_manager.h"
#include "units.h"

#ifndef STRINGIFY
  #define STRINGIFY_(x) #x
  #define STRINGIFY(x)  STRINGIFY_(x)
#endif

static const char *TAG = "ev_dash";

#ifndef TWAI_TX_PIN
  #define TWAI_TX_PIN  53
#endif
#ifndef TWAI_RX_PIN
  #define TWAI_RX_PIN  54
#endif

static lv_display_t      *g_disp       = NULL;
static SemaphoreHandle_t  g_dash_mutex = NULL;

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

static void can_rx_task(void *arg)
{
    twai_message_t msg;
    while (1) {
        if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
            uint8_t data[8] = {};
            uint8_t dlc = msg.data_length_code < 8 ? msg.data_length_code : 8;
            memcpy(data, msg.data, dlc);
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            // Feed SDO responses to SDO manager before dashboard parser
            sdo_process_frame(&msg);
            xSemaphoreTake(g_dash_mutex, portMAX_DELAY);
            parse_can_frame(msg.identifier, data, now_ms);
            xSemaphoreGive(g_dash_mutex);
        }
    }
}

static void app_display_init(void)
{
    lv_init();
    ESP_ERROR_CHECK(display_init(&g_disp));

    size_t buf_bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    void *buf1 = heap_caps_aligned_alloc(128, buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_aligned_alloc(128, buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) { ESP_LOGE(TAG, "LVGL buf alloc failed"); abort(); }

    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(g_disp, buf1, buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);

    ESP_LOGI(TAG, "display_init complete  %d×%d  buf=%zu B ×2 (PSRAM)",
             LCD_H_RES, LCD_V_RES, buf_bytes);
}

static void ui_task(void *arg)
{
    dashboard_ui_create(g_disp);

    const TickType_t period = pdMS_TO_TICKS(66);
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t frame = 0;

    while (1) {
        DashData snap;
        xSemaphoreTake(g_dash_mutex, portMAX_DELAY);
        memcpy(&snap, &g_dash, sizeof(DashData));
        xSemaphoreGive(g_dash_mutex);

        // VSS speed — overrides CAN speed when sensor is active
        #if !SIM_DATA
        {
            float vss_mph = vss_get_mph();
            if (vss_mph > 0.0f) {
                xSemaphoreTake(g_dash_mutex, portMAX_DELAY);
                g_dash.speed = vss_mph;
                xSemaphoreGive(g_dash_mutex);
            }
        }
        #endif
#if SIM_DATA
        {
            static float t = 0.0f;
            t += 0.05f;
            snap.soc_pct         = 50.0f + 45.0f * sinf(t * 0.3f);
            snap.speed           = 60.0f + 50.0f * sinf(t * 0.7f);
            snap.power_kw        = 150.0f * sinf(t * 1.1f);
            snap.pack_volts      = 390.0f + 10.0f * sinf(t * 0.2f);
            snap.pack_amps       = snap.power_kw * 1000.0f / snap.pack_volts;
            snap.inverter_temp_c = 40.0f + 20.0f * sinf(t * 0.15f);
            snap.motor_temp_c    = 60.0f + 30.0f * sinf(t * 0.2f);
            snap.batt_temp_c     = 25.0f + 10.0f * sinf(t * 0.1f);
            snap.aux_volts       = 13.5f + 0.5f * sinf(t * 0.4f);
            snap.range_dist      = snap.soc_pct * 2.5f;
            snap.gear            = 3;
        }
#endif
        static DashData last_snap = {};
        if (memcmp(&snap, &last_snap, sizeof(DashData)) != 0) {
            dashboard_ui_update(&snap);
            last_snap = snap;
            lv_refr_now(g_disp);
        } else {
            lv_timer_handler();
        }

        ESP_LOGD(TAG, "frame %lu", (unsigned long)frame++);
        vTaskDelayUntil(&last_wake, period);
    }
}


static void prv_bg_init_task(void *arg)
{
    // Runs after display and CAN are up — WiFi/OTA/SDO can be slow
    ota_server_start();
    sdo_manager_init(NULL, NULL);
    ota_server_mark_valid();
    ESP_LOGI("bg_init", "background init complete");
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
#if DISPLAY_TARGET == DISPLAY_TARGET_TAB5
    const char *disp_name = "Tab5/ST7123 " STRINGIFY(LCD_H_RES) "×" STRINGIFY(LCD_V_RES);
#elif DISPLAY_TARGET == DISPLAY_TARGET_WAVESHARE
    const char *disp_name = "Waveshare12.3/HX8399-C " STRINGIFY(LCD_H_RES) "×" STRINGIFY(LCD_V_RES);
#else
    const char *disp_name = "stub " STRINGIFY(LCD_H_RES) "×" STRINGIFY(LCD_V_RES);
#endif

    ESP_LOGI(TAG, "EV Dashboard  IDF %s  display: %s  units: %s",
             esp_get_idf_version(), disp_name, UNITS_SPEED_LABEL);

    g_dash_mutex = xSemaphoreCreateMutex();
    if (!g_dash_mutex) { ESP_LOGE(TAG, "mutex create failed"); abort(); }

    // ESP32-P4-Nano: initialize SDIO transport to C6 WiFi coprocessor
    // Must be called before any WiFi API (esp_wifi_init inside ota_server_start)

    app_display_init();
    vss_sensor_init();
    twai_init();

    xTaskCreatePinnedToCore(can_rx_task, "can_rx", 4096,  NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(ui_task,     "ui",     12288, NULL,  5, NULL, 1);

    // Start WiFi/OTA/SDO in background — display and CAN are already running
    xTaskCreate(prv_bg_init_task, "bg_init", 8192, NULL, 3, NULL);
    ESP_LOGI(TAG, "tasks running");
}
