# EV Dashboard — ESP32-P4 + Waveshare 10.1" DSI-Touch-A
## PlatformIO Build Guide

### Project structure
```
ev_dashboard_pio/
├── platformio.ini          ← PlatformIO project config (board, libs, pins)
├── sdkconfig.defaults      ← ESP-IDF Kconfig overrides (PSRAM, TWAI, DSI)
├── partitions_16MB.csv     ← Custom partition table (15 MB app partition)
├── src/
│   └── main.cpp            ← Application entry point (app_main)
└── include/
    ├── lv_conf.h           ← LVGL v9 configuration
    ├── can_signals.h       ← CAN IDs, bit layouts, scale/offset, thresholds
    ├── can_parser.h        ← Frame parser + DashData struct
    └── dashboard_ui.h      ← LVGL widget create + update functions
```

---

### Prerequisites

1. **Install PlatformIO** — VS Code extension or CLI:
   ```bash
   pip install platformio
   ```

2. **ESP-IDF toolchain** — PlatformIO downloads this automatically on first build.
   Requires ESP-IDF ≥ 5.2 for ESP32-P4 support.

---

### Build & Flash

```bash
# Clone / unzip project, then:
cd ev_dashboard_pio

# First build (downloads platform + toolchain ~5 min)
pio run

# Build + flash
pio run --target upload

# Serial monitor
pio device monitor

# Build + flash + monitor in one command
pio run --target upload && pio device monitor

# Debug build (verbose CAN frame logging)
pio run -e esp32p4_debug --target upload
```

---

### Pin configuration

All GPIO assignments live in `platformio.ini` under `build_flags`.
Change them there — no need to edit source files.

```ini
build_flags =
    -DTWAI_TX_PIN=5       ; CAN transceiver TX
    -DTWAI_RX_PIN=4       ; CAN transceiver RX
    -DTOUCH_SDA_PIN=8     ; GT911 I2C SDA
    -DTOUCH_SCL_PIN=9     ; GT911 I2C SCL
    -DTOUCH_INT_PIN=3     ; GT911 interrupt
    -DTOUCH_RST_PIN=2     ; GT911 reset
```

### CAN transceiver wiring (SN65HVD230 recommended — 3.3 V logic)
```
ESP32-P4 TWAI_TX_PIN  →  SN65HVD230 TXD
ESP32-P4 TWAI_RX_PIN  ←  SN65HVD230 RXD
SN65HVD230 CANH       →  vehicle CANH
SN65HVD230 CANL       →  vehicle CANL
SN65HVD230 VCC        →  3.3 V
SN65HVD230 GND        →  GND
120 Ω termination between CANH–CANL if ESP32-P4 is a bus endpoint.
```

---

### Confirmed CAN signal map

| Parameter       | CAN ID | Start Bit | Len | Scale | Offset | Sign     | Unit |
|-----------------|--------|-----------|-----|-------|--------|----------|------|
| Motor Temp      | 0x125  | 32        | 16  | 1.0   | 0      | Signed   | °C   |
| Inverter Temp   | 0x126  | 32        | 16  | 1.0   | 0      | Signed   | °C   |
| Speed           | 0x257  | 0         | 16  | 0.1   | 0      | Signed   | MPH  |
| Pack Voltage    | 0x356  | 0         | 16  | 0.1   | 0      | Unsigned | V    |
| Pack Current    | 0x356  | 16        | 16  | 0.1   | 0      | Signed   | A    |
| Battery Temp    | 0x356  | 32        | 16  | 0.1   | 0      | Signed   | °C   |
| State of Charge | 0x355  | 0         | 16  | 1.0   | 0      | Unsigned | %    |
| 12V Aux Voltage | 0x210  | 32        | 16  | 0.1   | 0      | Unsigned | V    |

Derived:
- **Power (kW)** = `pack_volts × pack_amps / 1000`  (positive = drive, negative = regen)
- **Range (mi)** = `soc_pct × RANGE_FULL_SOC_MILES / 100`
  → Set `RANGE_FULL_SOC_MILES` in `include/can_signals.h`

---

### Key differences from ESP-IDF build

| Item | ESP-IDF | PlatformIO |
|---|---|---|
| Entry point | `app_main()` in `main.c` | `app_main()` in `src/main.cpp` (same logic, `extern "C"` added) |
| Build config | `idf_component.yml` + `CMakeLists.txt` | `platformio.ini` |
| LVGL dependency | `idf_component.yml` registry | `lib_deps = lvgl/lvgl @ ^9.1.0` |
| LVGL config | `lv_conf.h` anywhere on include path | `include/lv_conf.h` + `-DLVGL_CONF_INCLUDE_SIMPLE` flag |
| LVGL tick | Separate `esp_timer` callback | Removed — `lv_conf.h` uses `LV_TICK_CUSTOM` → `esp_timer_get_time()` |
| Kconfig | `sdkconfig` edited by `idf.py menuconfig` | `sdkconfig.defaults` in project root |
| Pin definitions | Hardcoded `GPIO_NUM_x` macros | `build_flags -DTWAI_TX_PIN=5` etc. |
| Flash partition | Default | `partitions_16MB.csv` (15 MB app) |

---

### Outstanding TODOs

1. **DSI flush callback** — `lvgl_flush_cb()` in `src/main.cpp` has a `// TODO`.
   Add your `esp_lcd_panel_draw_bitmap()` call once you have the Waveshare
   DSI panel init sequence integrated.

2. **GT911 touch input driver** — touch init + LVGL `lv_indev_t` registration
   not yet implemented. I2C pins are defined; driver code needed.

3. **PRNDL gear signal** — stub at `case 0xDEAD:` in `include/can_parser.h`.
   Replace with real CAN ID and byte layout when known.

4. **Pack amps polarity** — verify positive = discharge for your BMS.
   Negate `pack_amps` in the `case CAN_ID_BMS_MAIN:` block if needed.

5. **Rated range** — set `RANGE_FULL_SOC_MILES` in `include/can_signals.h`
   to your vehicle's actual EPA/rated range.
