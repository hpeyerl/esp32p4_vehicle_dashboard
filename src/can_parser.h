#pragma once
// =============================================================
//  EV Dashboard — CAN Frame Parser
//  Intel (Little Endian) signal extraction
//  ESP32-P4 / ESP-IDF  (also compiles under Arduino framework)
// =============================================================

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "can_signals.h"

// -------------------------------------------------------------
//  Live dashboard data — filled by parse_can_frame()
//  Read these from your LVGL update task.
// -------------------------------------------------------------
typedef struct {
    // --- Drivetrain ---
    float   speed;              // 0x257 — native units from CAN (MPH)
    float   power_kw;           // derived: pack_volts * pack_amps / 1000

    // --- Battery / BMS  (0x355, 0x356) ---
    float   soc_pct;            // 0x355
    float   pack_volts;         // 0x356 bits 0-15
    float   pack_amps;          // 0x356 bits 16-31  (+ = discharge, - = regen)
    float   batt_temp_c;        // 0x356 bits 32-47

    // --- Thermal (0x125, 0x126) ---
    float   motor_temp_c;       // 0x125
    float   inverter_temp_c;    // 0x126

    // --- 12V system (0x210) ---
    float   aux_volts;          // 0x210 bits 32-47

    // --- Derived ---
    float   range_dist;          // native units (mi), convert at display

    // --- Gear (placeholder — fill when CAN ID is known) ---
    //  0=P  1=R  2=N  3=D  4=B
    uint8_t gear;

    // --- Frame freshness (ms since last update per ID) ---
    uint32_t last_ms_0x125;
    uint32_t last_ms_0x126;
    uint32_t last_ms_0x257;
    uint32_t last_ms_0x355;
    uint32_t last_ms_0x356;
    uint32_t last_ms_0x210;

} DashData;

// Declare the global instance (defined in can_parser.c / main .cpp)
extern DashData g_dash;

// =============================================================
//  Intel (LE) signal extraction
//
//  The CAN frame is treated as a flat 64-bit little-endian word.
//  start_bit is the LSB position counted from bit 0 of byte 0.
//
//  Example: start=16, len=16 → bytes[2] (LSB) and bytes[3] (MSB)
// =============================================================

static inline uint64_t can_extract_le(const uint8_t *data, uint8_t start_bit, uint8_t len)
{
    uint64_t raw = 0;
    // Copy frame bytes into a 64-bit word (LE)
    uint64_t frame = 0;
    memcpy(&frame, data, 8);   // safe: CAN frames are always 8 bytes here

    uint64_t mask = (len == 64) ? UINT64_MAX : ((1ULL << len) - 1ULL);
    raw = (frame >> start_bit) & mask;
    return raw;
}

// Sign-extend a value of bit_count width
static inline int64_t sign_extend(uint64_t val, uint8_t bits)
{
    uint64_t sign_bit = 1ULL << (bits - 1);
    if (val & sign_bit)
        return (int64_t)(val | (~0ULL << bits));
    return (int64_t)val;
}

// Extract physical float from frame
static inline float can_signal(const uint8_t *data,
                                uint8_t start_bit, uint8_t len,
                                float scale, float offset,
                                bool is_signed)
{
    uint64_t raw = can_extract_le(data, start_bit, len);
    float phys;
    if (is_signed)
        phys = (float)sign_extend(raw, len) * scale + offset;
    else
        phys = (float)raw * scale + offset;
    return phys;
}

