# EV Dashboard — ESP32-P4 Nano

LVGL dashboard for an electric vehicle conversion.
CAN messages are configured for a ZombieVerter VCU (JLR G1 drivetrain).

## Hardware

| Component | Part |
|---|---|
| MCU board | Waveshare ESP32-P4-Nano |
| Display | Waveshare 12.3" DSI Touch (12.3-DSI-TOUCH-A) — 1920×720 |
| Display controller | Himax HX8399-C, 2-lane MIPI-DSI |
| WiFi | ESP32-C6 coprocessor (on-board, SDIO via ESP-Hosted) |
| CAN transceiver | SN65HVD230 (TWAI, GPIO53 TX / GPIO48 RX) |
| Flash | GD25Q128, 16MB |
| PSRAM | 32MB HEX |

## Pin Assignments

| Signal | GPIO | Notes |
|---|---|---|
| TWAI TX | 53 | SN65HVD230 |
| TWAI RX | 48 | SN65HVD230 |
| VSS pulse | 5 | Reed switch to GND, internal pullup |
| SDIO CLK | 18 | ESP32-P4 → C6 (internal) |
| SDIO CMD | 19 | ESP32-P4 → C6 (internal) |
| SDIO D0–D3 | 14–17 | ESP32-P4 → C6 (internal) |
| C6 Reset | 54 | Via R54 0Ω to C6 CHIP_PU |

DSI, touch I2C, backlight, and LCD reset are on the DSI FPC connector — not configurable.

## CAN Transceiver Wiring (SN65HVD230)

```
ESP32-P4-Nano GPIO53  →  SN65HVD230 TX
ESP32-P4-Nano GPIO48  →  SN65HVD230 RX
ESP32-P4-Nano 3.3V    →  SN65HVD230 VCC
ESP32-P4-Nano GND     →  SN65HVD230 GND
SN65HVD230 CANH       →  vehicle CANH
SN65HVD230 CANL       →  vehicle CANL
```

## VSS Wiring

Reed switch (two-wire, no polarity):
```
One wire   →  GPIO5
Other wire →  GND
```
Internal pullup enabled. Default calibration: 103.67" tire circumference,
4.10 diff ratio, 4 pulses/rev. Override in `include/vss_sensor.h` or at
runtime via `http://ev-dashboard.local/settings`.

## First-Time Setup

### 1. WiFi credentials

Copy `include/wifi_config.h.template` to `include/wifi_config.h` and fill in:
```c
#define OTA_STA_SSID     "your_network"
#define OTA_STA_PASSWORD "your_password"
```
If absent, the device falls back to AP mode: SSID `ev-dashboard`, password `dashboard1`.

### 2. First flash (USB)

```bash
pio run -e stub_debug_usb -t upload --upload-port /dev/ttyACM0
```

After this, all subsequent flashes can be done over WiFi with `make`.

### 3. All subsequent flashes (OTA)

```bash
make          # build + deploy (curl POST to ev-dashboard.local/update)
make build    # build only
make ota      # deploy current binary without rebuilding
```

## Build Environments

| env | Display | Resolution | Flash method |
|---|---|---|---|
| `stub_debug` | Software framebuffer | 1920×720 | OTA (`make`) |
| `stub_debug_usb` | Software framebuffer | 1920×720 | USB |
| `stub` | Software framebuffer | 1920×720 | OTA |
| `stub_usb` | Software framebuffer | 1920×720 | USB |
| `waveshare` | Waveshare 12.3" HX8399-C | 1920×720 | OTA |
| `waveshare_usb` | Waveshare 12.3" HX8399-C | 1920×720 | USB |
| `waveshare_debug` | Waveshare 12.3" HX8399-C | 1920×720 | OTA |
| `tab5` | M5Stack Tab5 ST7123 | 1280×720 | OTA |
| `tab5_usb` | M5Stack Tab5 ST7123 | 1280×720 | USB |

The stub environments run without any display hardware. LVGL renders to a
software framebuffer; the MJPEG stream lets you see the UI in a browser.

## Web Interface

The device announces itself as `ev-dashboard.local` via mDNS.

| URL | Description |
|---|---|
| `http://ev-dashboard.local/` | Redirects to `/view` |
| `http://ev-dashboard.local/view` | Live MJPEG stream + navigation bar |
| `http://ev-dashboard.local/ota` | OTA firmware drag-and-drop |
| `http://ev-dashboard.local/settings` | ZombieVerter parameter editor (SDO) |
| `http://ev-dashboard.local/status-page` | Live spot values (auto-refresh) |
| `http://ev-dashboard.local/status` | JSON: firmware version, partition, IP |
| `http://ev-dashboard.local/api/status` | JSON spot values |
| `http://ev-dashboard.local/api/params` | JSON param list (triggers SDO fetch) |
| `http://ev-dashboard.local/api/param` | POST: write a ZombieVerter parameter |
| `http://ev-dashboard.local/api/save` | POST: save params to VCU flash |
| `http://ev-dashboard.local/nav?screen=X` | Switch LVGL screen (home/settings/status) |
| `http://ev-dashboard.local:81/stream` | Raw MJPEG stream (dedicated server) |

## MJPEG Stream

The display framebuffer is exposed as an MJPEG stream on port 81, running on
a dedicated HTTP server so OTA and navigation stay responsive during streaming.
Open `http://ev-dashboard.local/view` for the framed view with navigation
buttons, or connect directly to `:81/stream` for the raw feed.

## Dashboard Layout

```
┌──────────────┬──┬──────────────────────┬──┬────────────┐
│ 5× half-arc  │  │                      │  │ Efficiency │
│ meter gauges │S │    157  km/h         │P │ Trip kWh  │
│ inv/mot/bat  │O │    D  P R N D        │W │ Range     │
│ pack V / A   │C │                      │R │ 12V aux   │
├──────────────┴──┴──────────────────────┴──┴────────────┤
│  [Home]  [Settings]  [Status]              CAN  WiFi   │
└──────────────────────────────────────────────────────────┘
```

- **SOC arc** — `(` shape left of speed; cyan ≥50% / amber 21–49% / red ≤20%
- **Power arc** — `)` shape right of speed; orange = drive, green = regen
- **PRND labels** — tappable on touchscreen, sends CAN 0x312 at 20 ms
- **CAN / WiFi dots** — red/green status indicators at bottom of right panel
- **Left panel** — 5 half-circle meter gauges (inverter temp, motor temp, battery temp, pack volts, pack amps)
- **Navigation bar** — Home / Settings / Status; switchable from browser or touch

## CAN Signals (ZombieVerter)

| CAN ID | Signal |
|---|---|
| 0x125 | Motor temperature |
| 0x126 | Inverter temperature |
| 0x210 | 12V aux battery voltage |
| 0x257 | Vehicle speed |
| 0x312 | PRND gear (byte[3] upper nibble: 0=P 1=R 2=N 3=D) |
| 0x355 | State of charge |
| 0x356 | Pack voltage, pack amps, battery temperature |

SDO: node 3, TX 0x603 / RX 0x583. Parameters are ×32 fixed-point at index
`0x2100 | (paramId >> 8)`, subindex `paramId & 0xFF`.

## OTA Rollback

New firmware has 30 seconds to call `ota_server_mark_valid()`. If it crashes
before that, the bootloader rolls back to the previous image on next boot.

## Serial Monitor

```bash
pio device monitor -e stub_debug --port /dev/ttyACM0
```

## esptool Reset (if needed)

```bash
~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32p4 --port /dev/ttyACM0 --baud 115200 run
```
