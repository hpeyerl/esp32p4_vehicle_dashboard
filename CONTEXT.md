# EV Dashboard — Project Context

Last updated: 2026-06-02 (session 2)

## Hardware
- **Board**: Waveshare ESP32-P4-Nano
- **Display**: Waveshare 12.3" DSI Touch (HX8399-C, 1920×720 landscape) — arriving ~2 weeks
- **WiFi**: ESP32-C6 coprocessor, connected via **SDIO** internally
- **CAN transceiver**: SN65HVD230 — wired to board (GPIO53 TX, GPIO48 RX), not yet connected to vehicle
- **Flash**: GD25Q128, 16MB

## Pin Assignments (verified against P4-Nano expansion header pinout)
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
| EPB output (button) | 6 | Active-low pulse ~200ms, normally HIGH |
| EPB green LED input | 2 | Active-low + pullup, brake released (was 9 — not on headers) |
| EPB red LED input | 3 | Active-low + pullup, brake applied (was 10 — not on headers) |
| MagneRide CH1 PWM | 45 | LEDC, 25 kHz, left shock |
| MagneRide CH2 PWM | 46 | LEDC, 25 kHz, right shock |
| MagneRide CH3 PWM | 47 | Reserved — DNP on hat |
| MagneRide CH4 PWM | 33 | Reserved — DNP on hat |
| Display backlight dimmer | 20 | ADC1_CH4 or LEDC PWM |

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
2. ~~PRND touchable~~ DONE
3. Arc layout tightening (Tesla-style brackets) — defer to real display
4. ADC dimmer GPIO20 or GPIO21
5. MJPEG color fix (byte-swap) — cosmetic only
6. ~~CAN + WiFi status indicators~~ DONE
7. Navigation touch events on real display
8. ~~Status page~~ DONE
10. Odometer — accumulate VSS distance, persist in NVS, display in center panel between arcs
    - VSS already provides speed; integrate over time for distance
    - Write to NVS periodically (e.g. every 0.5 km) to limit flash wear cycles
    - Display between SOC and power arcs in center panel (where PRND used to be)
    - Trip odometer (reset on power cycle or button) + total odometer
    - Consider: NVS namespace "odo", keys "total_km" (float or uint32 in 0.1km units)
    - vss_sensor.c already has vss_get_mph(); add vss_add_distance() or accumulate in ui_task
11. MagneRide suspension control (Sport / Comfort / Auto modes)
    - MagneRide shocks controlled via PWM duty cycle at 25 kHz (LEDC peripheral)
    - Each shock: 3-5A at 12-14V — requires D4184/AOD4184/LR7843 MOSFET driver board per channel
    - 2 channels: left + right shock, each needs one ESP32 GPIO PWM output
    - Flyback diode (1N5408/UF5408) recommended directly at shock connector
    - Duty cycle maps to stiffness: ~35% = comfort, higher = stiffer/sport
    - Modes: Comfort, Sport, Auto (auto-stiffens above configurable speed threshold using VSS)
    - UI: toggle button on home screen; show current mode; Auto mode indicator
    - GPIO candidates: many free pins available (20/21 already considered for dimmer)
    - Module: suspension_ctrl.c — ledc_timer_config + ledc_channel_config, mode stored in NVS
    - Safety: in Auto mode, gradual ramp (not instant step) to avoid harsh transition
9. EPB / Park button integration — This drivetrain has no Park or Neutral gear (only R and D).
   Park is a separate EPB controller with a momentary button and status LEDs (not CAN — direct GPIO).
   Hardware interface (P4-Nano GPIOs — exact pins TBD, defaults below):
   - EPB_GREEN_PIN (GPIO 2):  input + pullup, active-low -> brake RELEASED
   - EPB_RED_PIN   (GPIO 3):  input + pullup, active-low -> brake APPLIED
   - EPB_OUT_PIN   (default GPIO 6):  output, normally HIGH; pulse LOW ~200ms to press button
   - Both green+red active simultaneously = SERVICE MODE (10s hold hazard — never hold output low >9s)
   Module: epb_controller.c — GPIO init, state read, one-shot 200ms pulse timer
   UI: small colored dot near PRND row (green=released, red=applied, amber=service)
   "P" on M5Dial gear strip auto-engages EPB; D/R auto-releases.
   Safety rules (BOTH conditions required to allow engage):
   - speed < 5 km/h (guards against accidental high-speed lockup)
   - gear != D  (belt-and-suspenders — CAN fault could zero speed reading)
   Reboot safety: EPB_OUT_PIN configured OUTPUT HIGH as first act of epb_init(), before any gear
   logic runs. GPIO defaults to high-impedance on reset so there is no unsafe window at boot.
   Physical button remains mounted as emergency stop (software path intentionally conservative).
