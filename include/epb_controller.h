// =============================================================
//  epb_controller.h — Electronic Parking Brake Controller
//
//  Hardware: EPB controller with momentary button output and
//  green/red LED status inputs (direct GPIO, not CAN).
//
//  States read from LED inputs:
//    Green only  → RELEASED
//    Red only    → APPLIED
//    Both        → SERVICE MODE (10-second hold hazard)
//    Neither     → UNKNOWN (power off or wiring fault)
//
//  Button output: pull EPB_OUT_PIN low for ~200ms to "press".
//  Never hold low >9 seconds — that triggers 10-second service mode.
//
//  Safety constraints for ENGAGE (both must be satisfied):
//    - speed_kph < EPB_MAX_ENGAGE_KPH  (default 5.0)
//    - gear != GEAR_D                  (belt-and-suspenders vs stale CAN)
//
//  Release has no speed/gear constraints (always allow driving away).
//
//  Reboot safety: EPB_OUT_PIN driven HIGH in epb_init() before any
//  gear logic runs. GPIO defaults to high-impedance on reset so
//  there is no unsafe window at boot.
//
//  Pin defaults — override via -DEPB_*_PIN=N in platformio.ini.
// =============================================================

#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Pin defaults ──────────────────────────────────────────────
#ifndef EPB_OUT_PIN
  #define EPB_OUT_PIN    6    // output: normally HIGH, pulse LOW to press
#endif
#ifndef EPB_GREEN_PIN
  #define EPB_GREEN_PIN  2    // input + pullup, active-low: brake RELEASED
#endif
#ifndef EPB_RED_PIN
  #define EPB_RED_PIN    3    // input + pullup, active-low: brake APPLIED
#endif

// ── Timing ────────────────────────────────────────────────────
#ifndef EPB_PULSE_MS
  #define EPB_PULSE_MS  200   // button press duration in ms
#endif

// ── Safety threshold ──────────────────────────────────────────
#ifndef EPB_MAX_ENGAGE_KPH
  #define EPB_MAX_ENGAGE_KPH  5.0f
#endif

// ── Gear value for Drive (from can_parser.h) ──────────────────
#define EPB_GEAR_D  3

// ── EPB state ─────────────────────────────────────────────────
typedef enum {
    EPB_STATE_UNKNOWN  = 0,  // both inputs inactive (power off / fault)
    EPB_STATE_RELEASED = 1,  // green active only
    EPB_STATE_APPLIED  = 2,  // red active only
    EPB_STATE_SERVICE  = 3,  // both active simultaneously
} epb_state_t;

// ── Public API ────────────────────────────────────────────────

// Configure GPIOs and create one-shot pulse timer.
// Sets EPB_OUT_PIN OUTPUT HIGH immediately — safe to call early in app_main.
// Does NOT engage or release the brake.
esp_err_t epb_init(void);

// Read current EPB state from LED inputs. Thread-safe (GPIO reads are atomic).
epb_state_t epb_get_state(void);

// Request EPB engage (apply brake).
// Enforces safety constraints: speed_kph < EPB_MAX_ENGAGE_KPH AND gear != GEAR_D.
// Returns ESP_ERR_INVALID_STATE if constraints not met or pulse already in progress.
// Returns ESP_OK and fires one-shot 200ms pulse on success.
esp_err_t epb_engage(float speed_kph, uint8_t gear);

// Request EPB release (release brake). No speed/gear constraints.
// Returns ESP_ERR_INVALID_STATE if pulse already in progress.
// Returns ESP_OK and fires one-shot 200ms pulse on success.
esp_err_t epb_release(void);

// True if a button pulse is currently in progress.
bool epb_is_busy(void);

#ifdef __cplusplus
}
#endif
