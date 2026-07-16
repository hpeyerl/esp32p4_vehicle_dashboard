// =============================================================
//  can_parser.cpp — CAN Frame Parser Implementation
// =============================================================

#include "can_parser.h"
#include <limits.h>
#include <string.h>

// Global dashboard data instance
DashData g_dash = { .hl_mode = -1, .mg_mode = -1,
                    .vcu_dir = INT8_MIN, .vcu_range = -1,
                    .dir_confirmed = INT8_MIN };

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

    case CAN_ID_GEAR:                               // 0x312 JLR G1 shifter
        // byte[3] upper nibble: 0=P 1=R 2=N 3=D
        // NOTE: requested gear only — not confirmed by Zombieverter.
        // Cross-check against dir_confirmed when oic dir mapping is added.
        g_dash.gear = (data[SIG_GEAR_BYTE] >> SIG_GEAR_SHIFT) & SIG_GEAR_MASK;
        if (g_dash.gear > 3) g_dash.gear = 0;
        g_dash.last_ms_0x312 = now_ms;
        break;

    case CAN_ID_RANGE:                             // 0x300 M5Dial hi/lo range
        g_dash.hl_mode = (int8_t)(data[0] & 0x03);
        break;

    case CAN_ID_MOTOR:                             // 0x301 M5Dial motor config
        g_dash.mg_mode = (int8_t)(data[0] & 0x03);
        break;

    case CAN_ID_VCU1: {                            // 0x510 ZombieVerter core
        g_dash.vcu_rpm    = (int16_t) extract_le(data,  0, 16);          // signed
        g_dash.vcu_dir    = (int8_t)  extract_le(data, 16,  8);          // DIRS
        g_dash.vcu_opmode = (uint8_t) extract_le(data, 24,  8);
        g_dash.power_kw   = (int16_t) extract_le(data, 32, 16) / 10.0f;  // real kW
        g_dash.vcu_udc    = (uint16_t)extract_le(data, 48, 16) / 10.0f;
        int rpm = g_dash.vcu_rpm < 0 ? -g_dash.vcu_rpm : g_dash.vcu_rpm;
        g_dash.speed      = rpm * VCU_RPM_TO_MPH;                        // coarse
        g_dash.last_ms_0x510 = now_ms;
        break;
    }

    case CAN_ID_VCU2: {                            // 0x511 ZombieVerter aux
        g_dash.vcu_idc    = (int16_t) extract_le(data,  0, 16) / 10.0f;  // signed
        g_dash.aux_volts  = (uint16_t)extract_le(data, 16, 16) / 100.0f; // uaux 12V
        g_dash.vcu_potnom = (int16_t) extract_le(data, 32, 16) / 10.0f;  // signed
        g_dash.vcu_range  = (int8_t)  extract_le(data, 48,  8);          // 0=Lo 1=Hi
        g_dash.last_ms_0x511 = now_ms;
        break;
    }

    // TODO: add case for Zombieverter dir signal (CAN ID TBD via oic)
    // case CAN_ID_DIR:
    //     g_dash.dir_confirmed = (int8_t)can_signal(data, ...);
    //     break;

    case CAN_ID_CRUISE:                             // 0xDEAD — TODO: assign
        // TODO: define signal layout once oic CAN IDs are known
        // cruisestt  = bitmask byte, signal layout TBD
        // cruisespeed = RPM value, signal layout TBD
        // g_dash.cruise_state = (uint8_t)can_signal(data, ...);
        // g_dash.cruise_kph   = can_signal(data, ...) * CRUISE_RPM_TO_KPH;
        g_dash.last_ms_cruise = now_ms;
        break;

    default:
        break;
    }
}

bool can_signal_stale(uint32_t last_ms, uint32_t now_ms, uint32_t timeout_ms)
{
    return (now_ms - last_ms) > timeout_ms;
}