// =============================================================
//  Main parser — call from your CAN RX ISR or task
//
//  Usage (ESP-IDF TWAI):
//    twai_message_t msg;
//    twai_receive(&msg, pdMS_TO_TICKS(10));
//    parse_can_frame(msg.identifier, msg.data, esp_timer_get_time()/1000);
//
//  Usage (Arduino-ESP32 CAN):
//    if (CAN.parsePacket()) {
//      uint8_t buf[8] = {0};
//      CAN.readBytes(buf, CAN.packetDlc());
//      parse_can_frame(CAN.packetId(), buf, millis());
//    }
// =============================================================
static inline void parse_can_frame(uint32_t id, const uint8_t *data, uint32_t now_ms)
{
    switch (id)
    {
    // ---------------------------------------------------------
    //  0x125 — Motor Temperature
    // ---------------------------------------------------------
    case CAN_ID_MOTOR_TEMP:
        g_dash.motor_temp_c = can_signal(data,
            SIG_MOTOR_TEMP_START, SIG_MOTOR_TEMP_LEN,
            SIG_MOTOR_TEMP_SCALE, SIG_MOTOR_TEMP_OFFSET,
            SIG_MOTOR_TEMP_SIGNED);
        g_dash.last_ms_0x125 = now_ms;
        break;

    // ---------------------------------------------------------
    //  0x126 — Inverter Temperature
    // ---------------------------------------------------------
    case CAN_ID_INVERTER_TEMP:
        g_dash.inverter_temp_c = can_signal(data,
            SIG_INV_TEMP_START, SIG_INV_TEMP_LEN,
            SIG_INV_TEMP_SCALE, SIG_INV_TEMP_OFFSET,
            SIG_INV_TEMP_SIGNED);
        g_dash.last_ms_0x126 = now_ms;
        break;

    // ---------------------------------------------------------
    //  0x257 — Vehicle Speed
    // ---------------------------------------------------------
    case CAN_ID_SPEED:
        g_dash.speed = can_signal(data,
            SIG_SPEED_START, SIG_SPEED_LEN,
            SIG_SPEED_SCALE, SIG_SPEED_OFFSET,
            SIG_SPEED_SIGNED);
        g_dash.last_ms_0x257 = now_ms;
        break;

    // ---------------------------------------------------------
    //  0x355 — State of Charge
    // ---------------------------------------------------------
    case CAN_ID_SOC:
        g_dash.soc_pct = can_signal(data,
            SIG_SOC_START, SIG_SOC_LEN,
            SIG_SOC_SCALE, SIG_SOC_OFFSET,
            SIG_SOC_SIGNED);
        // Clamp to 0-100
        if (g_dash.soc_pct > 100.0f) g_dash.soc_pct = 100.0f;
        if (g_dash.soc_pct <   0.0f) g_dash.soc_pct =   0.0f;
        // Derived range
        g_dash.range_dist = g_dash.soc_pct * RANGE_FULL_SOC_MILES / 100.0f;  // in native miles
        g_dash.last_ms_0x355 = now_ms;
        break;

    // ---------------------------------------------------------
    //  0x356 — Pack Voltage + Pack Current + Battery Temp
    //          Three signals packed in one 8-byte frame
    // ---------------------------------------------------------
    case CAN_ID_BMS_MAIN:
        g_dash.pack_volts = can_signal(data,
            SIG_PACK_VOLTS_START, SIG_PACK_VOLTS_LEN,
            SIG_PACK_VOLTS_SCALE, SIG_PACK_VOLTS_OFFSET,
            SIG_PACK_VOLTS_SIGNED);

        g_dash.pack_amps = can_signal(data,
            SIG_PACK_AMPS_START, SIG_PACK_AMPS_LEN,
            SIG_PACK_AMPS_SCALE, SIG_PACK_AMPS_OFFSET,
            SIG_PACK_AMPS_SIGNED);

        g_dash.batt_temp_c = can_signal(data,
            SIG_BATT_TEMP_START, SIG_BATT_TEMP_LEN,
            SIG_BATT_TEMP_SCALE, SIG_BATT_TEMP_OFFSET,
            SIG_BATT_TEMP_SIGNED);

        // Derived power (kW)
        // pack_amps sign convention: positive = discharge (motoring), negative = regen
        // If your BMS uses the opposite convention, negate pack_amps here.
        g_dash.power_kw = (g_dash.pack_volts * g_dash.pack_amps) / 1000.0f;

        g_dash.last_ms_0x356 = now_ms;
        break;

    // ---------------------------------------------------------
    //  0x210 — 12V Auxiliary Battery Voltage
    // ---------------------------------------------------------
    case CAN_ID_AUX_BATT:
        g_dash.aux_volts = can_signal(data,
            SIG_AUX_VOLTS_START, SIG_AUX_VOLTS_LEN,
            SIG_AUX_VOLTS_SCALE, SIG_AUX_VOLTS_OFFSET,
            SIG_AUX_VOLTS_SIGNED);
        g_dash.last_ms_0x210 = now_ms;
        break;

    // ---------------------------------------------------------
    //  Gear / PRNDL — ID TBD, stubbed here
    //  Replace 0xDEAD with real ID when known.
    //  Replace byte extraction with your actual signal layout.
    // ---------------------------------------------------------
    case 0xDEAD:
        // TODO: replace with real CAN ID and signal layout
        // g_dash.gear = (uint8_t)can_signal(data, start, len, 1, 0, false);
        break;

    default:
        break;
    }
}

// =============================================================
//  Stale data detector
//  Returns true if a given ID hasn't been seen in timeout_ms.
//  Use to grey out gauges or show a warning on the display.
// =============================================================
static inline bool can_signal_stale(uint32_t last_ms, uint32_t now_ms, uint32_t timeout_ms)
{
    return (now_ms - last_ms) > timeout_ms;
}
