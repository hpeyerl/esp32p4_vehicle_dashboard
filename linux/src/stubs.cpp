// =============================================================
//  stubs.cpp — Linux implementations of the few external symbols
//  the portable dashboard UI references.
//
//  Compiled as C++ so each symbol gets the linkage its header
//  dictates: gear_shifter.h and wifi_manager.h are extern "C"
//  (→ C linkage), while can_parser.h is not (→ C++ linkage for
//  can_signal_stale, matching dashboard_ui.cpp).
//
//  On the ESP32 these are backed by WiFi and a CAN gear-shift TX
//  task. For the Pi prototype they're inert: connectivity reports
//  "not connected", a gear request is just remembered, and every
//  CAN signal is treated as live so the sim data isn't flagged stale.
// =============================================================
#include <stdbool.h>
#include <stdint.h>
#include "gear_shifter.h"   // extern "C"  (clean, no ESP deps)
#include "wifi_manager.h"   // extern "C"  (compat shim)
#include "can_parser.h"     // DashData, g_dash, can_signal_stale (no extern "C")

// Global dashboard-data instance (on ESP this lives in main.cpp). The UI's
// dash_get_*() helpers read it directly, so the sim loop writes into it.
DashData g_dash;

// ESP impl lives in can_parser.cpp. Sim build: treat every signal as live so
// the demo doesn't paint "stale" indicators over the synthesized data.
bool can_signal_stale(uint32_t last_ms, uint32_t now_ms, uint32_t timeout_ms)
{
    (void)last_ms; (void)now_ms; (void)timeout_ms;
    return false;
}

// --- wifi_manager ---
bool wifi_manager_is_connected(void) { return false; }

// --- gear_shifter (no CAN on Linux; just track the request) ---
static volatile int8_t s_gear = -1;   // 0=P 1=R 2=N 3=D, -1 = none
void   gear_shifter_start(void)          { /* no-op on Linux */ }
void   gear_shifter_request(int8_t gear) { s_gear = gear; }
int8_t gear_shifter_current(void)        { return s_gear; }
