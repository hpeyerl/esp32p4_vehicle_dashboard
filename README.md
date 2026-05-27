# EV Dashboard — ESP32-P4 Nano + Waveshare 12.3"

An EV Dashboard for the ESP32-P4-Nano with MIPI-DSI display support.
CAN messages are currently configured for ZombieVerter VCU.

## Hardware

| Component | Part |
|---|---|
| MCU board | Waveshare ESP32-P4-Nano |
| Display | Waveshare 12.3" DSI Touch Display (12.3-DSI-TOUCH-A) |
| Display controller | Himax HX8399-C, 2-lane MIPI-DSI, 720×1920 portrait |
| Touch | Goodix GT911, I2C on GPIO7/8 (DSI connector) |
| CAN transceiver | IS3050G or compatible (TWAI, GPIO53/54) |
| Flash | GD25Q128, 16MB |
| PSRAM | 32MB HEX |

## Pin assignments (ESP32-P4-Nano)

| Signal | GPIO | Notes |
|---|---|---|
| DSI D0/D1/CLK | internal | MIPI DSI peripheral, not on headers |
| Touch I2C SDA | GPIO 7 | On DSI FPC connector |
| Touch I2C SCL | GPIO 8 | On DSI FPC connector |
| Touch INT | GPIO 23 | |
| Touch RST | GPIO 24 | |
| LCD Reset | GPIO 27 | Active low |
| Backlight | GPIO 22 | LEDC PWM |
| TWAI TX | GPIO 53 | IS3050G CAN transceiver |
| TWAI RX | GPIO 54 | IS3050G CAN transceiver |
| VSS pulse | GPIO 5 | Reed switch to GND, internal pullup |

## CAN transceiver wiring (IS3050G)

```
ESP32-P4-Nano GPIO53  →  IS3050G TX
ESP32-P4-Nano GPIO54  →  IS3050G RX
ESP32-P4-Nano 3.3V    →  IS3050G VCC
ESP32-P4-Nano GND     →  IS3050G GND
IS3050G CANH          →  vehicle CANH
IS3050G CANL          →  vehicle CANL
```

## VSS (Vehicle Speed Sensor) wiring

Reed switch VSS — two wire, no polarity:
```
One wire  →  GPIO5
Other wire →  GND
```
Internal pullup enabled. Calibration (tire circumference, diff ratio,
pulses/rev) editable at http://ev-dashboard.local/vss after first boot.

## First-time setup

### 1. WiFi credentials

Copy `include/secrets.h.template` to `include/secrets.h` and fill in:
```c
#define OTA_STA_SSID     "your_network"
#define OTA_STA_PASSWORD "your_password"
```
`secrets.h` is gitignored — never committed. If absent, the device
creates an AP: SSID `ev-dashboard`, password `dashboard1`.

### 2. First USB flash

The first flash must be via USB — OTA partitions don't exist yet.
Connect USB, then:
```bash
pio run -e waveshare_usb -t upload --upload-port /dev/ttyACM0
```

After this, all subsequent flashes can be OTA over WiFi.

### 3. Flash offsets (ESP32-P4 specific)

ESP32-P4 uses non-standard offsets. PlatformIO handles these automatically.
If you ever need to flash manually with esptool:
```bash
~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32p4 --port /dev/ttyACM0 --baud 921600 \
  write_flash \
  0x2000   .pio/build/waveshare_usb/bootloader.bin \
  0x8000   .pio/build/waveshare_usb/partitions.bin \
  0xF000   .pio/build/waveshare_usb/ota_data_initial.bin \
  0x20000  .pio/build/waveshare_usb/firmware.bin
```

## Build targets

### Normal development (OTA — after first USB flash)

```bash
# Build + flash over WiFi (uses ev-dashboard.local)
pio run -e waveshare -t upload

# With explicit IP if mDNS isn't resolving
OTA_HOST=192.168.1.x pio run -e waveshare -t upload
```

### USB flash targets (first flash, recovery, partition table change)

```bash
# Waveshare 12.3" — full USB flash (bootloader + partitions + app)
pio run -e waveshare_usb -t upload --upload-port /dev/ttyACM0

# M5Stack Tab5 — full USB flash
pio run -e tab5_usb -t upload --upload-port /dev/ttyACM0
```

