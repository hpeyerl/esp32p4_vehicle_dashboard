# EV Dashboard — M5Stack Tab5 Port

An EV Dashboard for ESP32-P4 based board with MIPI-DSI.  Currently ported to M5 Tab5 for testing.  Later ports to another display like WaveShare 10.1 or maybe a Riverdi 10.1.

CAN messages are currently configured for ZombieVerter VCU.

## What changed from the Waveshare build

| Item | Waveshare build | Tab5 build |
|---|---|---|
| Resolution | 1280×800 | 1280×720 |
| Display driver | Generic DSI stub (TODO) | ILI9881C or ST7123 (build-flag selected) |
| Touch | GT911 GPIO 8/9 | GT911 GPIO 17/18 or ST7123 combined |
| CAN TX/RX | GPIO 5/4 | GPIO 53/54 (GPIO_EXT / Grove port) |
| Display init | `// TODO` stub | `tab5_display_init()` in `src/tab5_display.h` |

Everything else — CAN parser, signal map, LVGL layout, splash screen,
all build scripts — is unchanged.

## CRITICAL: Hardware revision check

Check the sticker on the back of your Tab5 before building.

  ILI9881C sticker → -DTAB5_DISPLAY_ILI9881C=1 in platformio.ini (default)
  ST7123   sticker → -DTAB5_DISPLAY_ILI9881C=0 in platformio.ini

Units before Oct 14 2025: ILI9881C + GT911
Units from  Oct 14 2025:  ST7123 combined driver

Wrong setting = blank display, no error message.

## IS3050G CAN transceiver wiring

  Tab5 GPIO_EXT pin 1 (GPIO 53) → IS3050G TX
  Tab5 GPIO_EXT pin 2 (GPIO 54) → IS3050G RX
  Tab5 3.3V                      → IS3050G VCC
  Tab5 GND                       → IS3050G GND
  IS3050G CANH → vehicle CANH
  IS3050G CANL → vehicle CANL

The red Grove (HY2.0-4P) port uses the same GPIO 53/54 — either connector works.

## Build

  pio run                              # build
  pio run --target upload              # flash
  pio device monitor                   # serial monitor
  pio run -e tab5_debug --target upload  # debug build

Download mode: hold Reset ~2s until green LED flashes rapidly.

## Still TODO

  1. PRNDL gear CAN ID — stub at case 0xDEAD in include/can_parser.h
  2. Pack amps polarity — verify positive = discharge for your BMS
  3. RANGE_FULL_SOC_MILES in include/can_signals.h
  4. Battery Temp 2 — currently mirrors Batt 1
