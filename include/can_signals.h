#pragma once
// =============================================================
//  EV Dashboard — CAN Signal Definitions
//  Target: ESP32-P4 + Waveshare 10.1" DSI-Touch-A
//  Endianness: Intel (Little Endian) throughout
//  Generated from user-supplied DBC-style signal map
// =============================================================

#include <stdint.h>

// -------------------------------------------------------------
//  CAN Frame IDs
// -------------------------------------------------------------
#define CAN_ID_MOTOR_TEMP       0x125   // Motor temp (same format as 0x126)
#define CAN_ID_INVERTER_TEMP    0x126   // Inverter temp
#define CAN_ID_SPEED            0x257   // Vehicle speed
#define CAN_ID_BMS_MAIN         0x356   // Pack voltage + current + battery temp
#define CAN_ID_SOC              0x355   // State of charge
#define CAN_ID_AUX_BATT         0x210   // 12V auxiliary battery voltage
#define CAN_ID_GEAR             0x312   // JLR G1 shifter PRND
#define CAN_ID_RANGE            0x300   // M5Dial: hi/lo range  L/H/A/HL  (data[0])
#define CAN_ID_MOTOR            0x301   // M5Dial: motor config MG1+2/MG1/MG2/Blend (data[0])

// ZombieVerter dedicated dashboard frames (openinverter CAN Tx, little-endian).
// Apply via zombie_can_map.txt (paste into the Zombie terminal). The Zombie
// CAN-ID field is DECIMAL: 1296=0x510, 1297=0x511, 1298=0x512.
//  0x510: speed@0/16(s) dir@16/8(s) opmode@24/8 power@32/16/g10(s) udc@48/16/g10
//  0x511: idc@0/16/g10(s) uaux@16/16/g100 potnom@32/16/g10(s) Gear@48/8 din_brake@56/8
//  0x512: cruisespeed@0/16 cruisestt@16/8  (see CAN_ID_CRUISE)
#define CAN_ID_VCU1             0x510
#define CAN_ID_VCU2             0x511

// Motor(MG2) RPM -> MPH. GS450H high range, total motor->wheel ratio ~10
// (MG2 reduction ~2.478 x 4.10 diff), 29in tire. PLACEHOLDER — CALIBRATE vs GPS.
#define VCU_RPM_TO_MPH          0.0085f

// Cruise control — Zombieverter cruisespeed + cruisestt
// CAN IDs TBD — add via oic and update these
#define CAN_ID_CRUISE           0x512   // VCU3: cruisespeed@0/16 + cruisestt@16/8

// -------------------------------------------------------------
//  Signal layout within each frame
//  Format: start_bit, length_bits, scale, offset, signed
//
//  Extraction (Intel/LE): value lives in bits [start_bit .. start_bit+length-1]
//  Physical value = (raw * scale) + offset
// -------------------------------------------------------------

// 0x125  Motor Temp  — bits 32-47, scale 0.01, signed, °C
#define SIG_MOTOR_TEMP_START    32
#define SIG_MOTOR_TEMP_LEN      16
#define SIG_MOTOR_TEMP_SCALE    0.01f
#define SIG_MOTOR_TEMP_OFFSET   0.0f
#define SIG_MOTOR_TEMP_SIGNED   true

// 0x126  Inverter Temp — bits 32-47, scale 0.01, signed, °C
#define SIG_INV_TEMP_START      32
#define SIG_INV_TEMP_LEN        16
#define SIG_INV_TEMP_SCALE      0.01f
#define SIG_INV_TEMP_OFFSET     0.0f
#define SIG_INV_TEMP_SIGNED     true

// 0x257  Speed — bits 0-15, scale 0.1, signed, MPH
#define SIG_SPEED_START         0
#define SIG_SPEED_LEN           16
#define SIG_SPEED_SCALE         0.1f
#define SIG_SPEED_OFFSET        0.0f
#define SIG_SPEED_SIGNED        true

