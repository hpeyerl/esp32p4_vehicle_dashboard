# EV Dashboard — Project Context

Last updated: 2026-05-30

## Hardware
- **Board**: Waveshare ESP32-P4-Nano
- **Display**: Waveshare 12.3" DSI Touch (HX8399-C, 1920×720 landscape) — arriving ~2 weeks
- **WiFi**: ESP32-C6 coprocessor, connected via **SDIO** internally
- **CAN transceiver**: SN65HVD230 — wired to board (GPIO53 TX, GPIO48 RX), not yet connected to vehicle
- **Flash**: GD25Q128, 16MB

## Pin Assignments (verified against schematic)
| Signal | GPIO | Notes |
|---|---|---|
| TWAI TX | 53 | |
| TWAI RX | 48 | Moved from 3 — 48 is physically next to 53 |
| VSS pulse | 5 | Reed switch, internal pullup |
| C6 Reset/EN | 54 | Via R54 0R to C6 CHIP_PU |
| SDIO CLK | 18 | P4→C6 internal |
| SDIO CMD | 19 | P4→C6 internal |
| SDIO D0 | 14 | |
| SDIO D1 | 15 | |
| SDIO D2/Handshake | 16 | |
| SDIO D3/DataReady | 17 | |

## Current Working State
- WiFi connects via SDIO ESP-Hosted ✓
- Display starts immediately (WiFi/OTA in bg_init_task) ✓
- OTA via `make` (curl with -H "Expect:") ✓
- MJPEG stream on port 81 (separate httpd) ✓
- Main httpd on port 80 stays responsive during streaming ✓
- /view — MJPEG stream + nav bar ✓
- /ota — OTA page ✓
- /settings — param fetch (needs CAN) ✓
- /status-page — spot values grid, auto-refresh ✓
- /nav?screen=X — switches LVGL screen, returns immediately ✓
- Screen switching: Home/Settings/Status via curl or browser ✓
- SDO manager integrated ✓
- Settings + Status pages integrated ✓
- MJPEG tear-free (snapshot buffer) ✓

## File Structure (src/)
- main.cpp — app entry, bg_init_task for WiFi/OTA
- ota_server.c — HTTP server init, OTA handler, mDNS, rollback watchdog
- wifi_manager.c/h — WiFi STA/AP init, event handler
- dashboard_ui.cpp/h — LVGL UI, screen management (Home/Settings/Status)
- display_stub.c/h — headless framebuffer backend
- mjpeg_stream.c/h — MJPEG stream on port 81
- sdo_manager.c/h — CANopen SDO client for ZombieVerter
- settings_page.c/h — /settings web page + /api/params, /api/param
- status_page.c/h — /status-page web page + /api/status
- can_parser.cpp/h — CAN frame parser
- vss_sensor.c/h — VSS pulse counter
- vss_web_handlers.c/h — VSS web API
- layout_waveshare.h / layout_tab5.h / layout_stub.h — display geometry
- dashboard_layout.h — layout selector by DISPLAY_TARGET

## Build / Deploy
```bash
make          # build + deploy (default target)
make build    # build only
make ota      # upload current binary via curl
# First flash:
pio run -e stub_debug_usb -t upload --upload-port /dev/ttyACM0
# Serial monitor:
pio device monitor -e stub_debug --port /dev/ttyACM0
# esptool reset:
~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32p4 --port /dev/ttyACM0 --baud 115200 run
```

## URL Structure
- `/` → redirect to `/view`
- `/view` → HTML page with MJPEG from port 81, nav bar
- `/ota` → OTA drag-drop page
- `/settings` → ZombieVerter param settings
- `/status-page` → spot values auto-refresh
- `http://ev-dashboard.local:81/stream` → raw MJPEG stream
- `/status` → JSON firmware status
- `/api/status` → JSON spot values
- `/api/params` → JSON param list (triggers SDO fetch)
- `/api/param` POST → SDO write
- `/api/save` POST → save to VCU flash
- `/nav?screen=X` → switch LVGL screen (home/settings/status)

## SDO Protocol (ZombieVerter)
- TX: 0x603, RX: 0x583, Node: 3
- Values: ×32 fixed-point
- Param index: 0x2100 | (paramId >> 8), subindex = paramId & 0xFF
- Schema fetch: SDO segmented upload 0x5001/0x00
- Schema JSON: {paramName: {id:N, isparam:bool, value:V}}
- Save: SDO write 0x1010/1 = 0x65766173

## ZombieVerter CAN Broadcasts
- 0x522 udc, 0x411 idc, 0x355 SOC, 0x356 tmpm, 0x528 U12V
- 0x380+node PDO3, 0x480+node PDO4

## Critical sdkconfig.defaults
- `CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y` ← SDIO not SPI
- `CONFIG_ESP_WIFI_REMOTE_EAP_ENABLED=n`
- NO `CONFIG_ESP_HOST_WIFI_ENABLED=y`
- `CONFIG_LV_CONF_SKIP=n`
- `CONFIG_SLAVE_IDF_TARGET_ESP32C6=y`
- `CONFIG_HTTPD_TASK_STACK_SIZE=8192`

## Architecture Notes
- SCons + IDF CMake both compile src/ — IDF CMake wins at link
- `IDF_CMAKE_BUILD=1` in src/CMakeLists.txt guards managed-component code
- `--whole-archive` for libespressif__esp_hosted.a in platformio.ini — required for WiFi constructor
- managed_components/espressif__esp_wifi_remote/CMakeLists.txt patched (git committed)
- cJSON.h copied to include/ for SCons
- WiFi/OTA in bg_init_task (priority 3) — display starts first
- MJPEG snapshot buffer (3rd framebuffer in PSRAM) for tear-free frames
- Param/spot caches heap_caps_calloc'd in PSRAM

## Flash Offsets (ESP32-P4)
- Bootloader: 0x2000, Partitions: 0x8000, OTA data: 0xF000, App: 0x20000

## TODO
1. Connect SN65HVD230 to ZombieVerter CAN bus and test SDO
2. PRND touchable — CAN gear shift (M5Dial working code exists as reference)
3. Arc layout tightening (Tesla-style brackets) — defer to real display
4. ADC dimmer GPIO20 or GPIO21
5. MJPEG color fix (byte-swap) — cosmetic only
6. Background WiFi reconnect indicator on dashboard
7. Navigation touch events on real display
8. Status page populate from g_dash sim data
