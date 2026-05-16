// =============================================================
//  can_parser.h — CAN Frame Parser Public API
//
//  Call parse_can_frame() from your TWAI receive task.
//  Read g_dash (protected by a mutex) from your UI task.
// =============================================================

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "can_signals.h"

// ── Live dashboard snapshot ───────────────────────────────────────────────
typedef struct {
    // Drivetrain
    float   speed;          // vehicle speed, native CAN units (MPH from BMS)
    float   power_kw;       // derived: pack_volts × pack_amps / 1000

    // Battery / BMS (0x355, 0x356)
    float   soc_pct;        // 0–100 %
    float   pack_volts;     // V
    float   pack_amps;      // A  (+ = discharge / motoring, – = regen)
    float   batt_temp_c;    // °C

    // Thermal (0x125, 0x126)
    float   motor_temp_c;
    float   inverter_temp_c;

    // 12V system (0x210)
    float   aux_volts;

    // Derived
    float   range_dist;     // estimated range, native miles (convert at display)

    // Gear:  0=P  1=R  2=N  3=D  4=B
    uint8_t gear;

    // Frame freshness — ms timestamp of last update per CAN ID
    uint32_t last_ms_0x125;
    uint32_t last_ms_0x126;
    uint32_t last_ms_0x257;
    uint32_t last_ms_0x355;
    uint32_t last_ms_0x356;
    uint32_t last_ms_0x210;
} DashData;

// Global instance — write locked by g_dash_mutex in main.cpp
extern DashData g_dash;

// Parse one CAN frame and update g_dash.
// id      — 11-bit CAN identifier
// data    — 8-byte frame payload
// now_ms  — current time in milliseconds (for staleness tracking)
void parse_can_frame(uint32_t id, const uint8_t *data, uint32_t now_ms);

// Returns true if the given ID has not been seen within timeout_ms.
bool can_signal_stale(uint32_t last_ms, uint32_t now_ms, uint32_t timeout_ms);
