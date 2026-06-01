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
    float   odo_total_miles;
    float   trip_miles;

    // Gear:  0=P  1=R  2=N  3=D
    uint8_t gear;          // requested gear from shifter (0x312)

    // Cruise control
    uint8_t cruise_state;  // CRUISE_CC_* bitmask from cruisestt
    float   cruise_kph;    // target speed in kph (converted from cruisespeed RPM)
    uint32_t last_ms_cruise;

    // Zombieverter confirmed direction: 1=Forward 0=Neutral -1=Reverse
    // Populated when Zombieverter dir CAN mapping is configured.
    // TODO: add oic mapping for Zombieverter dir signal.
    int8_t  dir_confirmed; // INT8_MIN = unknown/not yet received

    // Frame freshness — ms timestamp of last update per CAN ID
    uint32_t last_ms_0x125;
    uint32_t last_ms_0x312;
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
