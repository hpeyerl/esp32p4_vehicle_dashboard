// =============================================================
//  vss_sensor.c — Vehicle Speed Sensor implementation
//
//  Pure IDF C — no Arduino dependencies.
//  See vss_sensor.h for full documentation.
// =============================================================

#include "vss_sensor.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>
#include <string.h>

static const char *TAG     = "vss";
static const char *NVS_NS  = "vss_cal";   // NVS namespace
static const char *NVS_KEY_TIRE  = "tire_circ";
static const char *NVS_KEY_DIFF  = "diff_ratio";
static const char *NVS_KEY_PPR   = "pulses_rev";

static const char *NVS_ODO_NS    = "odo";
static const char *NVS_KEY_TOTAL = "total_10th";  // uint32, units of 0.1 mile

// ── Calibration (runtime, NVS-backed) ────────────────────────────────────────
typedef struct {
    float tire_circ_inches;
    float diff_ratio;
    int   pulses_per_rev;
    // Derived — recomputed whenever cal changes
    float pulses_per_mile;
    float usec_per_pulse_at_1mph;
} vss_cal_t;

static vss_cal_t s_cal;

static void prv_cal_derive(vss_cal_t *c)
{
    c->pulses_per_mile        = (63360.0f / c->tire_circ_inches)
                                * c->diff_ratio
                                * (float)c->pulses_per_rev;
    c->usec_per_pulse_at_1mph = 3600000000.0f / c->pulses_per_mile;
}

// ── Odometer ──────────────────────────────────────────────────────────────────
#define ODO_NVS_WRITE_INTERVAL_MILES  0.5f

static volatile uint32_t s_total_pulses    = 0;  // incremented in ISR
static uint32_t          s_last_odo_pulses = 0;
static float             s_total_miles     = 0.0f;
static float             s_trip_miles      = 0.0f;
static float             s_odo_nvs_saved   = 0.0f;

// ── ISR timestamp ring buffer ─────────────────────────────────────────────────
#define TS_BUF_SIZE  16u
#define TS_BUF_MASK  (TS_BUF_SIZE - 1u)

static volatile uint64_t s_ts_buf[TS_BUF_SIZE];
static volatile uint32_t s_ts_write = 0;
static volatile uint32_t s_ts_read  = 0;

// ── Speed output — single-slot mailbox ───────────────────────────────────────
static QueueHandle_t s_speed_q = NULL;

// ── ISR ───────────────────────────────────────────────────────────────────────
static void IRAM_ATTR prv_vss_isr(void *arg)
{
    s_total_pulses++;
    uint64_t t    = (uint64_t)esp_timer_get_time();
    uint32_t next = (s_ts_write + 1) & TS_BUF_MASK;
    if (next != s_ts_read) {          // drop if full
        s_ts_buf[s_ts_write] = t;
        s_ts_write = next;
    }
}

// ── Signal processing ─────────────────────────────────────────────────────────
static uint64_t s_periods[VSS_PERIOD_AVG_DEPTH];
static uint32_t s_period_idx   = 0;
static uint32_t s_period_count = 0;
static uint64_t s_last_ts      = 0;
static float    s_ema           = 0.0f;
static bool     s_ema_init      = false;

static void prv_reset_state(void)
{
    s_ema          = 0.0f;
    s_ema_init     = false;
    s_last_ts      = 0;
    s_period_count = 0;
    s_period_idx   = 0;
    if (s_speed_q) {
        float zero = 0.0f;
        xQueueOverwrite(s_speed_q, &zero);
    }
}

// Returns speed in MPH, or -1.0 to discard
static float prv_process_ts(uint64_t ts)
{
    if (s_last_ts == 0) {
        s_last_ts = ts;
        return -1.0f;
    }

    uint64_t period_us = ts - s_last_ts;
    s_last_ts = ts;

    float instant = s_cal.usec_per_pulse_at_1mph / (float)period_us;
    if (instant > VSS_MAX_PLAUSIBLE_MPH || instant < 0.1f)
        return -1.0f;   // glitch, discard

    s_periods[s_period_idx] = period_us;
    s_period_idx = (s_period_idx + 1) % VSS_PERIOD_AVG_DEPTH;
    if (s_period_count < VSS_PERIOD_AVG_DEPTH) s_period_count++;

    // Mean of periods → correct average speed
    uint64_t sum = 0;
    for (uint32_t i = 0; i < s_period_count; i++) sum += s_periods[i];
    float avg_period = (float)(sum / s_period_count);

    return s_cal.usec_per_pulse_at_1mph / avg_period;
}