12. OpenVehicles / OVMS integration for remote telemetry
   - https://www.openvehicles.com/ — open-source vehicle monitoring platform with built-in LTE modem
   - Consider replacing or supplementing WiFi OTA/status with OVMS for remote telemetry, GPS tracking,
     remote commands, and cloud dashboard
   - OVMS speaks its own protocol over MQTT or direct TCP; may need a bridge or custom OVMS module
   - Evaluate: does OVMS CAN sniffing overlap with our existing CAN parser? Could share the bus.
   - Low priority; investigate when LTE connectivity becomes a requirement

## Splice CAD (EVJ-55 Wiring Diagram)

Project `EVJ-55` (UUID `17410eef-ffcd-4a2a-adb7-dab94271a8f4`) on splice-cad.com contains the full
vehicle wiring harness diagram. Edited via the `@splice-cad/mcp` MCP server in Claude Code.

### MCP Server Setup
- Configured in `~/.claude.json`, command: `/home/hpeyerl/.nvm/versions/node/v20.20.2/bin/node`
- Args: `["/home/hpeyerl/.nvm/versions/node/v20.20.2/bin/npx", "-y", "@splice-cad/mcp"]`
- Requires Node 18+ — system node is v12, must use nvm absolute path
- Agent bridge listens on **Linux:9876** (the MCP server). Browser connects to it via SSH local tunnel
  from Mac: `ssh -L 9876:localhost:9876 hpeyerl@<linux-box>` (NOT -R)

### Creating Visible Wire Connections via API

Two commands required — a link alone renders nothing on canvas.

**1. Generate IDs** (format: `link_<unix_ms>_<9char_alphanum>`, same for `cond_`):
```python
import time, random, string
ts = int(time.time() * 1000)
suffix = ''.join(random.choices(string.ascii_lowercase + string.digits, k=9))
```

**2. AddLinkCommand** — MUST include `id` or link is stored as `"undefined"` and never renders.
Do NOT include `sourcePinId`/`targetPinId` — that also causes `id: undefined`. Pin routing is
handled by the conductor, not the link.
```json
{"command": "AddLinkCommand", "params": {"link": {
  "id": "link_<ts>_<suffix>",
  "sourceNodeId": "comp_...",
  "targetNodeId": "comp_..."
}}}
```

**3. AddNewConductorCommand** — creates the visible wire with pin-level routing:
```json
{"command": "AddNewConductorCommand", "params": {"conductor": {
  "id": "cond_<ts>_<suffix>",
  "startEndpoint": {"nodeId": "comp_...", "pinId": "pin-..."},
  "endEndpoint":   {"nodeId": "comp_...", "pinId": "pin-..."},
  "linkPath": ["link_<ts>_<suffix>"],
  "color": "red",
  "gauge": "20 AWG"
}}}
```

**RemoveLinkCommand** (also removes its conductors):
```json
{"command": "RemoveLinkCommand", "params": {"link": {
  "sourceNodeId": "comp_...", "targetNodeId": "comp_..."
}}}
```

### Known Broken Commands (as of @splice-cad/mcp v0.4.0)
- `UpdateNodeCommand` — always fails: `"Cannot read properties of undefined (reading 'label')"`
- `UpdateNodePinsCommand` — same error
- `BulkEditPlanCommand` — `"undefined" is not valid JSON`
- Workaround: make these edits manually in the browser, then save

### Notes
- Ferrule nodes (X114 etc.) are created automatically by manual drawing as visual midpoints.
  API connections skip them — functionally equivalent.
- `AddNodeCommand` via bridge does not persist IDs to server until browser save. After save,
  fetch fresh `get_plan_summary` to get server-assigned component IDs.
- Cross-page connections work: link + conductor between components on different pages renders
  correctly on the page where the source component lives.
- Component names (e.g. "Zombie 10A", "Controls") are the canonical way to identify function.
  Labels (F11, K14) are positional designators.
