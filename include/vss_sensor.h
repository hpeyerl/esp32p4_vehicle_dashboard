// =============================================================
//  vss_sensor.h — Vehicle Speed Sensor, GPIO pulse counting
//
//  Reed switch VSS: switch between GPIO pin and GND.
//  GPIO configured INPUT + PULLUP. Interrupt on NEGEDGE
//  (switch closes to GND on each magnet pass).
//
//  Speed parameters stored in NVS, editable via web UI at
//  http://ev-dashboard.local/vss
//
//  Output: calls vss_get_mph() from ui_task to read speed.
//  Thread-safe — uses single-slot FreeRTOS queue internally.
//
//  Pin default: override via -DVSS_PULSE_PIN=N in platformio.ini
// =============================================================

#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Default pin — override in platformio.ini ──────────────────
#ifndef VSS_PULSE_PIN
  #define VSS_PULSE_PIN  10
#endif

// ── Stale timeout — declare speed=0 if no pulse for this long ─
#ifndef VSS_STALE_TIMEOUT_US
  #define VSS_STALE_TIMEOUT_US  2000000ULL   // 2 seconds
#endif

// ── Implausible speed filter ──────────────────────────────────
#ifndef VSS_MAX_PLAUSIBLE_MPH
  #define VSS_MAX_PLAUSIBLE_MPH  160.0f
#endif

// ── Period averager depth ─────────────────────────────────────
#ifndef VSS_PERIOD_AVG_DEPTH
  #define VSS_PERIOD_AVG_DEPTH  4
#endif

// ── Dual-rate EMA alphas ──────────────────────────────────────
#ifndef VSS_EMA_ALPHA_SLOW
  #define VSS_EMA_ALPHA_SLOW  0.08f
#endif
#ifndef VSS_EMA_ALPHA_FAST
  #define VSS_EMA_ALPHA_FAST  0.40f
#endif
#ifndef VSS_EMA_FAST_THRESHOLD_MPH
  #define VSS_EMA_FAST_THRESHOLD_MPH  2.0f
#endif

// ── FreeRTOS task config ──────────────────────────────────────
#ifndef VSS_TASK_CORE
  #define VSS_TASK_CORE      0     // core 0; UI on core 1
#endif
#ifndef VSS_TASK_PRIORITY
  #define VSS_TASK_PRIORITY  8
#endif
#ifndef VSS_TASK_STACK
  #define VSS_TASK_STACK     3072
#endif

// ── VSS calibration parameters (NVS-backed) ──────────────────
// Defaults used on first boot or if NVS is blank.
// Editable at runtime via http://ev-dashboard.local/vss
#ifndef VSS_DEFAULT_TIRE_CIRC_INCHES
  #define VSS_DEFAULT_TIRE_CIRC_INCHES  103.67f   // pi * 33" tire
#endif
#ifndef VSS_DEFAULT_DIFF_RATIO
  #define VSS_DEFAULT_DIFF_RATIO        4.10f
#endif
#ifndef VSS_DEFAULT_PULSES_PER_REV
  #define VSS_DEFAULT_PULSES_PER_REV    4
#endif

// ─────────────────────────────────────────────────────────────

// Initialise GPIO, load NVS calibration, spawn VSS task.
// Call once from app_main after NVS is initialised.
esp_err_t vss_sensor_init(void);

// Returns current filtered speed in MPH. Thread-safe.
// Returns 0.0 if sensor not initialised or speed is stale.
float vss_get_mph(void);

// Returns current calibration values (for web UI display).
void vss_get_cal(float *tire_circ_inches,
                 float *diff_ratio,
                 int   *pulses_per_rev);

// Update calibration and persist to NVS.
// Recalculates derived constants immediately.
esp_err_t vss_set_cal(float tire_circ_inches,
                      float diff_ratio,
                      int   pulses_per_rev);

// Computed from current calibration — useful for diagnostics.
float vss_get_pulses_per_mile(void);
float vss_get_usec_per_pulse_at_1mph(void);

#ifdef __cplusplus
}
#endif
