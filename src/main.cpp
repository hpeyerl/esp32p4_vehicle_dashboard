// =============================================================
//  EV Dashboard — Main Application
//  Build system: PlatformIO + ESP-IDF framework
//  Target:  ESP32-P4 (ESP32-P4-Nano or DevKit)
//  Display: Waveshare 10.1" DSI-Touch-A  (1280×800, MIPI DSI)
//  CAN:     TWAI peripheral (ESP32-P4 built-in)
//  UI:      LVGL v9
//
//  Pin assignments are set via build_flags in platformio.ini:
//    -DTWAI_TX_PIN=5   -DTWAI_RX_PIN=4
//    -DTOUCH_SDA_PIN=8 -DTOUCH_SCL_PIN=9
//    -DTOUCH_INT_PIN=3 -DTOUCH_RST_PIN=2
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

static const char *TAG = "ev_dash";

// -------------------------------------------------------------
//  Global DashData instance (declared extern in can_parser.h)
// -------------------------------------------------------------
DashData g_dash = {};

// Mutex protecting g_dash between CAN task and UI task
static SemaphoreHandle_t g_dash_mutex = NULL;

// -------------------------------------------------------------
//  Pin definitions — injected from platformio.ini build_flags
//  Fallback defaults if not defined
// -------------------------------------------------------------
#ifndef TWAI_TX_PIN
  #define TWAI_TX_PIN  5
#endif
#ifndef TWAI_RX_PIN
  #define TWAI_RX_PIN  4
#endif
#ifndef TOUCH_SDA_PIN
  #define TOUCH_SDA_PIN  8
#endif
#ifndef TOUCH_SCL_PIN
  #define TOUCH_SCL_PIN  9
#endif
#ifndef TOUCH_INT_PIN
  #define TOUCH_INT_PIN  3
#endif
#ifndef TOUCH_RST_PIN
  #define TOUCH_RST_PIN  2
#endif
#ifndef LCD_H_RES
  #define LCD_H_RES  1280
#endif
#ifndef LCD_V_RES
  #define LCD_V_RES   800
#endif

// -------------------------------------------------------------
//  LVGL display buffers (allocated in PSRAM)
// -------------------------------------------------------------
static lv_display_t *g_disp = NULL;
static lv_color_t   *g_buf1 = NULL;
static lv_color_t   *g_buf2 = NULL;

// =============================================================
//  TWAI (CAN) — 500 kbps
// =============================================================
static esp_err_t twai_init(void)
{
    const twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)TWAI_TX_PIN,
        (gpio_num_t)TWAI_RX_PIN,
        TWAI_MODE_NORMAL
    );
    const twai_timing_config_t  t_cfg = TWAI_TIMING_CONFIG_500KBITS();
    const twai_filter_config_t  f_cfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_cfg, &t_cfg, &f_cfg));
    ESP_ERROR_CHECK(twai_start());

    ESP_LOGI(TAG, "TWAI started  TX=GPIO%d  RX=GPIO%d  500 kbps",
             TWAI_TX_PIN, TWAI_RX_PIN);
    return ESP_OK;
}

// =============================================================
//  CAN receive task — core 0, priority 10
// =============================================================
static void can_rx_task(void *arg)
{
    twai_message_t msg;

    while (1) {
        esp_err_t ret = twai_receive(&msg, pdMS_TO_TICKS(100));
        if (ret == ESP_OK) {
            uint8_t data[8] = {0};
            uint8_t dlc = msg.data_length_code;
            if (dlc > 8) dlc = 8;
            memcpy(data, msg.data, dlc);

            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

            xSemaphoreTake(g_dash_mutex, portMAX_DELAY);
            parse_can_frame(msg.identifier, data, now_ms);
            xSemaphoreGive(g_dash_mutex);

#ifdef DASHBOARD_DEBUG_CAN
            ESP_LOGD(TAG, "CAN RX  ID=0x%03lX  DLC=%u  [%02X %02X %02X %02X %02X %02X %02X %02X]",
                     (unsigned long)msg.identifier, dlc,
                     data[0], data[1], data[2], data[3],
                     data[4], data[5], data[6], data[7]);
#endif
        }
        /* On timeout: loop back — UI will detect stale signals */
    }
}

// =============================================================
//  LVGL flush callback
//  Replace the body with your Waveshare DSI panel draw call.
//
//  Typical esp_lcd integration:
//    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
//    esp_lcd_panel_draw_bitmap(panel,
//        area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
// =============================================================
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // TODO: add Waveshare DSI panel draw call here
    lv_display_flush_ready(disp);
}

// =============================================================
//  Display + LVGL init
//  lv_conf.h routes LV_TICK_CUSTOM to esp_timer_get_time() so
//  we do NOT need a separate tick timer here.
// =============================================================
static void display_init(void)
{
    lv_init();

    size_t buf_bytes = (size_t)LCD_H_RES * (LCD_V_RES / 10) * sizeof(lv_color_t);

    g_buf1 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_buf2 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!g_buf1 || !g_buf2) {
        ESP_LOGE(TAG, "LVGL draw buffer allocation failed! "
                      "Ensure PSRAM is enabled in sdkconfig.");
        abort();
    }

    g_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(g_disp, lvgl_flush_cb);
    lv_display_set_buffers(g_disp, g_buf1, g_buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "LVGL display created  %d×%d  buf=%zu bytes×2 (PSRAM)",
             LCD_H_RES, LCD_V_RES, buf_bytes);
}

// =============================================================
//  UI task — core 1, priority 5, 30 fps
// =============================================================
static void ui_task(void *arg)
{
    dashboard_ui_create(g_disp);

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(33); /* ~30 fps */

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

// =============================================================
//  app_main — PlatformIO ESP-IDF framework entry point
//  (identical to IDF; PlatformIO calls app_main() from its
//   generated main.cpp stub automatically)
// =============================================================
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "EV Dashboard booting  (PlatformIO / ESP-IDF)");
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    g_dash_mutex = xSemaphoreCreateMutex();
    if (!g_dash_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        abort();
    }

    display_init();
    twai_init();

    /* CAN RX — high priority on core 0 to never miss a frame */
    xTaskCreatePinnedToCore(can_rx_task, "can_rx", 4096,  NULL, 10, NULL, 0);

    /* UI / LVGL — normal priority on core 1 */
    xTaskCreatePinnedToCore(ui_task,     "ui",     12288, NULL,  5, NULL, 1);

    ESP_LOGI(TAG, "Tasks running.");
    /* app_main returns here; FreeRTOS scheduler continues */
}
