#!/bin/bash
# =============================================================
#  setup_can_mappings.sh — Zombieverter CAN TX mappings
#  for EV Dashboard (ESP32-P4 / M5Stack Tab5)
#
#  Uses openinverter-can-tool (oic) to configure Zombieverter
#  to transmit the signals the dashboard expects.
#
#  Usage:
#    oic -u http://<zombieverter-ip> can add ...
#  or via serial:
#    oic -d /dev/ttyUSB0 can add ...
#
#  CAN IDs and signal layout must match can_signals.h exactly.
#
#  Signal format: oic can add <param> <can_id> <start_bit> <length> <gain> <offset>
#
#  Note on gain:
#    oic gain is a multiplier applied to the param value before sending.
#    Our scale factors in can_signals.h are divisors (0.1 means raw/10 = physical).
#    So oic gain = 1/scale = 10 for 0.1 scale signals, 1 for 1.0 scale signals.
# =============================================================

OIC="oic"         # path to oic binary, adjust if needed
TARGET="${1:-}"   # pass -u http://ip or -d /dev/ttyX as first argument

if [ -z "$TARGET" ]; then
    echo "Usage: $0 '-u http://<ip>' or '$0 -d /dev/ttyUSB0'"
    exit 1
fi

set -e

echo "Configuring Zombieverter CAN TX mappings..."

# ── 0x125  Motor Temp  — bits 32-47, scale 1.0, signed, °C ──────────────
$OIC $TARGET can add tmpm 0x125 32 16 1 0

# ── 0x126  Inverter Temp — bits 32-47, scale 1.0, signed, °C ────────────
$OIC $TARGET can add tmphs 0x126 32 16 1 0

# ── 0x257  Vehicle Speed — bits 0-15, scale 0.1, signed, kph ────────────
# gain=10: raw = Veh_Speed * 10, dashboard divides by 10 → kph
$OIC $TARGET can add Veh_Speed 0x257 0 16 10 0

# ── 0x355  SOC — bits 0-15, scale 1.0, unsigned, % ──────────────────────
$OIC $TARGET can add SOC 0x355 0 16 1 0

# ── 0x356  Pack Voltage — bits 0-15, scale 0.1, unsigned, V ─────────────
# gain=10: raw = udc * 10, dashboard divides by 10 → V
$OIC $TARGET can add udc 0x356 0 16 10 0

# ── 0x356  Pack Current — bits 16-31, scale 0.1, signed, A ─────────────
# gain=10: raw = idc * 10, dashboard divides by 10 → A
# Note: Zombieverter idc sign convention: negative=discharge, positive=charge
# Dashboard convention: positive=drive (discharge), negative=regen (charge)
# If display shows inverted sign, change gain to -10
$OIC $TARGET can add idc 0x356 16 16 10 0

# ── 0x356  Battery Temp — bits 32-47, scale 0.1, signed, °C ─────────────
# gain=10: raw = BMS_Tmax * 10
$OIC $TARGET can add BMS_Tmax 0x356 32 16 10 0

# ── 0x210  12V Aux — bits 32-47, scale 0.1, unsigned, V ─────────────────
# gain=10: raw = uaux * 10
$OIC $TARGET can add uaux 0x210 32 16 10 0

# ── 0x312  Gear — from JLR G1 M5Dial shifter directly (not Zombieverter) ──
# No oic mapping needed — shifter transmits this frame autonomously.
# byte[3] upper nibble: 0=P 1=R 2=N 3=D

echo "Done. Verify with: $OIC $TARGET can list"