static float prv_ema_update(float sample)
{
    if (!s_ema_init) {
        s_ema      = sample;
        s_ema_init = true;
        return s_ema;
    }
    float err   = sample - s_ema;
    float alpha = (fabsf(err) > VSS_EMA_FAST_THRESHOLD_MPH)
                  ? VSS_EMA_ALPHA_FAST : VSS_EMA_ALPHA_SLOW;
    s_ema += alpha * err;
    return s_ema;
}

static void prv_drain_buffer(void)
{
    while (s_ts_read != s_ts_write) {
        uint64_t ts = s_ts_buf[s_ts_read];
        s_ts_read = (s_ts_read + 1) & TS_BUF_MASK;

        float spd = prv_process_ts(ts);
        if (spd > 0.0f) {
            float ema = prv_ema_update(spd);
            if (s_speed_q)
                xQueueOverwrite(s_speed_q, &ema);
        }
    }
}

// ── Odometer NVS + accumulation ───────────────────────────────────────────────
static void prv_odo_nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_ODO_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint32_t raw = 0;
    if (nvs_get_u32(h, NVS_KEY_TOTAL, &raw) == ESP_OK)
        s_total_miles = (float)raw / 10.0f;
    nvs_close(h);
    s_odo_nvs_saved = s_total_miles;
    ESP_LOGI(TAG, "ODO: loaded %.1f miles", s_total_miles);
}

static void prv_odo_nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_ODO_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, NVS_KEY_TOTAL, (uint32_t)(s_total_miles * 10.0f));
    nvs_commit(h);
    nvs_close(h);
    s_odo_nvs_saved = s_total_miles;
}

static void prv_odo_update(void)
{
    uint32_t pulses = s_total_pulses;   // atomic 32-bit read
    if (pulses == s_last_odo_pulses || s_cal.pulses_per_mile <= 0.0f) return;

    float delta = (float)(pulses - s_last_odo_pulses) / s_cal.pulses_per_mile;
    s_last_odo_pulses = pulses;
    s_total_miles += delta;
    s_trip_miles  += delta;

    if ((s_total_miles - s_odo_nvs_saved) >= ODO_NVS_WRITE_INTERVAL_MILES)
        prv_odo_nvs_save();
}

// ── FreeRTOS task ─────────────────────────────────────────────────────────────
static void prv_vss_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        prv_drain_buffer();
        prv_odo_update();

        if (s_last_ts > 0) {
            uint64_t now_us = (uint64_t)esp_timer_get_time();
            if ((now_us - s_last_ts) > VSS_STALE_TIMEOUT_US)
                prv_reset_state();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}

// ── NVS load/save ─────────────────────────────────────────────────────────────
static void prv_nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "NVS: no saved calibration, using defaults");
        return;
    }

    uint32_t raw;
    float    fval;

    // Store floats as raw uint32 to avoid NVS type issues
    if (nvs_get_u32(h, NVS_KEY_TIRE, &raw) == ESP_OK) {
        memcpy(&fval, &raw, 4);
        if (fval > 10.0f && fval < 500.0f) s_cal.tire_circ_inches = fval;
    }
    if (nvs_get_u32(h, NVS_KEY_DIFF, &raw) == ESP_OK) {
        memcpy(&fval, &raw, 4);
        if (fval > 0.5f && fval < 20.0f) s_cal.diff_ratio = fval;
    }
    int32_t ival;
    if (nvs_get_i32(h, NVS_KEY_PPR, &ival) == ESP_OK) {
        if (ival >= 1 && ival <= 64) s_cal.pulses_per_rev = (int)ival;
    }

    nvs_close(h);
    ESP_LOGI(TAG, "NVS: loaded cal — tire=%.2f\" diff=%.3f ppr=%d",
             s_cal.tire_circ_inches, s_cal.diff_ratio, s_cal.pulses_per_rev);
}

