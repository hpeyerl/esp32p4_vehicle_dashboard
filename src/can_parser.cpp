// =============================================================
//  can_parser.cpp — CAN Frame Parser Implementation
// =============================================================

#include "can_parser.h"
#include <string.h>

// Global dashboard data instance
DashData g_dash = {};

// ── Intel (LE) signal extraction ──────────────────────────────────────────
static uint64_t extract_le(const uint8_t *data, uint8_t start_bit, uint8_t len)
{
    uint64_t frame = 0;
    memcpy(&frame, data, 8);
    uint64_t mask = (len == 64) ? UINT64_MAX : ((1ULL << len) - 1ULL);
    return (frame >> start_bit) & mask;
}

static int64_t sign_extend(uint64_t val, uint8_t bits)
{
    uint64_t sign_bit = 1ULL << (bits - 1);
    if (val & sign_bit)
        return (int64_t)(val | (~0ULL << bits));
    return (int64_t)val;
}

static float can_signal(const uint8_t *data,
                         uint8_t start_bit, uint8_t len,
                         float scale, float offset, bool is_signed)
{
    uint64_t raw = extract_le(data, start_bit, len);
    float phys = is_signed
        ? (float)sign_extend(raw, len) * scale + offset
        : (float)raw * scale + offset;
    return phys;
}

// ── Public API ────────────────────────────────────────────────────────────
void parse_can_frame(uint32_t id, const uint8_t *data, uint32_t now_ms)
{
    switch (id) {

    case CAN_ID_MOTOR_TEMP:                             // 0x125
        g_dash.motor_temp_c = can_signal(data,
            SIG_MOTOR_TEMP_START, SIG_MOTOR_TEMP_LEN,
            SIG_MOTOR_TEMP_SCALE, SIG_MOTOR_TEMP_OFFSET,
            SIG_MOTOR_TEMP_SIGNED);
        g_dash.last_ms_0x125 = now_ms;
        break;

    case CAN_ID_INVERTER_TEMP:                          // 0x126
        g_dash.inverter_temp_c = can_signal(data,
            SIG_INV_TEMP_START, SIG_INV_TEMP_LEN,
            SIG_INV_TEMP_SCALE, SIG_INV_TEMP_OFFSET,
            SIG_INV_TEMP_SIGNED);
        g_dash.last_ms_0x126 = now_ms;
        break;

    case CAN_ID_SPEED:                                  // 0x257
        g_dash.speed = can_signal(data,
            SIG_SPEED_START, SIG_SPEED_LEN,
            SIG_SPEED_SCALE, SIG_SPEED_OFFSET,
            SIG_SPEED_SIGNED);
        g_dash.last_ms_0x257 = now_ms;
        break;

    case CAN_ID_SOC:                                    // 0x355
        g_dash.soc_pct = can_signal(data,
            SIG_SOC_START, SIG_SOC_LEN,
            SIG_SOC_SCALE, SIG_SOC_OFFSET,
            SIG_SOC_SIGNED);
        if (g_dash.soc_pct > 100.0f) g_dash.soc_pct = 100.0f;
        if (g_dash.soc_pct <   0.0f) g_dash.soc_pct =   0.0f;
        g_dash.range_dist = g_dash.soc_pct * RANGE_FULL_SOC_MILES / 100.0f;
        g_dash.last_ms_0x355 = now_ms;
        break;

    case CAN_ID_BMS_MAIN:                               // 0x356
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
        // Positive = discharge (motoring), negative = regen
        g_dash.power_kw = (g_dash.pack_volts * g_dash.pack_amps) / 1000.0f;
        g_dash.last_ms_0x356 = now_ms;
        break;

    case CAN_ID_AUX_BATT:                              // 0x210
        g_dash.aux_volts = can_signal(data,
            SIG_AUX_VOLTS_START, SIG_AUX_VOLTS_LEN,
            SIG_AUX_VOLTS_SCALE, SIG_AUX_VOLTS_OFFSET,
            SIG_AUX_VOLTS_SIGNED);
        g_dash.last_ms_0x210 = now_ms;
        break;

    case 0xDEAD:
        // TODO: replace with real PRNDL CAN ID and signal layout
        // g_dash.gear = (uint8_t)can_signal(data, start, len, 1, 0, false);
        break;

    default:
        break;
    }
}

bool can_signal_stale(uint32_t last_ms, uint32_t now_ms, uint32_t timeout_ms)
{
    return (now_ms - last_ms) > timeout_ms;
}
