// =============================================================
//  epb_controller.c — Electronic Parking Brake Controller
//
//  Pure IDF C — no Arduino dependencies.
//  See epb_controller.h for full documentation.
// =============================================================

#include "epb_controller.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "epb";

// ── State ─────────────────────────────────────────────────────
static esp_timer_handle_t s_pulse_timer = NULL;
static SemaphoreHandle_t  s_mutex       = NULL;
static volatile bool      s_busy        = false;

// ── Timer callback — ends the button pulse ────────────────────
static void prv_pulse_end(void *arg)
{
    gpio_set_level((gpio_num_t)EPB_OUT_PIN, 1);
    s_busy = false;
    ESP_LOGI(TAG, "pulse end — OUT_PIN HIGH");
}

// ── Internal: fire one pulse, caller holds mutex ──────────────
static esp_err_t prv_fire_pulse(void)
{
    if (s_busy) return ESP_ERR_INVALID_STATE;

    s_busy = true;
    gpio_set_level((gpio_num_t)EPB_OUT_PIN, 0);
    ESP_LOGI(TAG, "pulse start — OUT_PIN LOW for %d ms", EPB_PULSE_MS);

    esp_err_t err = esp_timer_start_once(s_pulse_timer,
                                         (uint64_t)EPB_PULSE_MS * 1000ULL);
    if (err != ESP_OK) {
        // Failed to arm timer — release GPIO immediately and clear flag
        gpio_set_level((gpio_num_t)EPB_OUT_PIN, 1);
        s_busy = false;
        ESP_LOGE(TAG, "timer start failed: %s", esp_err_to_name(err));
    }
    return err;
}

// ── Public API ────────────────────────────────────────────────

esp_err_t epb_init(void)
{
    // Drive output HIGH first — before any other setup — so the brake
    // is never accidentally triggered during GPIO configuration.
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << EPB_OUT_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));
    gpio_set_level((gpio_num_t)EPB_OUT_PIN, 1);
    ESP_LOGI(TAG, "OUT_PIN GPIO%d HIGH (safe)", EPB_OUT_PIN);

    // Configure LED status inputs with internal pullups
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << EPB_GREEN_PIN) | (1ULL << EPB_RED_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg));
    ESP_LOGI(TAG, "GREEN_PIN GPIO%d  RED_PIN GPIO%d configured (input + pullup)",
             EPB_GREEN_PIN, EPB_RED_PIN);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = prv_pulse_end,
        .arg      = NULL,
        .name     = "epb_pulse",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_pulse_timer));

    ESP_LOGI(TAG, "init complete — pulse=%d ms  max_engage=%.1f kph",
             EPB_PULSE_MS, (double)EPB_MAX_ENGAGE_KPH);
    return ESP_OK;
}

epb_state_t epb_get_state(void)
{
    // Active-low: LED on = pin LOW = level 0
    bool green = (gpio_get_level((gpio_num_t)EPB_GREEN_PIN) == 0);
    bool red   = (gpio_get_level((gpio_num_t)EPB_RED_PIN)   == 0);

    if (green && red)  return EPB_STATE_SERVICE;
    if (red)           return EPB_STATE_APPLIED;
    if (green)         return EPB_STATE_RELEASED;
    return EPB_STATE_UNKNOWN;
}

esp_err_t epb_engage(float speed_kph, uint8_t gear)
{
    // Safety: refuse if moving or in Drive
    if (speed_kph >= EPB_MAX_ENGAGE_KPH) {
        ESP_LOGW(TAG, "engage refused — speed %.1f kph >= %.1f kph limit",
                 (double)speed_kph, (double)EPB_MAX_ENGAGE_KPH);
        return ESP_ERR_INVALID_STATE;
    }
    if (gear == EPB_GEAR_D) {
        ESP_LOGW(TAG, "engage refused — gear is D");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = prv_fire_pulse();
    xSemaphoreGive(s_mutex);

    if (err == ESP_OK)
        ESP_LOGI(TAG, "engage requested (speed=%.1f kph, gear=%d)",
                 (double)speed_kph, gear);
    return err;
}

esp_err_t epb_release(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = prv_fire_pulse();
    xSemaphoreGive(s_mutex);

    if (err == ESP_OK)
        ESP_LOGI(TAG, "release requested");
    return err;
}

bool epb_is_busy(void)
{
    return s_busy;
}