static esp_err_t prv_nvs_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    uint32_t raw;
    memcpy(&raw, &s_cal.tire_circ_inches, 4);
    nvs_set_u32(h, NVS_KEY_TIRE, raw);
    memcpy(&raw, &s_cal.diff_ratio, 4);
    nvs_set_u32(h, NVS_KEY_DIFF, raw);
    nvs_set_i32(h, NVS_KEY_PPR, (int32_t)s_cal.pulses_per_rev);

    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "NVS: saved cal — tire=%.2f\" diff=%.3f ppr=%d",
             s_cal.tire_circ_inches, s_cal.diff_ratio, s_cal.pulses_per_rev);
    return err;
}

// ── Public API ────────────────────────────────────────────────────────────────
esp_err_t vss_sensor_init(void)
{
    // Load calibration defaults then overlay from NVS
    s_cal.tire_circ_inches = VSS_DEFAULT_TIRE_CIRC_INCHES;
    s_cal.diff_ratio       = VSS_DEFAULT_DIFF_RATIO;
    s_cal.pulses_per_rev   = VSS_DEFAULT_PULSES_PER_REV;
    prv_nvs_load();
    prv_cal_derive(&s_cal);
    prv_odo_nvs_load();

    ESP_LOGI(TAG, "pulses/mile=%.1f  us/pulse@1mph=%.1f",
             s_cal.pulses_per_mile, s_cal.usec_per_pulse_at_1mph);

    // Single-slot speed mailbox
    s_speed_q = xQueueCreate(1, sizeof(float));
    float zero = 0.0f;
    xQueueOverwrite(s_speed_q, &zero);

    // GPIO — input with internal pullup, interrupt on falling edge
    // (reed switch closes to GND; pin is HIGH at rest, pulses LOW)
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << VSS_PULSE_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "ISR service failed");
    ESP_RETURN_ON_ERROR(
        gpio_isr_handler_add((gpio_num_t)VSS_PULSE_PIN, prv_vss_isr, NULL),
        TAG, "ISR handler add failed");

    ESP_LOGI(TAG, "GPIO%d configured — NEGEDGE interrupt, pullup enabled",
             VSS_PULSE_PIN);

    xTaskCreatePinnedToCore(prv_vss_task, "vss", VSS_TASK_STACK,
                            NULL, VSS_TASK_PRIORITY, NULL, VSS_TASK_CORE);
    ESP_LOGI(TAG, "VSS task started on core %d", VSS_TASK_CORE);
    return ESP_OK;
}

float vss_get_mph(void)
{
    float mph = 0.0f;
    if (s_speed_q) xQueuePeek(s_speed_q, &mph, 0);
    return mph;
}

void vss_get_cal(float *tire_circ_inches, float *diff_ratio, int *pulses_per_rev)
{
    if (tire_circ_inches) *tire_circ_inches = s_cal.tire_circ_inches;
    if (diff_ratio)       *diff_ratio       = s_cal.diff_ratio;
    if (pulses_per_rev)   *pulses_per_rev   = s_cal.pulses_per_rev;
}

esp_err_t vss_set_cal(float tire_circ_inches, float diff_ratio, int pulses_per_rev)
{
    if (tire_circ_inches < 10.0f || tire_circ_inches > 500.0f) return ESP_ERR_INVALID_ARG;
    if (diff_ratio < 0.5f        || diff_ratio > 20.0f)        return ESP_ERR_INVALID_ARG;
    if (pulses_per_rev < 1       || pulses_per_rev > 64)       return ESP_ERR_INVALID_ARG;

    s_cal.tire_circ_inches = tire_circ_inches;
    s_cal.diff_ratio       = diff_ratio;
    s_cal.pulses_per_rev   = pulses_per_rev;
    prv_cal_derive(&s_cal);
    prv_reset_state();   // flush stale speed readings

    return prv_nvs_save();
}

float vss_get_pulses_per_mile(void)        { return s_cal.pulses_per_mile; }
float vss_get_usec_per_pulse_at_1mph(void) { return s_cal.usec_per_pulse_at_1mph; }

float vss_get_total_miles(void) { return s_total_miles; }
float vss_get_trip_miles(void)  { return s_trip_miles;  }
void  vss_reset_trip(void)      { s_trip_miles = 0.0f;  }