// 0x356  Pack Voltage — bits 0-15, scale 0.01, unsigned, V
#define SIG_PACK_VOLTS_START    0
#define SIG_PACK_VOLTS_LEN      16
#define SIG_PACK_VOLTS_SCALE    0.01f
#define SIG_PACK_VOLTS_OFFSET   0.0f
#define SIG_PACK_VOLTS_SIGNED   false

// 0x356  Pack Current — bits 16-31, scale 0.1, signed, A
#define SIG_PACK_AMPS_START     16
#define SIG_PACK_AMPS_LEN       16
#define SIG_PACK_AMPS_SCALE     0.1f
#define SIG_PACK_AMPS_OFFSET    0.0f
#define SIG_PACK_AMPS_SIGNED    true

// 0x356  Battery Temp — bits 32-47, scale 0.1, signed, °C
#define SIG_BATT_TEMP_START     32
#define SIG_BATT_TEMP_LEN       16
#define SIG_BATT_TEMP_SCALE     0.1f
#define SIG_BATT_TEMP_OFFSET    0.0f
#define SIG_BATT_TEMP_SIGNED    true

// 0x355  State of Charge — bits 0-15, scale 1, unsigned, %
#define SIG_SOC_START           0
#define SIG_SOC_LEN             16
#define SIG_SOC_SCALE           1.0f
#define SIG_SOC_OFFSET          0.0f
#define SIG_SOC_SIGNED          false

// 0x210  12V Aux Battery — bits 32-47, scale 0.1, unsigned, V
#define SIG_AUX_VOLTS_START     32
#define SIG_AUX_VOLTS_LEN       16
#define SIG_AUX_VOLTS_SCALE     0.1f
#define SIG_AUX_VOLTS_OFFSET    0.0f
#define SIG_AUX_VOLTS_SIGNED    false

// 0x312  Gear — byte[3] upper nibble (bits 7-4), 0=P 1=R 2=N 3=D
#define SIG_GEAR_BYTE           3
#define SIG_GEAR_SHIFT          4
#define SIG_GEAR_MASK           0x0F

// -------------------------------------------------------------
//  Derived values (not from CAN directly)
// -------------------------------------------------------------
// Power (kW) = (pack_volts * pack_amps) / 1000.0f
// Range (mi) = soc_pct * RANGE_FULL_SOC_MILES / 100.0f

#define RANGE_FULL_SOC_MILES    250.0f   // <-- adjust to your vehicle's rated range

// -------------------------------------------------------------
//  Warning thresholds
// -------------------------------------------------------------
#define WARN_MOTOR_TEMP_C       100.0f
#define CRIT_MOTOR_TEMP_C       130.0f
#define WARN_INV_TEMP_C         80.0f
#define CRIT_INV_TEMP_C         100.0f
#define WARN_BATT_TEMP_C        40.0f
#define CRIT_BATT_TEMP_C        55.0f
#define WARN_SOC_PCT            20.0f
#define LOW_SOC_PCT             10.0f
#define WARN_AUX_VOLTS_LOW      11.5f
#define WARN_AUX_VOLTS_HIGH     15.5f

// -------------------------------------------------------------
//  Cruise control
// -------------------------------------------------------------
// cruisestt bitmask values (from Stm32-vcu vehicle.h)
#define CRUISE_CC_NONE      0
#define CRUISE_CC_ON        1
#define CRUISE_CC_CANCEL    2
#define CRUISE_CC_SET       4
#define CRUISE_CC_RESUME    8

// cruisespeed is in motor RPM — convert to kph for display
// Assumes: high gear 1:1, transfer case 1:1, diff 4.10:1, 33" tires
// kph = rpm * PI * 0.0254 * 33 * 3.6 / (4.10 * 60)
#define CRUISE_RPM_TO_KPH   0.03862f
#define CRUISE_KPH_TO_RPM   25.89f