### Debug builds (verbose logging, DASHBOARD_DEBUG_CAN=1)

```bash
# Waveshare debug — build + OTA flash
pio run -e waveshare_debug -t upload

# Waveshare debug — USB flash
pio run -e waveshare_debug
~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32p4 --port /dev/ttyACM0 --baud 921600 \
  write_flash \
  0x2000  .pio/build/waveshare_debug/bootloader.bin \
  0x8000  .pio/build/waveshare_debug/partitions.bin \
  0xF000  .pio/build/waveshare_debug/ota_data_initial.bin \
  0x20000 .pio/build/waveshare_debug/firmware.bin
```

### Stub builds (no display hardware required)

Use while waiting for display hardware. LVGL renders to a software
framebuffer. WiFi, OTA, CAN, and VSS all work normally.
MJPEG stream available at http://ev-dashboard.local/stream (TODO).

```bash
# USB flash (stub, release)
pio run -e stub_usb -t upload --upload-port /dev/ttyACM0

# USB flash (stub, debug)
pio run -e stub_debug_usb -t upload --upload-port /dev/ttyACM0

# OTA flash once stub firmware is running
pio run -e stub -t upload
```

### Serial monitor

```bash
pio device monitor -e waveshare_debug --port /dev/ttyACM0
```

### Build only (no upload)

```bash
pio run -e waveshare
pio run -e waveshare_debug
pio run -e stub
pio run -e stub_debug
pio run -e tab5
pio run -e tab5_debug
```

## OTA web interface

After boot, the device announces itself via mDNS as `ev-dashboard.local`.

| URL | Function |
|---|---|
| http://ev-dashboard.local/ | OTA firmware upload (drag & drop) |
| http://ev-dashboard.local/status | JSON: version, partition, IP |
| http://ev-dashboard.local/update | POST endpoint for curl/pio upload |
| http://ev-dashboard.local/vss | VSS calibration (tire, diff ratio, PPR) |
| http://ev-dashboard.local/stream | MJPEG display stream (TODO) |

### OTA via curl

```bash
curl -X POST http://ev-dashboard.local/update \
     -H "Content-Type: application/octet-stream" \
     --data-binary @.pio/build/waveshare/firmware.bin
```

## Rollback safety

New OTA firmware has 30 seconds to call `ota_server_mark_valid()`.
If the firmware crashes before that, the bootloader automatically
rolls back to the previous working image on next boot.

## Display target summary

| env | Display | DSI lanes | Resolution | USB/OTA |
|---|---|---|---|---|
| `waveshare` | Waveshare 12.3" HX8399-C | 2 | 1920×720 landscape | OTA |
| `waveshare_usb` | Waveshare 12.3" HX8399-C | 2 | 1920×720 landscape | USB |
| `waveshare_debug` | Waveshare 12.3" HX8399-C | 2 | 1920×720 landscape | OTA |
| `tab5` | M5Stack Tab5 ST7123 | 2 | 1280×720 landscape | OTA |
| `tab5_usb` | M5Stack Tab5 ST7123 | 2 | 1280×720 landscape | USB |
| `tab5_debug` | M5Stack Tab5 ST7123 | 2 | 1280×720 landscape | OTA |
| `stub` | Software framebuffer | none | 1920×720 | OTA |
| `stub_usb` | Software framebuffer | none | 1920×720 | USB |
| `stub_debug` | Software framebuffer | none | 1920×720 | OTA |
| `stub_debug_usb` | Software framebuffer | none | 1920×720 | USB |

## Still TODO

1. MJPEG stream at `/stream` — framebuffer available via `display_stub_get_fb()`
2. PRNDL gear CAN ID — stub at `case 0xDEAD` in `include/can_parser.h`
3. Pack amps polarity — verify positive = discharge for your BMS
4. `RANGE_FULL_SOC_MILES` in `include/can_signals.h`
5. Battery Temp 2 — currently mirrors Batt 1
6. Additional dashboard widgets for 1920×720 real estate
