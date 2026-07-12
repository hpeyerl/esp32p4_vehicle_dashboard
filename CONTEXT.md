# EV Dashboard — Project Context

Last updated: 2026-06-18

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
| MagneRide CH4 PWM | 32 | Reserved — DNP on hat |
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
3. Arc gradient effect (Rivian-style) — see below
   Arc layout tightening — defer to real display
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
   ~~GPIO wiring + LED status reading~~ DONE (2026-06-17) — bench tested on real hardware,
   red LED correctly indicates brake applied, green LED correctly indicates brake released.
   Still open: UI dot, gear-automation safety logic (see below), EPB_OUT button-press testing.
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

## Waveshare Display Bring-Up — Debugging Log (2026-06-18)

Display is now physically wired to the P4-Nano. First real boot attempts on
`waveshare_usb` exposed several real bugs, fixed in this order:

1. **`scripts/patch_espidf_builder.py` idempotency bug** (fixed) — its
   wifi_sources patch re-applied on every single build, corrupting
   `managed_components/espressif__esp_wifi_remote/CMakeLists.txt` with
   duplicate blocks and an overly-broad regex that stripped
   `wifi_default_ap.c` from the wrong target, causing a "WiFi header
   mismatch" build error. Fixed the idempotency check and the regex; cleaned
   up the corrupted managed_components file.
2. **DSI PHY never powered** (fixed) — ESP32-P4's `VDD_MIPI_DPHY` rail needs
   an internal LDO channel (3, 2500mV) acquired via `esp_ldo_acquire_channel`
   before `esp_lcd_new_dsi_bus()`. Without it the PHY PLL lock loop in
   `esp_lcd_new_dsi_bus()` spins forever (no timeout in ESP-IDF's HAL).
   Added to `src/waveshare_display.c` `prv_panel_init()`.
3. **AXI-ICM DW-GDMA priority boost** (added, probably harmless/minor) —
   boosted DW-GDMA master-port arbiter priority via `axi_icm_ll_set_dw_gdma_qos_arbiter_prio()`.
   Added `hal`/`soc` to `src/CMakeLists.txt` PRIV_REQUIRES for this.
4. **PSRAM at 200MHz is unreliable on this board** (abandoned) — tried
   matching the working `tab5` branch's PSRAM/cache config
   (`CONFIG_SPIRAM_SPEED_200M` + `CONFIG_CACHE_L2_CACHE_256KB`, needs
   `CONFIG_IDF_EXPERIMENTAL_FEATURES`). DQS calibration is genuinely
   non-deterministic on THIS p4-nano — same firmware/config sometimes
   calibrates fine, sometimes hangs forever in an infinite
   "set to best phase: 0" retry loop. Also tried `CONFIG_SPIRAM_XIP_FROM_PSRAM`
   (from tab5) — hung even earlier, during flash→PSRAM segment copy. Not
   shippable; reverted to the stable `CONFIG_SPIRAM_SPEED_20M` default.
5. **Real bug: DISPON sent after video streaming already started** (fixed,
   but see open issue below) — `esp_lcd_hx8399`'s `panel_hx8399_init()` calls
   `hx8399->init(panel)` (which starts the DPI video engine) as its last
   step, inside `esp_lcd_panel_init()`. Our app then called
   `esp_lcd_panel_disp_on_off(s_panel, true)` *afterward*, sending a DBI
   command while video was already streaming. ESP-IDF's
   `mipi_dsi_hal_host_gen_write_dcs_command()` has an unconditional,
   timeout-free `while(cmd_fifo_full);` spin — if the DSI host never reopens
   a command window after video starts, this hangs the whole CPU core
   forever (eventually trips the task watchdog, repeating every 5s, no
   reset). Fix: moved the Display ON (0x29) command into
   `s_hx8399_init_cmds[]` itself (sent *before* `hx8399->init()` triggers
   video), removed the separate `disp_on_off()` call. See comments in
   `src/waveshare_display.c` around line ~170 and `prv_panel_init()`.

### Open issue / where we left off

The same `cmd_fifo_full` hang signature is **still occurring**, but now at a
different, earlier point: in `esp_lcd_panel_reset()`'s software-reset path
(since our `panel_cfg.reset_gpio_num = -1` — we do our own hardware reset via
GPIO27 in `prv_lcd_reset()` first — the HX8399 driver's `reset()` falls
through to sending `LCD_CMD_SWRESET` via DBI as a "software reset", which is
the very first DBI transaction of the whole boot). Log evidence: after a
hardware GPIO27 reset + 120ms settling delay, the very first DCS command sent
to the panel hangs in the FIFO-full wait — "send init commands success"
never even gets logged.

**Important confound**: all testing so far has used `esptool.py ... run`
(EN-pin pulse) between attempts, NOT a true power cycle. An EN-pin reset
resets the SoC's digital logic but does **not** power-cycle the display
panel — the panel's internal state (e.g. still mid-video-stream from the
previous boot) persists across an EN-only reset. This is suspected to
explain the apparent non-determinism (same as the PSRAM DQS flakiness):
behavior may depend on what state the panel was left in before each
EN-pulse reset, not on randomness in the SoC.

**Next step**: do a genuine **full power cycle** — disconnect both the
USB-C (P4-Nano) and the bench supply (display 5V/GND) completely, wait
~10s, reconnect both together, then capture the boot log. If "send init
commands success" and beyond now happens reliably, the EN-only-reset theory
is confirmed and the real fix is either (a) always testing via full power
cycles, or (b) adding a true panel power-down/up sequence (not just GPIO
reset) before DSI bus init when we can't trust a cold start.

**HX8399-C datasheet acquired**: full register-level datasheet (incl. Himax
internal SETGIP0-3 bit-field docs) saved as `HX8399-C_datasheet.pdf` in repo
root (source: https://dl.espressif.com/AE/esp-iot-solution/HX8399-C_DS_temporary_v00.06_150714.pdf).
Confirms 2-lane mode (`SETMIPI`/0xBA `LAN_NUM=01`) is a fully documented,
intended chip feature, independent of GIP timing (SETGIP0-3) — no documented
coupling between lane count and GIP register values. This substantially
weakens the "2-lane forces invalid GIP timing" theory raised during the
2026-06-18 session — the 4-lane-derived GIP array we're using should remain
valid in 2-lane mode. Still worth the Pi test to confirm empirically, but the
panel hardware/cable should no longer be the prime suspect for the
fifo-full hang — that's more likely still a P4-Nano-side sequencing/timing
issue, or the unverified custom DSI adapter cable.

**Separate wiring note**: the display backfeeds the P4-Nano's `ESP_3V3` net
through the DSI connector (display's local 3.3V regulator, fed from the
bench 5V, drives current backward onto the P4-Nano's normally-off onboard
3.3V regulator output whenever USB-C is unplugged — confirmed by the power
LED staying lit). Not fatal for now, but worth fixing later (power P4-Nano
from the same supply whenever bench-powering the display, or add a series
Schottky diode on that net) since two regulator outputs fighting risks
component stress over repeated sessions.

### DSI Adapter Cable Pinout (confirmed 2026-06-18, corrected 2026-06-19)

User's P4-Nano↔display cable is a custom adapter (15-pin/1mm-pitch FFC on the
P4-Nano end, 22-pin/0.5mm-pitch FFC on the display end) bought without a
pin-mapping table from the vendor. Confirmed both connectors' real pinouts
from the schematic (P4-Nano, labeled "15PIN--PI4B" in `ESP32-P4-NANO-schematic.pdf`)
and the display's official pinout diagram (Waveshare product page).

**Correction (2026-06-19)**: the P4-Nano pinout below was originally
mis-read by one position (D1 starting at pin 1 instead of pin 2). Herb
caught this live by re-checking the schematic directly — corrected version
below, also fixed in `HAT_CONTEXT.md`. The display's 22-pin pinout was
correct from the start; only the Nano side shifted.

**P4-Nano J1 (15-pin)**: 1,4,7,10,13=GND, 2,3=DSI_D1_N/P, 5,6=DSI_CLK_N/P,
8,9=DSI_D0_N/P, 11=ESP_I2C_SCL, 12=ESP_I2C_SDA, 14,15=3V3.

**Display (22-pin)**: 1=3V3, 2=I2C_SDA, 3=I2C_SCL, 4/7/10/13/16/19/22=GND,
5/6=RESERVE, 8/9=MIPI_D3_P/N, 11/12=MIPI_D2_P/N, 14/15=MIPI_CLK_P/N,
17/18=MIPI_D1_P/N, 20/21=MIPI_D0_P/N. Power (5V) is on a SEPARATE 8-pin
connector, not on this 22-pin DSI ribbon at all — display's actual 5V/GND
comes via a JST connector elsewhere on the board.

**Required cross-reference** (only 2 of the display's 4 lanes are used,
since P4-Nano only has D0/D1 wired):
| P4-Nano pin | Display pin | Signal |
|---|---|---|
| 2/3 | 18/17 | DSI_D1_N/P ↔ MIPI_D1_N/P |
| 5/6 | 15/14 | DSI_CLK_N/P ↔ MIPI_CLK_N/P |
| 8/9 | 21/20 | DSI_D0_N/P ↔ MIPI_D0_N/P |
| 11 | 3 | I2C_SCL |
| 12 | 2 | I2C_SDA |
| 14/15 | 1 | 3V3 |
| 1/4/7/10/13 | 4/7/10/13/16/19/22 | GND |

**Verified with a multimeter (2026-06-18) — both candidate cables are bad.**
Note: the physical buzz-out results themselves haven't changed, only which
signal name each physical pin maps to (per the 2026-06-19 correction above):
- The original Amazon 22-to-15 adapter has confirmed internal miswiring:
  P4-Nano pin 12 (`ESP_I2C_SDA`, not GND as originally misattributed) lands
  on Display pin 14 (`MIPI_CLK_P`), and P4-Nano pin 4 (`GND`, not CLK_N)
  lands on Display pin 8 (`MIPI_D3_P`). SDA landing on the display's actual
  clock pin is just as fatal to the DSI link as the original (incorrect)
  "GND on clock" description — conclusion unchanged, abandoned.
- A second cable (came bundled with a 3rd-party Pi camera) is genuinely
  1:1/straight, but its 15-pin end was very likely built to the classic
  Raspberry Pi 15-pin **CSI** ordering, not **DSI** — confirmed via RPi forums
  that 15-pin CSI and DSI use different pin orders from each other (unlike
  the newer 22-pin Pi5/CM4 generation, where CSI/DSI ARE pin-identical).
  P4-Nano's connector has D1/CLK/D0 at positions 2-3/5-6/8-9; Pi5/CM4's 22-pin
  standard has D0/D1/C at those relative positions — a lane-order mismatch
  consistent with "this cable matches CSI, not DSI." Not pursued further
  empirically since the conclusion was clear either way — abandoned.

**Decision: hand-build a custom breakout-to-breakout adapter** rather than
keep gambling on off-the-shelf cables with unstated internal wiring. Plan
(rainy-day project, not yet built as of 2026-06-18): two Proto-Advantage
FPC/FFC SMT breakout boards (one per connector pitch), short FPC pigtails
into each, hand-soldered jumpers between the two boards' SMT pads per the
table above. Practical notes for the build:
- **Differential pairs (D0, D1, CLK) — polarity matters.** Get P/N exactly
  right; a swap silently reintroduces the same class of bug as today.
- **GND**: don't wire 5-to-7 point-to-point. Bus all of P4-Nano's GND pins
  (1,4,7,10,13) together on that board, bus all of the display's GND pins
  (4,7,10,13,16,19,22) together on its board, single jumper between the two
  buses. Electrically identical, far less error-prone.
- **Leave unconnected**: display pins 5,6 (RESERVE), 8,9 (`MIPI_D3_P/N`),
  11,12 (`MIPI_D2_P/N`) — P4-Nano has no D2/D3 lanes at all.
- **Before trusting this for the actual build**: physically verify pin 1 /
  contact-side orientation on the real connectors, not just datasheet
  figures — datasheet pin numbering can differ from physical/silkscreen
  pin-1 marking. This exact class of mistake (an unverified assumption
  about pin numbering) is what cost a full day earlier in this session;
  the hand-built breakout board should confirm orientation against known
  Nano GND pins before any signal pin is trusted.
- See also `HAT_CONTEXT.md` → "DSI Pass-Through Connectors" for the
  PCB-trace version of this same routing table (longer-term fix: route
  DSI through the HAT itself instead of a hand-wired adapter).

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

### Creating Nodes and Pages via API

`AddNodeCommand` and `AddPageCommand` **must include an explicit `id` field** — without it the
element gets key `"undefined"` in the store and is non-functional even after save. Same fix as
`AddLinkCommand`. Generate IDs with the same `comp_<ts>_<9char>` / `page_<ts>_<9char>` format.
Pins also need explicit `id` fields (`pin-<8hex>`) to be wirable via conductors.

```json
{"command": "AddNodeCommand", "params": {"node": {
  "id": "comp_<ts>_<suffix>",
  "type": "component", "label": "VSS", "name": "VSS Reed Switch",
  "pins": [{"id": "pin-<8hex>", "label": "Signal"}, {"id": "pin-<8hex>", "label": "Gnd"}],
  "position": {"x": 825, "y": 450}
}}}
```

Newly created nodes land on whichever page was last "active" (determined by the most recent
command that touched a node on that page). To land on a specific page, first issue any
successful command involving an existing node on that page — this sets the active page context.

**Multiple wires between same two nodes:** Create ONE link, attach all conductors to it.
Creating multiple links between the same node pair triggers a "duplicate bundles" warning
(Splice CAD auto-merges with `MergeDuplicateLinksCommand`).

### Correct Command Params (fixed in @splice-cad/mcp upgrade 2026-06-05)

Use `mcp__splice-cad__describe_command` to look up exact params for any command.

**UpdateNodeCommand** — key is `newValues` (not `updates` or `node`):
```json
{"nodeId": "comp_...", "newValues": {"name": "New Name"}}
```

**AssignToPageCommand** — key is `nodeIds` array (not `nodeId` string):
```json
{"pageId": "page_...", "nodeIds": ["comp_..."]}
```

**MoveNodeCommand** — requires BOTH `oldPosition` and `newPosition`:
```json
{"nodeId": "comp_...", "oldPosition": {"x": 0, "y": 0}, "newPosition": {"x": 100, "y": 200}}
```

**AddNodeCommand** — now has `pageId` param, no separate AssignToPage needed:
```json
{"node": {"id": "comp_<ts>_<suffix>", ...}, "pageId": "page_..."}
```

**AddLinkCommand** — also has `pageId`:
```json
{"link": {"id": "link_...", "sourceNodeId": "...", "targetNodeId": "..."}, "pageId": "page_..."}
```

### Notes
- Ferrule nodes (X114 etc.) are created automatically by manual drawing as visual midpoints.
  API connections skip them — functionally equivalent.
- Cross-page connections work: link + conductor between components on different pages renders
  correctly on the page where the source component lives.
- Component names (e.g. "Zombie 10A", "Controls") are the canonical way to identify function.
  Labels (F11, K14) are positional designators.

### DashDisplay (ESP32-P4-Nano + Hat) — Wiring Status
All 14 pins wired as of 2026-06-02:

| Pin | Function | Connected to |
|-----|----------|-------------|
| 1 | Sw12v+ | F21 (Controls fuse) OUT |
| 2 | Gnd | Gnd bus |
| 3 | CANHi | CAN bus (existing) |
| 4 | CANLo | CAN bus (existing) |
| 5 | EPB Out | PBCtrl pin 5 (Button) |
| 6 | EPB Grn | PBCtrl pin 7 (LEDGreen) |
| 7 | EPB Red | PBCtrl pin 8 (LEDRed) |
| 8 | VSS | VSS Reed Switch (Signal) |
| 9 | MgRide CH1 | MgRideL (Out+) |
| 10 | MgRide CH2 | MgRideR (Out+) |
| 11 | MgRide CH3 | — (DNP) |
| 12 | MgRide CH4 | — (DNP) |
| 13 | Dimmer | Dimmer (Signal) |
| 14 | MagRide 12V+ | Bat+ |

F21 IN connected to IGN+ pin 10.

## Arc Gradient Design (SOC + Power arcs)

**Reference:** Rivian-style dashboard (see `2-1.png` in project root).

**Target effect:** The SOC and Power arcs have a fixed pre-set gradient underneath
(e.g., blue→white for SOC, amber→orange for power). A dark mask covers the "empty"
portion. As the value changes, the filled portion sweeps to reveal the gradient —
the leading edge acts as the pointer. The unfilled portion remains dim/dark.

**Current implementation:** Solid `lv_arc` that switches color at thresholds
(green→orange→red). This is wrong — entire arc recolors at each threshold.

**Chosen approach: multi-segment arc (Option 1)**
- Draw the arc as ~20 short arc segments, each color-interpolated along the gradient
- Only render segments up to the current value (empty portion left dark/undrawn)
- No LVGL gradient API needed — just `lv_draw_arc()` calls in a loop with lerp'd color
- Tune gradient stop colors against the real display once it arrives

**SOC arc gradient:** blue (#0080FF) → cyan (#00FFFF) → white (#FFFFFF) at 100%
**Power arc gradient:** amber (#FF8C00) → orange (#FF4500), mirrored for regen side
(exact colors TBD — tune visually on real display)

**Defer implementation until display is in hand** — segment count and colors need
visual tuning that can't be done headlessly.

## Waveshare Display Bring-Up — Debugging Log (2026-07-08)

**RESOLVED 2026-07-09:** `gvret` and `main` have been merged (commit `3a4007d`,
pushed to `origin/gvret`) — `gvret` now has both the GVRET/SavvyCAN sniffer
and all display bring-up fixes described below. Build (`waveshare_debug_usb`)
and flash verified working post-merge; DSI hang reproduces identically,
confirming no regression from the merge itself.

**Hardware status as of today:** Custom 15-pin(P4-Nano)↔22-pin(display) FPC
has arrived and is plugged in — no HAT built yet (display power/backlight
tested via direct bench 5V and separately via P4-Nano's onboard 5V0/GND,
both produce byte-identical results, ruling out supply capacity/quality as
a variable). Cable is Herb's own design: a straight-through but flipped
(mirrored) adapter based on the Pi5/CM4IO 22-pin DSI standard, cross-checked
in a prior conversation — connector divots confirm all pins seated well
mechanically. Cable wiring is NOT currently suspected.

**Symptom (unchanged across every attempt/power source):** Boot log
proceeds cleanly through I2C init, GT911 touch (I2C ACKs — `TouchPad_ID:
0x00,0x00,0x00`, `GT911 touch ready`), backlight PWM config, DSI PHY PLL
lock (`dsi_hal: phy pll: ... locked`), DPI panel + DW-GDMA + framebuffer
setup, and panel object creation (`hx8399: new hx8399 panel @0x...`) — then
hangs forever. Task watchdog fires repeatedly (every 5s) on IDLE0 with
nearly-identical register dumps each time (`S1`=panel handle pointer,
`A4=0x500a0000`=some DSI-host peripheral register, `MEPC` only 1-2
instructions apart between hits) — a tight polling loop, not a true crash.
No backlight ever visibly lights up.

**Root cause narrowed to:** `panel_hx8399_reset()` in
`managed_components/espressif__esp_lcd_hx8399/esp_lcd_hx8399.c:278-290`.
Because firmware passes `reset_gpio_num = -1` (no hardware reset line in
this cable design — by design, see DSI Adapter Cable Pinout above, P4-Nano
15-pin has no reset pin), reset falls through to the software-reset branch:
`esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0)`. This is the
**very first DBI transaction ever sent to the panel**, and it's exactly
where the hang occurs — nothing after `new hx8399 panel @...` in the log
ever executes. Confirms the host has never received any real acknowledgment
from the panel controller at any point in bring-up.

**Important correction:** the `hx8399: version: 1.0.5` log line is **not** a
hardware readback — it's `ESP_LOGI(TAG, "version: %d.%d.%d", ...)` printing
the driver component's own compile-time version constants
(`esp_lcd_hx8399.c:61`). Likewise the DSI PHY PLL lock is a host-local
reference-clock lock that doesn't require the panel's cooperation. So as of
today, **no log to date has ever confirmed genuine two-way DSI
communication with the physical panel** — every "success" line prior to the
hang is host-side only.

**Ruled out today:** cable wiring (verified design, good mechanical
contact), power source/capacity (bench 5V and P4-Nano 5V0 both hang
identically), and the theory that a missing hardware RESX line explains
this (deemed unlikely — Pi5/CM4IO standard doesn't carry one either and
official panels using this connector type work fine without it).

**Still open / next step:** isolate host (ESP32-P4 DSI driver/HAL) vs.
panel (dead/unresponsive hardware) by testing this exact panel + cable on
a real Raspberry Pi 5 or CM4IO board, which Herb has access to. If the
panel lights up there, the bug is in ESP-IDF's DSI host driver for this
2-lane config (the `mipi_dsi_hal_host_gen_write_dcs_command` spin-wait has
no timeout, so any real host-side protocol bug would present exactly like
this). If it stays dark on the Pi too, the panel module itself is suspect,
independent of all P4-Nano firmware/cable work.

**Ruled out 2026-07-09 — SDA/GND short.** Reviewed the cable's actual JLCPCB
production Gerbers + flying-probe test netlist (`FlyingProbeTesting.json`,
in `/tmp` from the fab order zip): JLCPCB's own net extraction correctly
separates `SDA`/`SCL` from `GND` as distinct nets feeding their prober (SDA
= top-connector PAD11 / bottom-connector PAD27, matching the documented
P4-Nano-pin-12 ↔ Display-pin-2 mapping). That's supportive but not
conclusive (no pass/fail numbers on hand, and it only covers the bare
fabricated board, not post-assembly defects). Physical multimeter check
confirms it directly: **no continuity between GND and either GPIO7 (SDA,
P1 header pin 3) or GPIO8 (SCL, P1 header pin 5), cable plugged in, board
powered off.** Combined with the earlier finding that the GT911 I2C read
genuinely ACKs (no NACK/error — see "Important correction" above), the I2C
bus itself is now considered electrically sound end-to-end. Neither the
GT911's blank product-ID read nor the DSI hang have an explanation yet —
both remain open, but wiring/shorts are no longer suspects for either.

**New finding 2026-07-10 — touch produces zero live data either.** Added a
standalone touch-only diagnostic (`ws_touch_diag_run()` in
`waveshare_display.c`, new `waveshare_touch_test` platformio env,
`-DTOUCH_ONLY_TEST=1`) that skips the DSI panel entirely (so it can't hang)
and just polls `esp_lcd_touch_read_data()`/`get_data()` every 100ms, logging
PRESSED/RELEASED + coordinates. Built, flashed, confirmed it boots cleanly
into the poll loop with no watchdog issue. User physically touched the
panel while this was running — **zero PRESSED events, ever.** Not a timing
miss; captured a live 25s window during an actual touch attempt.

**Implication:** this is the second independent subsystem (after DSI video)
that's electrically present/responding at the bus/PHY level but produces no
real functional output. Two unrelated subsystems failing identically is a
lot less likely than one shared cause. This promotes "the panel module
itself is dead/DOA" to the leading theory, ahead of "ESP32-P4 DSI driver
bug" specifically — and makes the planned Pi5/CM4IO cross-check even more
valuable, since it would now settle both the touch and DSI questions in one
test rather than just the DSI one.

**How to apply:** `waveshare_touch_test` env is a fast, low-risk way to
re-check touch independent of the DSI panel — reuse it if the panel gets
swapped/reflowed/reseated and touch needs re-verification without waiting
on the full display path.

**CORRECTION 2026-07-10 — logic analyzer shows SDA never ACKs, contradicting
the earlier "I2C read succeeds" conclusion.** Captured SDA (P1 pin3) + SCL
(P1 pin5) with a Saleae, exported raw edges to CSV. Verified programmatically
across the full ~20.4s capture (2014 edges): **SDA is high on every single
row — 0 rows where SDA=0, anywhere.** SCL shows two genuine bursts, 10.000s
apart exactly, ~504 clock pulses each over 17.2ms — a real master actively
attempting a substantial transaction (more than a simple register read),
getting zero acknowledgment. In I2C, no device pulling SDA low during 500+
clock pulses means nothing is answering, full stop.

This means the earlier claim ("GT911 I2C read genuinely ACKs, no NACK/error
— bus is electrically sound end-to-end") was **wrong**, or no longer
reflects the current physical setup. Physical/electrical evidence overrides
the earlier software-log-based inference. Most likely explanation: ESP-IDF's
I2C driver isn't reliably surfacing a NACK as an error on this path (so the
earlier "success" was actually a silent NACK — consistent with the all-zero
data it returned every time), though a changed physical setup since that
earlier log can't be ruled out either.

Open question as of this correction: which firmware was flashed during this
capture — the 10.000s-exact burst interval doesn't match
`waveshare_touch_test`'s 100ms poll loop, so needs to be confirmed before
reading too much into the burst size/timing specifically. The core finding
(nothing ACKs on this I2C bus, ever) holds regardless of which firmware
produced it.

**Net effect:** touch is now suspect at the same "nothing electrically
responds" level as DSI — reinforcing, not weakening, the "dead panel
module" theory. Don't cite the old "bus is electrically sound" line above
as current — this correction supersedes it.

**Open thread 2026-07-10 (interrupted, not yet resolved):** confirmed the
`waveshare_touch_test` firmware (the one that produced the logic-analyzer
capture above) was indeed on the board. Its poll loop uses
`vTaskDelay(pdMS_TO_TICKS(100))` between calls — that constant is correct,
not a scaling bug. The mismatch (10.000s-exact burst period, ~504 pulses/
burst, vs. an expected ~10Hz poll of small NACK'd transactions) is more
likely explained by the I2C transaction itself blocking for a long time
before failing — e.g. an unset/default `xfer_timeout_ms` on the
`esp_lcd_panel_io_i2c_config_t` in `prv_touch_init()`
(`waveshare_display.c`, no explicit timeout field set), or some retry/bus-
recovery behavior in ESP-IDF's i2c_master driver — rather than anything in
our polling loop. **Was actively checking the default I2C transaction
timeout in the ESP-IDF i2c_master/esp_lcd_io_i2c source when the session
was interrupted (user had to leave for work) — not yet confirmed.** Next
session: finish checking whether `tp_io_cfg` needs an explicit short
timeout set, and whether setting one changes the burst period back to
~100ms (which would confirm the theory) or reveals something else.

**RESOLVED 2026-07-10 (after user left):** confirmed via the actual compiled
source (`compile_commands.json` → resolved
`~/.platformio/packages/framework-espidf/components/esp_lcd/include/esp_lcd_io_i2c.h`):
`esp_lcd_panel_io_i2c_config_t` **has no `xfer_timeout_ms` field at all** in
this ESP-IDF version — it's not application-configurable, so this was never
an "unset timeout" bug in our code specifically. The real answer is in
`esp_lcd_panel_io_i2c_v2.c:145,147` (same component): every I2C transaction
the esp_lcd I2C backend issues calls `i2c_master_transmit_receive(...)` /
`i2c_master_receive(...)` with a **hardcoded literal `-1`** for the timeout
argument — meaning "block forever," no software timeout applied by this
layer at all, ever, for any device.

So each `esp_lcd_touch_read_data()` call isn't failing fast on a NACK the
way a normal I2C transaction would — it's blocking for a long time (~10s
observed) before whatever lower-level mechanism (I2C peripheral hardware
timeout registers, or internal `i2c_master` driver retry/recovery state,
below the esp_lcd layer and not further chased) finally gives up and
generates the ~504-pulse burst we captured. Our 100ms `vTaskDelay` between
poll attempts is real and correctly coded, but irrelevant in practice —
completely dwarfed by the ~10s each blocking call actually takes.

**Also reframes the earlier correction above:** if a non-responding GT911
now takes ~10s to fail per attempt, that's a materially different symptom
than the original (days-old) log where `touch_gt911_read_cfg()` returned
quickly (whether that was a genuine success or a fast silent NACK, it
wasn't a multi-second stall either way). That's a real behavior change
between sessions, not just a reinterpretation of the same evidence — worth
asking the user whether anything physically changed (reseated connector,
different power-up order, etc.) between the two observations, since it's
one more data point and not yet explained.

**Practical fix, not yet applied:** nothing to configure on our side via
`esp_lcd_panel_io_i2c_config_t` (no timeout field exists). If faster/cleaner
failure behavior is wanted for future diagnostics, would need to either (a)
bypass `esp_lcd_new_panel_io_i2c()`/GT911 driver entirely and issue raw
`i2c_master_transmit_receive()` calls directly with an explicit short
timeout, or (b) accept the ~10s-per-failed-attempt behavior as inherent to
using the stock `esp_lcd_touch_gt911` + `esp_lcd_io_i2c` v2 stack on a
non-responding device. Not done — low priority, doesn't change the
underlying "nothing answers on this bus" finding either way.

## BREAKTHROUGH 2026-07-11 — panel and cable are exonerated. It's an ESP32-P4 host-side bug.

The Pi5/CM4IO cross-check finally ran (after an unrelated Pi5 boot saga —
bad initial power source, USB3 hub port instead of a real PD supply,
fixed by switching to a Surface Pro PD charger). Result, direct from the
user: **"display works fine under pi5. Touch also."**

This is conclusive. The exact same panel, same custom FPC cable, same
GT911 touch controller — fully functional on a Pi5. That overturns the
"dead panel module" theory that every session since 2026-07-08 had been
converging on. It was wrong. The panel and cable are both healthy.

**What this means:** both failures we spent days chasing are 100%
ESP32-P4-side (firmware/HAL/driver/electrical-drive) problems, not
hardware defects:

1. **DSI hang** — `panel_hx8399_reset()`'s first DBI transaction
   (`LCD_CMD_SWRESET`) never completes on the ESP32-P4, hanging forever in
   `mipi_dsi_hal_host_gen_write_dcs_command`'s timeout-free spin-wait (see
   entries above). Since a different host drives this same panel over DSI
   successfully, suspect ESP-IDF's DSI HAL/PHY config for this specific
   2-lane HX8399-C setup — the `WS_DSI_LANE_MBPS=950`/`WS_DPI_CLK_MHZ=75`
   timing parameters, the 2-lane-specific init sequence, or a genuine
   ESP-IDF bug in `esp_lcd_hx8399`/the DSI HAL for this configuration.

2. **I2C touch never ACKs** — logic analyzer proved SDA never goes low
   across a full capture on the ESP32-P4 (see 2026-07-10 entries above).
   Since the same GT911 chip responds fine to the Pi5's I2C controller,
   suspect the ESP32-P4 side's electrical I2C drive specifically —
   `prv_i2c_init()` in `waveshare_display.c` relies on internal weak
   pull-ups only (`flags.enable_internal_pullup = true`, no external
   pull-up resistors), at `scl_speed_hz = 400000`. A Pi5's board-level I2C
   pull-ups are likely stronger/better-tuned than the ESP32-P4's internal
   ones, and this custom cable adds trace length/capacitance beyond either
   host's original reference design. Worth trying: drop to 100kHz, and/or
   add real external pull-up resistors on SDA/SCL rather than relying on
   the ESP32-P4's internal ones.

**Ruled out for good, do not re-open:** cable wiring, SDA/GND short, power
source/capacity, panel/touch-chip hardware health. All closed. The only
open question now is what specifically ESP-IDF/the ESP32-P4 is doing wrong
on both the DSI and I2C paths — two separate host-side bugs (or possibly
one shared root cause, e.g. a marginal PHY/electrical issue affecting both
buses) to chase from here.

**Pi5 dmesg (`/tmp/foo`) — precise confirming evidence, not just qualitative "it works":**

```
Goodix-TS 11-005d: supply AVDD28 not found, using dummy regulator
Goodix-TS 11-005d: supply VDDIO not found, using dummy regulator
Goodix-TS 11-005d: ID 9271, version: 1060
input: Goodix Capacitive TouchScreen as .../i2c-11/11-005d/input/input5
```
Bus `11`, address `0x5d` — the exact same I2C address our ESP32-P4 code
uses (`WS_GT911_I2C_ADDR = 0x5D`). On the Pi it returns a real chip ID
(GT**9271**, a GT911-family variant) and firmware version 1060. On the
ESP32-P4, the same read returns `0x00,0x00,0x00`/version 0. Same chip,
same address, valid data on one host, nothing on the other — about as
clean a same-hardware A/B as this investigation could produce.

**Bonus, previously-unknown fact:** there's a second, separate I2C device
on this panel — a "display MCU" at address `0x45`:
```
display_mcu@45 ... waveshare panel hw id = 0x1, panel size = 123, mcu version = 0x1
```
Distinct from the GT911 touch controller at `0x5d`. Almost certainly what
feeds Raspberry Pi's `display_auto_detect=1` mechanism (panel
identification). Not used/relevant to our ESP32-P4 code path, just
newly-documented panel architecture.

**Update — 2-lane operation of this exact panel is vendor-validated, not
just theoretically possible.** Initial pass at this (below, superseded)
wrongly leaned on the overlay filename (`...12_3_inch_a_4lane`) appearing
on both the DSI0 and DSI1 config.txt lines and concluded that didn't prove
2-lane support. That was the wrong piece of evidence to lean on. The real
evidence: the CM4IO schematic (reviewed in an earlier session, and CM4IO
is confirmed to share the same DSI connector layout as Pi5) shows its DSI0
connector only has `DSI0_D0_N/P` and `DSI0_D1_N/P` wired — physically
2-lane, at the hardware/schematic level, independent of any overlay
naming. Waveshare's own recommended `config.txt` for this exact panel
ships a documented option to run it through that same DSI0 connector:
```
dtoverlay=vc4-kms-dsi-waveshare-panel-v2,12_3_inch_a_4lane,dsi0
```

**Checked the actual driver source** (`panel-waveshare-dsi-v2.c`, from
`github.com/raspberrypi/linux` branch `rpi-6.18.y` — same file our own
`ws_panel_12_3_a_4lane_init[]`/`ws_panel_12_3_a_4lane_mode` arrays in
`waveshare_display.c` were originally sourced from in an earlier session)
to settle this properly rather than argue from the overlay filename. Two
things are true and initially looked like they were in tension:

1. This driver defines only **one** descriptor for the 12.3" panel,
   hardcoded `.lanes = 4` (`ws_panel_12_3_inch_a_4lane_desc`), unlike
   several other panel sizes in the same file (10.1", 9", 8", 8.8") which
   *do* have distinct 2-lane descriptors alongside their 4-lane ones.
   Lane count is applied unconditionally at attach
   (`dsi->lanes = ctx->desc->lanes;`, line 2210) — no devicetree override,
   no adaptation based on which connector (dsi0/dsi1) it's bound to. This
   exactly matches the Pi5's dmesg (`lanes: 4`) from the successful DSI1
   run. So Waveshare's *Linux driver* does not demonstrate genuine 2-lane
   operation of this panel — running it via DSI0 with this driver would
   request 4 lanes on a connector the CM4IO schematic shows only has 2
   wired, with no code-level adaptation. Whether that's actually been
   verified to work by anyone is unknown.

2. **This doesn't matter for our approach, and isn't a blocker.** Our own
   `waveshare_display.c` already anticipated exactly this gap (from
   whichever earlier session ported the init array) — see line 166:
   `// NOTE: SETMIPI (0xBA) is sent by esp_lcd_hx8399 component based on
   lane_num.` We deliberately borrow *only* the GIP/gamma/calibration DCS
   commands from Waveshare's array and explicitly exclude the lane-count
   command from it (also line 158-159: "Do NOT include it here — the
   component handles lane configuration"). Lane count itself is handled
   entirely separately via ESP-IDF's `esp_lcd_hx8399` component
   (`hx8399_vendor_config_t.mipi_config.lane_num = WS_DSI_LANE_NUM`),
   which builds and sends its own SETMIPI/2-lane command, independent of
   whatever Waveshare's Linux driver does or doesn't support. Our 2-lane
   approach was never dependent on Waveshare's Linux driver validating
   2-lane — it's grounded directly in the HX8399-C datasheet's own
   documented chip-level 2-lane capability (SETMIPI/0xBA LAN_NUM field,
   established 2026-06-18) plus ESP-IDF's dedicated mechanism for it.

**CONFIRMED EMPIRICALLY 2026-07-11 — 2-lane DSI works fine on this exact
panel.** User tested it directly: **"I can confirm the display works just
fine on DSI0."** DSI0 is the schematically-2-lane-wired connector (CM4IO
schematic). So this is no longer a theoretical/driver-analysis question —
2-lane DSI operation of this exact panel is empirically proven on known-
good host hardware, on top of the datasheet-level chip capability already
established 2026-06-18. Interesting (unexplained, not worth chasing)
detail: the driver requests `.lanes = 4` unconditionally regardless of
connector (confirmed from source above), yet it still works correctly
over a 2-lane-wired connector — suggests either the RP1 DSI host silently
only drives whatever's physically wired, or the HX8399-C panel itself
auto-detects active lane count at the PHY level rather than requiring an
explicit host-side lane-count command. Not confirmed which; doesn't
matter for our purposes.

**Net, correct conclusion:** 2-lane DSI is a solved, working
configuration on this panel — not a variable to keep questioning. The
DSI hang is 100% an ESP32-P4/ESP-IDF-side bug. Combined with the
`display_mcu@0x45` power-sequencing discovery (below), that's now the
leading suspect for root cause on BOTH the DSI hang and the I2C-never-
acks finding — a panel/touch-chip held in reset because nothing on the
ESP32-P4 side ever talks to 0x45 to release it.

Full dmesg saved by user at `/tmp/foo` on the dev box if deeper detail is
needed later (e.g. DPI/byte clock values:
`Nominal Byte clock 71250000 DPI clock 94999673`, from the Pi's 4-lane
run — note these specific clock values are 4-lane-derived and won't
transfer directly to a 2-lane config). Driver source saved locally at
`/tmp/claude-1000/.../scratchpad/panel-waveshare-dsi-v2.c` for this
session — re-fetch from the GitHub URL above if needed in a future one.

## POSSIBLE ROOT CAUSE FOUND 2026-07-11 — `display_mcu@0x45` GPIO-expander/power-sequencer, never talked to on ESP32-P4

User pulled the full decompiled devicetree overlay (`vc4-kms-dsi-waveshare-panel-v2.dtbo`, saved at `/tmp/ws.dts` on the dev box) and it reveals panel architecture we had no prior knowledge of. Fragment 0:

```c
display_mcu@45 {
    compatible = "waveshare,touchscreen-panel-regulator";
    reg = <0x45>;
    gpio-controller;
    #gpio-cells = <0x02>;
    enable-gpio = <0x01 0x02 0x00>;
    phandle = <0x01>;
};

goodix@5d {
    compatible = "goodix,gt9271";
    reg = <0x5d>;
    reset-gpio = <0x01 0x09 0x00>;   // phandle 0x01 = display_mcu
    phandle = <0x05>;
};
```

Fragment 1:
```c
dsi_panel@0 {
    compatible = "waveshare,10.1-dsi-touch-a";  // (generic string in this overlay skeleton; actual compatible comes from the override, e.g. 12_3_inch_a_4lane)
    reset-gpio = <0x01 0x01 0x00>;   // phandle 0x01 = display_mcu
    iovcc-gpio = <0x01 0x04 0x00>;   // phandle 0x01 = display_mcu
    avdd-gpio  = <0x01 0x00 0x00>;   // phandle 0x01 = display_mcu
    ...
};
```

**The `display_mcu@45` chip is declared a `gpio-controller`** (standard
`#gpio-cells = <2>` pin+flags binding). Every `reset-gpio`/`iovcc-gpio`/
`avdd-gpio` reference above — for BOTH the DSI panel AND the GT9271 touch
controller — points at phandle `0x01`, i.e. this MCU. None of these are
native SoC GPIOs. This chip is an I2C-addressed power-sequencer/GPIO-
expander for the whole panel module: it gates AVDD, IOVCC, panel reset,
and touch-controller reset.

**Our ESP32-P4 firmware has never talked to I2C address `0x45` at all.**
We power the panel via a simple buck-enable GPIO (`HAT_DISP_PWR_EN`) and
reset the DSI panel purely via software `SWRESET` with
`reset_gpio_num = -1`; the GT9271 gets no explicit reset handling either.
If this MCU is what's actually supposed to release AVDD/IOVCC and
de-assert reset on both the panel and the touch controller — and we never
send it anything — that would cleanly explain **both** long-standing
failures with a single missing step: a panel held in reset never responds
to any DSI command; a touch controller held in reset never ACKs on I2C.

Full overlay source saved at `/tmp/ws.dts` on the dev box.

## IMPLEMENTED 2026-07-11 — MCU power-sequencing driver added to waveshare_display.c (build-verified, NOT YET hardware-tested)

Found the exact driver: `drivers/regulator/waveshare-panel-regulator.c`
(`raspberrypi/linux`, branch `rpi-6.18.y` — matches our dmesg's
`waveshare_touchscreen 11-0045` exactly, both the `.driver.name` and the
`compatible` string). Full protocol:

**Registers** (8-bit reg, 8-bit val, plain I2C, addr `0x45`):
- `0x94` (`REG_TP`) — GPIO output state, bits 8-15 (high byte)
- `0x95` (`REG_LCD`) — GPIO output state, bits 0-7 (low byte)
- `0x96` (`REG_PWM`) — backlight brightness 0-255
- `0x97` (`REG_SIZE`) — read-only, panel size (12.3" reads 123)
- `0x98` (`REG_ID`) — read-only, hw id
- `0x99` (`REG_VERSION`) — read-only, mcu fw version

16-bit GPIO state, write-only, **no per-bit register** — every GPIO change
requires rewriting both `REG_TP` and `REG_LCD` together (kernel driver
keeps a shadow `poweron_state`; we do too, `s_mcu_gpio_state`).

**Pin assignments** (from `/tmp/ws.dts`): bit 0 = AVDD, bit 1 = LCD reset,
bit 2 = backlight enable (BL_EN), bit 4 = IOVCC, bit 9 = GT911 touch reset.
Bits 8+9 get set unconditionally at MCU probe ("Enable VCC" — this is the
only place upstream ever touches touch-reset, no pulse, just released high
once).

**Panel power-up sequence** (from `ws_panel_prepare()` in
`panel-waveshare-dsi-v2.c`, confirmed working on real Pi5 hardware): IOVCC
high → 20ms → AVDD high → 20ms → reset low → 60ms → reset high → 60ms →
then DCS init commands.

**Implemented in `src/waveshare_display.c`:**
- `prv_mcu_write_state()` / `prv_mcu_set_gpio()` / `prv_mcu_read_reg()` —
  low-level register helpers, raw `i2c_master_transmit()`/
  `i2c_master_transmit_receive()` (NOT the `esp_lcd_io_i2c` v2 backend —
  avoids that component's known hardcoded `-1`/block-forever timeout, see
  2026-07-10 entries above).
- `prv_mcu_init()` — phase 1: adds the I2C device, reads/logs
  ID/SIZE/VERSION for diagnostic confirmation, sets bits 8+9 (releases
  GT911 touch reset). Called in `ws_display_init()` between `prv_i2c_init()`
  and `prv_touch_init()`, and in `ws_touch_diag_run()` (the standalone
  touch test) in the same position — that diagnostic would otherwise still
  fail identically, since it never touched the MCU either.
- `prv_mcu_panel_power_on()` — phase 2: IOVCC/AVDD/reset sequence + BL_EN,
  exact timing match to upstream. Called from `prv_panel_init()`
  immediately after `esp_lcd_new_panel_hx8399()` creates the panel object,
  before `esp_lcd_panel_reset()`/`esp_lcd_panel_init()`.
- `platformio.ini`: removed `-DWS_TOUCH_RST=24` (was toggling a native
  GPIO that has nothing to do with the real touch-reset line, now known to
  be MCU bit 9 — changed to `-1`/disabled) and the already-dead
  `-DWS_LCD_RESET=27` flag (unused since `reset_gpio_num=-1` was adopted
  in an earlier session; harmless but stale).

**Build-verified:** both `waveshare_debug_usb` and `waveshare_touch_test`
compile clean.

**Partially flash-tested 2026-07-11 — with the display physically
disconnected (not yet a real test of the fix).** Flashed and captured
boot serial. Result: `prv_mcu_init()` correctly fails fast with a real
I2C error (`MCU write REG_TP failed`) when nothing answers at `0x45`, and
`ws_display_init()` propagates that as fatal (`ESP_ERROR_CHECK` upstream
in `app_display_init()`), so the board clean-aborts and reboots every
~5s. **This is expected/correct given no display was attached** — nothing
is at that address to ACK. The one genuinely useful signal from this run:
**no hang** — a fast clean error + reboot loop, categorically different
from the old DSI watchdog-spam hang. Good sign for the error-handling
path itself, but this run does NOT validate whether the MCU sequence
actually fixes anything — that needs the display physically connected.

**Known limitation surfaced by this test, decision deferred:** MCU/panel
init failure is currently fatal to the whole app (no CAN/WiFi/etc. either)
if the display is ever disconnected/faulty. Could be made non-fatal
(log + continue headless, same pattern as touch-failure handling) but
that's a real behavior change for field operation — asked the user,
they said hold off until the display is reconnected and properly tested
first. Revisit only if asked.

**REAL HARDWARE TEST RESULT 2026-07-11 — partial win.** Flashed with
display genuinely connected (bench-powered — see power note below), full
boot captured.

**Display MCU: fully confirmed working, byte-for-byte match to Pi5.**
```
I (4930) ws_disp: init: display MCU (addr 0x45)
I (4935) ws_disp: display MCU hw id = 0x01
I (4939) ws_disp: display MCU panel size = 123
I (4943) ws_disp: display MCU fw version = 0x01
I (4968) ws_disp: display MCU init complete (touch reset released)
...
I (5670) ws_disp: display MCU panel power sequence complete
```
`hw id`/`panel size`/`fw version` are identical to the Pi5's dmesg values.
Our reverse-engineered I2C protocol for this chip is proven correct on
our own hardware, not just in theory.

**Real, visible, first-ever result: backlight works.** User confirms
backlight is on, with a dim uniform glow visible against the black
bezel — exactly what you'd expect from a powered backlight with no video
signal ever loaded (LC layer sits in default/unmodulated state). This
panel has never shown ANY visible response before today. Genuine forward
progress, independent of the DSI issue below.

**DSI hang: still present, identical signature, unchanged by the fix.**
Right after `display MCU panel power sequence complete`, watchdog fires
every 5s exactly as before — `S1=0x4ff3b3c8` (panel handle), `A4=0x500a0000`
(same DSI-host register), same spin-wait. **This is a meaningful negative
result, not a wasted effort:** it rules out power/reset sequencing as the
DSI hang's cause. The panel is now confirmably powered and reset
correctly (matching the exact working Pi5 sequence) and DSI still never
responds — this re-focuses the DSI problem squarely onto the ESP32-P4's
actual DSI PHY/protocol handling (timing params, 2-lane config, or a
genuine ESP-IDF driver bug), not a "panel never woke up" explanation.
Touch/backlight both being fixed by the same change while DSI is
unaffected shows these are genuinely separate root causes, not one shared
issue as originally hypothesized.

**Touch: still `TouchPad_ID:0x00,0x00,0x00`, unchanged.** Reset is now
genuinely released (confirmed via MCU), but GT911 still doesn't respond.
Worth a fresh logic-analyzer capture on SDA/SCL now that reset is real —
per the 2026-07-10 finding, previously SDA never ACKed at all; re-checking
whether that's changed now that reset is properly handled is the natural
next diagnostic step. Also worth trying a longer delay between touch-reset
release and the first GT911 read (currently ~20ms, matching upstream's
tight timing, but real-world Linux driver-probe ordering likely gives
GT9271 far longer in practice before its own driver ever touches the bus).

**Important process note — P4-Nano 5V0 cannot power the display.**
Backlight/panel now draws real current once actually powered on, and
running the display off the P4-Nano's own 5V0 rail browned out the whole
board (USB serial dropped mid-boot, right after backlight/panel power-on
— exactly where current draw spikes). **Always bench-power the display
separately from the P4-Nano when testing this fix; do not reuse 5V0
now that the panel is genuinely drawing current.**

**If this doesn't fully fix it:** the MCU sequence is a faithful port of
upstream's exact register writes and timing, but two things weren't
verified and are worth checking if it's still not working:
1. Whether `s_mcu_gpio_state`'s bit-8 (set alongside bit-9 at MCU init,
   meaning/purpose undocumented in the kernel driver beyond "Enable VCC")
   matters for us specifically — we replicate it faithfully but don't
   know what it does.
2. Whether the mainline Goodix touchscreen driver
   (`goodix,gt9271` compatible, separate from the MCU driver) does its own
   additional reset pulse on bit 9 beyond the MCU's one-time release — we
   only replicate the MCU driver's behavior, not any possible additional
   pulsing the standard Linux Goodix input driver might do independently.
   If GT911 still doesn't respond, this is the next thing to check (may
   need `esp_lcd_touch_gt911`'s own reset handling redirected through the
   MCU too, rather than left disabled).

## DSI hang deep-dive 2026-07-11 (after MCU fix confirmed power/reset isn't the cause)

**Touch settle-time experiment — negative result.** Bumped the delay
between releasing GT911's reset (MCU bit 9) and the first touch register
read from 20ms to 300ms (`prv_mcu_init()` in `waveshare_display.c`).
**No change** — still `TouchPad_ID:0x00,0x00,0x00`. Rules out "just needs
more settle time" as the touch explanation. Left the 300ms delay in place
(harmless, matches the spirit of giving the chip more time even though it
didn't fix this specific symptom) — worth reverting to something shorter
if it's confirmed truly irrelevant later, but not urgent.

**This IS a known, community-reported ESP-IDF bug — not our config.**
Confirmed via GitHub issues **espressif/esp-idf#15137** (IDFGH-14356) and
**#15358** (IDFGH-14601), both describing the *exact* same hang: stuck in
`mipi_dsi_hal_host_gen_write_dcs_command()` at
`while (mipi_dsi_host_ll_gen_is_cmd_fifo_full(hal->host));` — this checks
`cmd_pkt_status.gen_cmd_full`, a raw register bit from the underlying
Synopsys DesignWare MIPI DSI Host Controller IP (used across many chip
vendors, not ESP32-P4-specific hardware). **This spin-wait is entirely
host-side/local** — it checks the ESP32-P4's own internal command FIFO
fullness, not anything requiring a panel response. If it never clears,
the ESP32-P4's own DSI command-transmission engine is stalled internally,
independent of whether the panel is listening at all.

Issue #15137 (ILI9881C panel, different from ours) is marked
**"Resolution: Done" / "Status: Done"** by Espressif, confirmed affecting
v5.3.2/v5.4.0/v5.5.0(master) — our board runs **ESP-IDF 5.4.2**, squarely
in the affected range. Their reported trigger was different from ours
though: hangs after the **17th** consecutive `esp_lcd_panel_io_tx_param()`
call, not the first — we hang on the very first (`SWRESET`, via
`panel_hx8399_reset()`). Same underlying bug class, different trigger
threshold — plausibly command-count/timing-dependent rather than a fixed
condition.

**Community-reported workaround (untested by us, and I have real
reservations about it):** commenting out
`mipi_dsi_host_ll_gen_set_packet_header(...)` (the line right after the
hanging wait, `mipi_dsi_hal.c:160`) reportedly "prevents the program from
getting stuck." Note this is in **ESP-IDF's own framework source**
(`~/.platformio/packages/framework-espidf/components/hal/mipi_dsi_hal.c`
on this box), not our repo — same category of thing as the existing
`scripts/patch_espidf_builder.py` (which already patches a different
component for a different reason). Reservation: deleting that line
doesn't just skip the wait, it skips **sending the command entirely,
forever** for whichever call hits it — not obviously safe to do
unconditionally on our very-first-command case, versus their
17th-command case where 16 prior commands already succeeded normally.
**Not applied — needs your judgment call before patching framework code,
that's a materially different risk category than editing our own
`src/` files.**

**No official fix version identified.** Couldn't find the specific
ESP-IDF release/commit that resolved IDFGH-14356. Search also surfaced
**other, seemingly-different** ESP32-P4 DSI issues reported specifically
*against* v5.5.x (#17805, #18083, plus one report of code "working
perfectly on v5.4.2 but producing empty screen/display issues on
v5.5.X") — so blindly upgrading isn't a safe slam-dunk, it risks trading
this bug for a different one others are hitting on newer versions.
**Not attempted** — a version bump is a bigger, more disruptive change
than anything else done today and deserves an explicit decision, not an
autonomous one.

**Ruled out: "skip `esp_lcd_panel_reset()`, rely on our real MCU hardware
reset instead."** Checked `panel_hx8399_init()` source directly
(`esp_lcd_hx8399.c:212-214`) before trying this — it sends its **own**
first DBI command (`HX8399_CMD_PAGE`/`HX8399_CMD_CLOSE`) as its very
first action, through the identical `esp_lcd_panel_io_tx_param()` →
`mipi_dsi_hal_host_gen_write_dcs_command()` path that hangs on SWRESET.
Skipping `esp_lcd_panel_reset()` would just move the identical hang one
step later, onto a different command byte — not a fix. Saved a wasted
flash/test cycle by checking source first.

**Ruled out: "just needs more delay before the first command."** We
already have ~160ms of real elapsed time (the full MCU power sequence —
20+20+60+60ms) between DSI-PHY-PLL-lock and the first SWRESET attempt.
If this were a simple hardware-readiness race, 160ms should be far more
than enough. It isn't fixing it, so this isn't a simple race condition.

**Comparison against Tab5 (this same repo, ESP32-P4, confirmed working
before — commit `efb837e` "First working version with data changing on
screen in real time"):** structurally very close to our setup — 2-lane
DSI, 965Mbps vs our 950, 70MHz DPI vs our 75MHz, `reset_gpio_num=-1`,
*also* uses an external I2C GPIO expander (PI4IOE5V6416 @ 0x43) for
panel/touch reset release, same general call pattern
(`esp_lcd_panel_reset()` then `esp_lcd_panel_init()`). Key difference:
Tab5 (ST7123 panel, different `esp_lcd_st7123` component) explicitly
calls `esp_lcd_panel_disp_on_off(s_panel, true)` after init — something
we deliberately do NOT do (established 2026-06-18: HX8399 component's
`init()` starts DPI video streaming as its last internal step, so a
disp_on_off() call afterward hits the same class of hang on a *later*
command). Tab5 not hitting the SWRESET-stage hang despite very similar
DSI parameters suggests the bug is specific to something about the
`esp_lcd_hx8399` component/command sequence, not an unconditional
ESP32-P4 platform-wide failure — consistent with the "17th command"
vs "1st command" difference in the two GitHub issues also being
command-sequence-dependent rather than a fixed, universal trigger.

**Where this leaves things:** DSI hang is confirmed to be a genuine,
external ESP-IDF/ESP32-P4-silicon-or-HAL bug (not our wiring, power,
config, or a fixable application-level sequencing issue — those are all
now checked and ruled out). Two real paths forward, both requiring a
judgment call rather than autonomous action:
1. Try the community's framework-patch workaround, accepting the risk
   that it may just silently skip sending a real command rather than
   fixing anything.
2. Try upgrading to a newer ESP-IDF version, accepting the risk of
   trading this bug for a different, newer-version-specific DSI issue
   others have reported.
Neither attempted — flagged for the user's decision.

## ESP-IDF version upgrade attempt 2026-07-11 — tried, reverted, real build-system incompatibility found

User asked to try option 2 above (upgrade ESP-IDF) and see what changes.
Bumped `[p4_base]`'s `platform =` in `platformio.ini` (shared across
*all* environments — waveshare/tab5/stub, not just this target) from
pioarduino release `54.03.21-2` (ESP-IDF 5.4.2, current) to `55.03.39`
(ESP-IDF 5.5.4, latest stable per pioarduino's version-mapping repo
`sivar2311/platform-espressif32-versions`).

**Attempt 1** (kept the pinned `platformio/toolchain-riscv32-esp @
14.2.0+20241119`): failed immediately — `Error: Missing toolchain
directory 'None'`. That toolchain pin doesn't resolve against the new
platform version. Also saw `patch_espidf_core.py exited with error`
(one of our own compatibility scripts, `scripts/patch_espidf_builder.py`,
choking on the new platform's file layout) — a second, separate
compatibility problem.

**Attempt 2** (removed the toolchain pin, let PlatformIO auto-select):
toolchain resolved fine this time, bootloader compiled and linked
successfully — but the **main firmware link failed**:
```
undefined reference to `_bss_start_low'
undefined reference to `_bss_end_low'
undefined reference to `_bss_start_high' / `_bss_end_high'
undefined reference to `_data_start_high' / `_data_start_low'
undefined reference to `_heap_start_high' / `_heap_start_low'
```
From `esp_system/port/cpu_start.c` and `heap/port/esp32p4/memory_layout.c`
— ESP-IDF 5.5.4 introduced a new low/high memory-region split for
ESP32-P4 that expects these symbols to be defined in the linker script.
This project's custom SCons+CMake dual-build integration (see
"Architecture Notes" above — `IDF_CMAKE_BUILD=1` guards, whole-archive
linking for esp_hosted, etc.) doesn't provide/generate whatever linker
script template 5.5.4 now expects. This is a **structural build-system
incompatibility**, not a config tweak — fixing it would mean adapting our
custom linker/build integration to 5.5.4's internals, a materially
different and larger task than what we set out to test.

**Reverted.** `platformio.ini` back to `54.03.21-2` +
`toolchain-riscv32-esp @ 14.2.0+20241119` exactly as before. Confirmed
`waveshare_debug_usb` builds clean again post-revert (224s, no errors).

**Conclusion — don't re-attempt this without a real plan for the linker
issue.** The ESP-IDF-bump path (option 2 from the deep-dive above) isn't
viable as a quick experiment for this project; it needs someone to
either (a) work out what linker script changes 5.5.4 needs and adapt our
build scripts accordingly, or (b) find a pioarduino release that bundles
IDF 5.5.x with an ESP32-P4 linker script our current integration is
already compatible with (untested whether one exists). That leaves
**option 1 (patch ESP-IDF's `mipi_dsi_hal.c` framework source directly)
as the more immediately tractable path**, if the user wants to keep
pursuing the DSI hang — still requires their judgment call on the risk
of that patch (see deep-dive above), but doesn't carry this same
build-system rabbit hole.

## Framework patch attempt 2026-07-11 — hang stopped, but display still shows nothing. Negative result, informative.

User asked to try option 1. Added a patch to `scripts/patch_espidf_builder.py`
(idempotent, marker-based, same style as the existing esp_wifi_remote patch)
that modifies `~/.platformio/packages/framework-espidf/components/hal/mipi_dsi_hal.c`
directly: replaces the infinite `while (mipi_dsi_host_ll_gen_is_cmd_fifo_full(...));`
and `while (mipi_dsi_host_ll_gen_is_write_fifo_full(...));` spin-waits (8
occurrences total across the file — 3 in `mipi_dsi_hal_host_gen_write_dcs_command`
plus 5 more in sibling functions using the same LL checks) with a bounded
retry: up to 100,000 iterations × 10µs `esp_rom_delay_us()` (~1s max), log a
warning via `HAL_LOGW("dsi_hal", ...)` and proceed if it never clears, rather
than hang forever.

**False start — a reproducible crash that turned out to be unrelated.**
After first applying this patch, got a NEW, different, 100%-reproducible
(5/5 resets) crash: `assert failed: xTaskCreateStaticPinnedToCore
freertos_tasks_c_additions.h:300 (xPortCheckValidTCBMem(pxTaskBuffer))` —
happening during FreeRTOS scheduler startup, before `app_main()` or any
display code runs at all. Did a proper A/B test rather than assume
causation: temporarily disabled the patch (guarded with
`_MIPI_DSI_HAL_PATCH_ENABLED = False` in the builder script, framework file
manually reverted), rebuilt, retested — **crash still happened 5/5 times
without the patch too.** Correctly cleared the DSI patch as the cause.

Root cause was actually a **stale generated `sdkconfig.waveshare_debug_usb`**
left over from the ESP-IDF 5.5.4 upgrade experiment earlier the same
session — switching `[p4_base]`'s platform version back and forth doesn't
touch `sdkconfig.defaults`'s mtime, so the existing staleness-detection in
`patch_espidf_builder.py` (which only compares `sdkconfig.defaults` vs. the
generated file's mtime) never caught it. Deleted the stale generated
sdkconfig, forced a clean regenerate, crash gone (5/5 clean boots
afterward). **Lesson for the future: after ANY `[p4_base]` platform-version
change (even a revert back to the original), delete the relevant
`sdkconfig.<env>` file(s) and let them regenerate clean — don't assume the
existing staleness check catches platform swaps, it only catches
`sdkconfig.defaults` edits.**

**Re-enabled the DSI patch on the now-clean baseline and got a real
result.** Serial log showed, for the first time in this entire
investigation:
```
D (5785) hx8399: new hx8399 panel @0x4ff3b3c8
I (5950) ws_disp: display MCU panel power sequence complete
W (7211) dsi_hal: cmd FIFO never cleared after ~1s (patched, see esp-idf#15137) - sending header anyway
D (7221) hx8399: send init commands success
I (7223) ws_disp: ws_display_init complete  1920x720 landscape
I (7226) ev_dash: display_init complete  1920×720  buf=2764800 B ×2 (PSRAM)
D (7903) ev_dash: frame 0
```
Exactly **one** FIFO-full timeout occurred (not one per command — every
other command among the ~15-20 sent went through fast/normally), then
every subsequent DBI command succeeded, full display init completed,
CAN/tasks started, LVGL began flushing frames. No hang anywhere. Told the
user this looked like the fix — **premature.**

**User physically checked the panel: still just backlight glow, no image
at all.** This is the real, sobering result. The software layer reporting
"success" after forcing past the FIFO-full check does **not** mean the
DBI transaction actually reached the panel correctly — it means the code
stopped waiting for hardware readiness and proceeded regardless. This is
exactly the risk flagged before ever trying this patch (see "Community-
reported workaround" note in the deep-dive above). The panel showing
nothing — same as every prior attempt — strongly suggests the forced-
through command(s) were never actually latched/received correctly by the
panel, even though the host-side FIFO/header bookkeeping completed
without error.

**What this actually tells us, and it's useful:** the `gen_cmd_full`
condition is not a false alarm or spurious status bit — it's a real
signal that the DSI transmit path genuinely never became ready, and
forcing through it doesn't fix whatever that underlying condition is, it
just hides it from the logs. Combined with everything else already
established (PLL locks fine, MCU/I2C/backlight/power all confirmed
working via the same host), this keeps pointing at something specific to
how the ESP32-P4's DSI PHY actually drives data onto the physical lanes —
a deeper hardware/silicon/PHY-level issue, not something a software
timing patch can route around by simply waiting longer or proceeding
anyway.

**State of the framework patch:** left in place and enabled (harmless —
it only changes hang-forever into give-up-after-1s-and-log, doesn't make
anything worse, and the diagnostic log line is genuinely useful signal).
Doesn't fix the display. `_MIPI_DSI_HAL_PATCH_ENABLED = True` in
`scripts/patch_espidf_builder.py`.

**Where this leaves the DSI hang:** both realistic software-side avenues
from the deep-dive have now been tried and exhausted — the ESP-IDF
version bump (real build-system incompatibility, reverted) and the
framework patch (stops the symptom, doesn't fix the underlying problem,
panel still shows nothing). What's left is either a genuine ESP32-P4 DSI
PHY hardware/silicon issue (would need Espressif's own investigation, or
someone with a scope on the physical DSI lanes to see what's actually
happening electrically during that stuck FIFO period), or something not
yet identified in this investigation. Touch remains separately broken too
(`TouchPad_ID:0x00,0x00,0x00`, unaffected by any of today's DSI work, as
expected since they're different root causes).

## Cable swap 2026-07-11 — found a physical tear, real improvement, but doesn't fix the blank screen

User found a **tear** in the custom FPC cable that had been in use (had 5
identical ones made, swapped to a fresh one). Rebooted with the patched
firmware still active. Result, precise comparison against the prior run:

```
I (5950) ws_disp: display MCU panel power sequence complete
D (6200) hx8399: send init commands success        ← only ~250ms later
```

**Zero FIFO timeouts this run** (`grep -c "never cleared"` = 0), versus
exactly one in the prior run with the torn cable. Every DBI command went
through immediately — no bounded-wait fallback needed at all this time.
LVGL rendered frames 0 through 6 over several seconds of runtime (longer,
cleaner boot than any prior run).

**But the screen still shows nothing** — same backlight-only glow as
every attempt before it.

**Interpretation:** the cable tear was real and was genuinely degrading
DSI signal integrity enough to trigger the FIFO-stuck condition
intermittently — that's a real, measurable improvement, not a wash. But
it wasn't *the* root cause of the blank screen: the fundamental "no image
reaches the panel" problem is fully independent of whatever was causing
the FIFO stall, since a materially cleaner boot (zero stalls vs one)
produced an identical visible outcome. Two separate problems were
conflated by the same visible symptom (blank screen) — fixing one
(marginal cable → intermittent FIFO stall, now resolved) left the other
(whatever actually prevents image data reaching the panel) completely
unaffected.

**Updates this to the running theory:** cable condition matters and is
now confirmed to affect *something* real at the DSI signal-integrity
level, which weakens (but doesn't eliminate) the "must be a pure ESP32-P4
PHY/silicon issue" framing from directly above — worth being less certain
that this is purely a chip-level problem than the previous section
concluded. Still doesn't explain the blank screen by itself though, since
the fresh cable didn't fix that.

## BREAKTHROUGH 2026-07-11 (later same session) — the SWRESET hang is gone; real blocker is continuous DMA underrun

User asked to revert the `mipi_dsi_hal.c` patch (disabled via
`_MIPI_DSI_HAL_PATCH_ENABLED = False`, framework file manually reverted
to original content) to prep for the planned scope session — expecting
this would bring back the original hang-forever behavior for a clean,
unhurried probe target. **It didn't.** With the fresh (untorn) cable, even
the completely stock, unpatched ESP-IDF HAL now boots clean:

```
D (5785) hx8399: new hx8399 panel @0x4ff3b3c8
I (5950) ws_disp: display MCU panel power sequence complete
D (6200) hx8399: send init commands success
I (6202) ws_disp: ws_display_init complete  1920x720 landscape
```

No hang, no watchdog, no crash dump — confirmed clean via multiple
resets. **The cable swap alone fixed the SWRESET/cmd-FIFO hang. The HAL
patch was never actually necessary once the torn cable was replaced.**
This reframes the whole multi-day "ESP32-P4 DSI PHY bug" investigation:
what looked like (and partially IS, per the still-open espressif/esp-idf
GitHub issues) a host-side HAL bug was, on our specific hardware, at
least significantly caused by the physical cable defect. Worth being
humble here — we can't fully rule out that both factors were compounding
(marginal cable *and* a genuine HAL timing sensitivity that the marginal
connection was tipping over), but the practical result is what matters:
current hardware boots past this point reliably now.

**But a new, different, previously-partially-addressed problem is now
visible for the first time** (only reachable because we finally get past
the hang): continuous DMA underrun once real DPI video streaming starts.
```
E lcd.dsi.dpi: can't fetch data from external memory fast enough, underrun happens
```
195 occurrences in a 20-second capture — starts immediately after
`ws_display_init complete`, before even the first LVGL frame renders,
and continues throughout. Confirmed present in **both** the unpatched
run just described and the earlier patched-firmware fresh-cable test
(re-checked that capture: 194 occurrences there too — same issue, just
hadn't grepped for it at the time). This is one consistent finding
across both firmware variants, not a new distinction between them.

**Leading hypothesis, ties directly to something the user recalled
earlier this session:** PSRAM is currently running at the slow, stable
**20MHz** setting (`sdkconfig` — deliberately reverted from 200MHz in an
earlier session because 200MHz was non-deterministic, sometimes hanging
in DQS tuning; see `project_display_power_wiring`-adjacent history /
earlier CONTEXT.md entries). This project already has a mitigation for
DMA-vs-arbitration contention (the AXI arbiter QoS priority boost in
`prv_panel_init()`, `axi_icm_ll_set_dw_gdma_qos_arbiter_prio(...)` set to
max for both DW-GDMA ports) — but that only helps the DPI-fetch DMA *win*
bus arbitration faster, it can't raise PSRAM's actual raw read bandwidth
ceiling. If 20MHz genuinely isn't fast enough to sustain the read
bandwidth 1920×720 RGB565 DPI video streaming needs, continuous underrun
regardless of arbitration priority is exactly what you'd expect. This
couldn't have been observed before today — we never got far enough past
the SWRESET hang to reach sustained streaming and find out.

**This is good news relative to where the investigation stood an hour
ago:** we've moved from an unexplained, seemingly ESP32-P4-silicon-level
mystery to a well-understood, previously-documented class of problem
(PSRAM bandwidth vs. DMA demand) with a known previous non-determinism
history to draw on.

**Next step, not yet done:** investigate whether PSRAM can run faster
than 20MHz reliably now (worth retrying the 200MHz setting now that the
cable defect — which may have been contributing to broader system
flakiness beyond just DSI — is fixed; the earlier DQS-tuning
non-determinism might have been partially related to the same marginal
connection, or might be a fully separate PSRAM-silicon issue — genuinely
unknown, worth a fresh, honest retry rather than assuming), or explore
whether DPI bandwidth demand can be reduced (lower `WS_DPI_CLK_MHZ`,
reduced resolution/color depth, or a smaller renders-only-dirty-region
approach) if 20MHz truly can't be exceeded reliably. The planned scope
session (Tek TDS640A on CLK/D0/D1, P4-Nano 15-pin connector pins 5/6 and
8/9) is **lower priority now** — it was aimed at diagnosing the hang,
which appears resolved; not clearly useful for a PSRAM-bandwidth
question. Hold that plan unless the PSRAM-speed investigation doesn't
pan out.

**State of the HAL patch:** currently disabled/reverted
(`_MIPI_DSI_HAL_PATCH_ENABLED = False` in
`scripts/patch_espidf_builder.py`, framework file back to stock). Given
it's confirmed unnecessary with the fresh cable, leave it disabled —
re-enabling would just hide a real signal (the cmd-FIFO-full condition)
if the underlying hang theory turns out to still matter on some future
cable/connector variance. Don't re-enable without a specific reason.

## DMA underrun investigation 2026-07-11 (continued, autonomous — user stepped away) — 200MHz PSRAM re-tested and rejected, DPI clock lowered as a working mitigation

**200MHz PSRAM retested, conclusively rejected — worse than before, not
the cable.** Set `CONFIG_SPIRAM_SPEED_200M=y` +
`CONFIG_IDF_EXPERIMENTAL_FEATURES=y` in `sdkconfig.defaults` (deleted the
generated `sdkconfig.waveshare_debug_usb` first — lesson from earlier
today, config changes need a clean regenerate). Tested 10 consecutive
resets: **10/10 hung** in the exact same `MSPI DQS: set to best phase: 0`
infinite retry loop documented from an earlier session — previously
described as "sometimes calibrates, sometimes hangs," now 100%
reproducible failure. The cable fix did not help this — it's a genuine,
separate PSRAM calibration issue, unrelated to the DSI/cable work.
**Reverted to 20MHz** (`sdkconfig.defaults` updated with this finding,
generated sdkconfig deleted again for clean regen). Confirmed: for
ESP32-P4 in this ESP-IDF version, PSRAM speed is a hard binary choice —
20M or 200M only; the Kconfig has a vestigial `SPIRAM_SPEED_100M`
reference in a default-value line but no such option is actually defined
anywhere, so no real middle ground exists via config alone.

**User's idea, tested and ruled out cleanly: is underrun caused by app-
side PSRAM contention rather than a hard DPI bandwidth ceiling?** Added a
temporary diagnostic (`UI_RENDER_ONCE_TEST` — `#ifdef` block in
`main.cpp`'s `ui_task`, gated via `-DUI_RENDER_ONCE_TEST=1` in
`waveshare_debug_usb`'s `build_flags`): render exactly one frame via
`dashboard_ui_update()`/`lv_refr_now()`, then go fully idle — no further
LVGL/PPA/framebuffer writes from the app at all. Result at the original
75MHz DPI clock: **underrun persisted regardless** (190 occurrences in
20s, same as normal continuous rendering) — conclusively rules out app-
side contention as the (sole) cause. This is a genuine, hard DPI hardware
scan-out bandwidth ceiling: the DPI controller can't keep re-reading even
a completely static, unchanging framebuffer fast enough at 20MHz PSRAM
speed, regardless of whether the app touches PSRAM further. (Test flag
since removed from `platformio.ini` — diagnostic concluded — but the
`#ifdef UI_RENDER_ONCE_TEST` block is left in `main.cpp` for reuse.)

**Fix found: lower the DPI clock to reduce the bandwidth requirement
itself.** Since PSRAM's raw bandwidth ceiling can't be raised (200MHz is
broken, no middle speed exists), tried reducing `WS_DPI_CLK_MHZ` instead
— less bandwidth demanded per unit time, whether or not PSRAM's ceiling
moves. Tested under **normal continuous rendering** (not idle — this is
what the real app needs) across a full sweep, 20-second capture each,
multiple resets confirmed reproducible:

| DPI clock | Underrun count (20s, continuous load) | Refresh rate (approx) |
|---|---|---|
| 75 MHz (original) | ~190 | ~49.7 Hz |
| 50 MHz | ~184 | ~33.2 Hz |
| 30 MHz | ~143 | ~19.9 Hz |
| 25 MHz | ~126 | ~16.6 Hz |
| **20 MHz** | **~40** | **~13.25 Hz** |
| 20 MHz, app fully idle (render-once test) | ~3 | n/a |

**Sharp, non-linear threshold between 20 and 25 MHz** — not a smooth
bandwidth-proportional curve. 25MHz is already most of the way back to
baseline-bad; only at 20MHz does underrun drop to a small fraction of its
original rate. Reproducibility confirmed: 5/5 resets at 20MHz gave
identical results (idle case: exactly 3 underruns every time, all
clustered at startup/transient events, zero for the rest of each 20s
capture).

**Settled on `WS_DPI_CLK_MHZ = 20`** (`waveshare_display.c`) as the
current best value — by far the largest underrun reduction found, and
the only one tested that gets substantially below baseline rather than
just marginally better. Full build+flash+reflash cycle completed, final
firmware confirmed running with this value (41 underruns/20s on the
final verification pass, consistent with the 40 found during the sweep).

**Caveats / not yet resolved:**
1. **Still not zero underrun under continuous load** (~40/20s, ~2/sec) —
   better than ~190/20s but not eliminated. Whether occasional/transient
   underrun events (previously documented as self-recovering, not fatal)
   still permit a stable, viewable image, or whether even this reduced
   rate is enough to prevent one, is unknown — needs the user's eyes on
   the physical screen. This is the single most important open question
   right now.
2. **~13.25Hz refresh rate is low** for a live dashboard — likely visible
   as sluggish/laggy updates (LCD sample-and-hold behavior, not flicker
   like a CRT, but still noticeably slow). Worth revisiting once
   something actually displays: could try recovering refresh rate via
   reduced color depth (e.g. RGB565→something narrower) or a smaller
   active framebuffer region instead of a flat DPI clock cut, since the
   goal is reducing PSRAM read bandwidth per unit time, and DPI clock is
   only one lever for that.
3. Noticed only ~7 `ev_dash: frame N` log lines per 20s capture at 20MHz
   (vs. an expected ~300 at the nominal 66ms task loop period) — likely
   because `esp_lcd_panel_draw_bitmap()` blocks until the actual (now
   much slower) DSI transfer completes, so real UI update cadence is
   probably closer to the ~13Hz DPI refresh rate than the task loop's
   nominal period. Not investigated further; noted as a likely expected
   side-effect of the lower clock, not a new bug.
4. Touch remains completely separately broken
   (`TouchPad_ID:0x00,0x00,0x00`) — untouched by any of today's DSI/PSRAM
   work, still needs its own investigation whenever picked back up.

**Immediate next step: check the physical screen with the current
firmware** (20MHz DPI clock, normal continuous rendering, 20MHz PSRAM,
fresh untorn cable, HAL patch disabled/reverted to stock). This is the
first time in the entire multi-day investigation that every known
software-side blocker has been addressed simultaneously — genuinely
don't know whether an image will finally appear or not without looking.

## Result: still blank at 20MHz DPI clock. Panel-timing-validity theory tested too — also blank. Software-side avenues now exhausted.

**User checked the screen at 20MHz DPI clock: nothing.** Same backlight-
only glow as every prior attempt, despite underrun dropping from ~190 to
~40/20s and every other known blocker addressed.

**Reconsidered before trying anything else:** the DPI-clock reduction
fixed a *host*-side symptom (DMA underrun) but was never checked against
whether it's still within the *panel's* valid operating range. HX8399-C
datasheet (Table 8.14, Vertical timings for DSI I/F) specifies **Vertical
Refresh rate = 60Hz**. Our original 75MHz DPI clock already ran below
that at ~49.7Hz (a pre-existing compromise for 2-lane bandwidth, per the
original code comment) — but 20MHz gives only **~13.25Hz**, roughly a
quarter of spec. Cross-checked against the Pi5 (confirmed working):
~95MHz DPI clock → ~63Hz, right in line with the datasheet. Strongly
suggested 20MHz might be far enough outside the panel's valid GIP timing
range to prevent any coherent scan-out regardless of host-side success —
worth testing before pursuing bandwidth reduction further.

**Reverted `WS_DPI_CLK_MHZ` back to 75** (the panel-timing-valid,
datasheet/Pi5-consistent value) to isolate this variable. First attempt
hit a **new, different symptom**: backlight flashed on then off, USB
serial dropped completely (no crash dump, no watchdog message — a hard
disconnect) at the exact point where `prv_mcu_panel_power_on()` was about
to engage real panel power (IOVCC/AVDD/reset/backlight-enable via the
MCU). Root cause: **user had turned down the bench supply's current
limit too far** (from 3A to "something more sane," went too far —
overcurrent tripped right as real panel power draw kicked in). Not a
regression, not code-related — fixed by restoring the current limit.
Worth remembering for future sessions: **real panel power draw (once
IOVCC/AVDD/backlight actually engage) is higher than earlier stages of
boot** — a current limit that's fine through MCU/touch/DSI-bus-setup can
still trip once the panel's own power sequence actually runs.

**After fixing the current limit: clean boot, `ws_display_init
complete`, underrun back to ~185/20s (baseline, as expected at 75MHz),
no disconnect. User checked the screen: still nothing.**

**This is a genuinely important negative result.** Both ends of the DPI
clock spectrum have now been tried — 20MHz (low underrun, likely-invalid
panel timing) and 75MHz (panel-timing-valid per datasheet + Pi5
cross-check, high underrun) — and **neither produces any visible
change**. This rules out DPI clock tuning, in either direction, as *the*
fix on its own. Left `WS_DPI_CLK_MHZ = 75` (the more defensible baseline
— matches spec and the known-working Pi5 reference; 20MHz was a more
speculative deviation that didn't pay off).

**Where this leaves the whole investigation:** every reasonable
software-side lever has now been tried — cable (fixed), MCU power
sequencing (confirmed byte-exact match to the working Pi5 reference),
SWRESET hang (resolved), PSRAM speed (settled at 20MHz, 200MHz
confirmed broken), DPI bandwidth (tried both directions) — and the
visible symptom (backlight-only, no image) has not changed once through
any of it. Combined with the earlier, directly-demonstrated gap between
"host software reports success" and "panel actually received anything"
(the HAL-patch experiment: forced past the FIFO-full check, full
"success" logged, still nothing on screen) — continuing to infer from
serial logs alone has a demonstrated failure mode. **The planned scope
session (Tek TDS640A on CLK/D0/D1, P4-Nano 15-pin connector pins 5/6 and
8/9) is now the highest-value next step, no longer deprioritized** — it's
the only way left to get real electrical ground truth on whether
anything is actually being transmitted on the DSI lanes during panel
init, independent of what any host-side status register claims.

## 2026-07-12: host-side DSI register diagnostics — real breakthrough on WHERE the fault is

Built a series of temporary diagnostics in `src/waveshare_display.c`, all
gated by build flags added to `[env:waveshare_debug_usb]` in
`platformio.ini` (`-DPANEL_DIAG_READ_TEST`, `-DPANEL_BIST_TEST`,
`-DDSI_HOST_STATUS_TEST` — all still enabled as of this writing, meant to
be temporary, should be removed once the investigation concludes).

**1. Panel-side MIPI DCS read-back (genuine BTA, not fire-and-forget).**
Added real reads via `esp_lcd_panel_io_rx_param()` right after
`esp_lcd_panel_init()` in `prv_panel_init()`: `RDDPM` (0x0A), `RDDSDR`
(0x0F), `RDNUMPE` (0x05, DSI parity error count), `RDDID` (0x04), and
`GETSCAN` (0x45, twice 20ms apart). Result: `RDDPM=0x9D` decodes to
Booster-OK / Sleep-Out / Display-Normal-Mode-ON / Display-ON — a
coherent, plausible value, not stuck/garbage, and getting it requires
real electrical bus-turnaround. `RDNUMPE=0x00` (zero DSI parity errors).
`RDDID=83:10:2E` exactly matches our own `SETEXTC` unlock key
(`s_hx8399_init_cmds[0]`) — concluded this is the panel echoing the
unlock key back as an interface-alive check, not a real factory ID (so
not suspicious, actually a good sign the manufacturer-page read path
works). `GETSCAN=0/0` unchanged — likely meaningless since Tearing
Effect mode was never enabled; not read as evidence either way.
**Verified the read path itself is real**: `mipi_dsi_hal_host_gen_read_short_packet()`
(`framework-espidf/components/hal/mipi_dsi_hal.c:196`) genuinely enables
BTA, sends the packet, busy-waits the real hardware RX FIFO — not a
software stub/echo.

**2. HX8399-C internal BIST self-test pattern — two attempts, both
inconclusive on-screen, but exposed a real bug in attempt 1.** The
HX8399-C datasheet documents a `SETDISP`/`B2h` "Bank1" register with a
`DISP_BIST_EN` bit (internal free-running test-pattern generator,
independent of any DSI video stream — datasheet calls it "SW free
running mode"). First attempt used the WRONG `SETEXTC` unlock key
(copied from the *generic* Espressif reference driver's default init
table, `{0xFF,0x83,0x99}`) instead of this panel's real key (confirmed
working, from our own `s_hx8399_init_cmds[0]`: `{0x83,0x10,0x2E}`) —
almost certainly meant the manufacturer page was never actually open, so
the BIST-enable write was likely a no-op. **Second attempt fixed the
unlock key** and sent a single continuous 19-byte `B2h` write (Bank0's
10 known-good bytes, identical to `s_hx8399_init_cmds`, followed by 9
Bank1 bytes with `DISP_BIST_EN=1`, pattern=White) instead of the
first attempt's unverified `BDh`-bank-select detour (which, on
inspection, is never used anywhere in our real working init table —
only seen paired with an unrelated command, `D8h`, in the generic
reference driver — so that pairing was never actually validated for
`B2h`). **Neither attempt produced any visible change on screen.**
Given finding #3 below, this is no longer surprising — see conclusion.

**3. THE FINDING: direct ESP32-P4 host-side DSI register reads show HS
video is never transmitted, at all, independent of anything the panel
reports.** Added reads of `MIPI_DSI_HOST.phy_status`, `.int_st0`,
`.int_st1`, `.vid_pkt_status` (struct in
`framework-espidf/components/soc/esp32p4/register/soc/mipi_dsi_host_struct.h`,
extern instance `MIPI_DSI_HOST`) — these are chip-side registers,
nothing to do with the panel. Sampled 6x (80ms apart) right after
`esp_lcd_panel_init()`, and 8x (20ms apart) during the app's first real
`esp_lcd_panel_draw_bitmap()` call (via a counter in
`prv_lvgl_flush_cb()`, `flush_count==1`) — i.e. one sample before any
real frame, one sample during active, ongoing frame-pushing with the
"can't fetch data... underrun" spam happening concurrently. **Both
sample sets are byte-for-byte identical**: `phy_status=0x000015BD`
(PLL locked, but clock lane AND both data lanes permanently parked in LP
stop-state — never once seen bursting HS), `vid_pkt_status=0x00010005`
(all DPI-to-bridge FIFOs permanently empty — not full/backed-up, just
never fed), `int_st0=int_st1=0` (zero errors, because there's zero
activity to error on). **Retested at `WS_DPI_CLK_MHZ=20`** (max possible
DMA slack) — identical result, ruling out simple bandwidth starvation as
the cause (the earlier theory, from a forked sub-agent trace through
`esp_lcd_panel_dpi.c`'s `dpi_panel_init()`, that the GDMA→bridge
pipeline never sustains long enough to complete a burst). **Also
confirmed `esp_lcd_panel_draw_bitmap()` returns `ESP_OK` on every call**
(logged explicitly) — ruling out a second theory, that the DPI panel's
async DMA2D copy path (`esp_lcd_panel_dpi.c` ~line 530,
`xSemaphoreTake(dpi_panel->draw_sem, 0)` non-blocking, returns
`ESP_ERR_INVALID_STATE` and silently drops the frame if the previous
copy hasn't finished) was silently dropping every frame.

**Conclusion: this is a genuine "HS video mode never starts" bug,
proven from the host side, independent of the panel.** Every panel-side
signal we can get (LP commands, BTA reads, BIST attempts) is consistent
with the panel being correctly powered and idly waiting for video it
never receives — not with a panel/cable defect. **User confirmed the
same physical panel worked immediately on a Pi5** — ruling out dead
glass/bonding as an explanation. Since the M5Stack Tab5 board (same
ESP32-P4 chip family, different panel — ST7123, `src/tab5_display.c`)
*does* get real video output through the same ESP-IDF DSI/DPI framework,
**the DSI peripheral silicon itself is not broken** — this is either a
P4-Nano-board-specific hardware quirk or a config/sequencing bug in our
bring-up, not a fundamental chip problem.

**Ruled out as the Tab5-vs-Waveshare differentiator** (via direct code
diff, `src/tab5_display.c` vs `src/waveshare_display.c`): DSI PHY LDO
channel/voltage (both ch3 @ 2500mV), lane count (both 2) and lane bit
rate (950 vs 965 Mbps, both close), DBI IO config (identical, both use
the same macro-expanded values), `num_fbs` (both 2),
`dpi_cfg.flags.use_dma2d` (both true), `bits_per_pixel` (Tab5 itself has
a *worse* mismatch — 24bpp `panel_cfg` vs 16bpp RGB565 `pixel_format` —
yet still works, so this field is unlikely DMA-critical),
`disp_on_off()` chaining (both the HX8399 and ST7123 vendor components
override `disp_on_off` identically — just resend a DCS command, never
call through to the real underlying generic DPI panel's `disp_on_off` —
so whether our app calls it or not is irrelevant; confirmed by reading
`panel_hx8399_disp_on_off()` in the managed component directly). Also
ruled out, via a forked trace through
`framework-espidf/components/esp_lcd/dsi/esp_lcd_mipi_dsi_bus.c:90-97`:
**DSI PHY calibration timing relative to panel power** — our board
powers the panel glass (via the external I2C MCU) *after* creating the
DSI bus/PHY object, unlike Tab5's self-contained panel, so this looked
like a plausible board-specific culprit, but `esp_lcd_new_dsi_bus()`'s
PHY calibration (`mipi_dsi_hal_configure_phy_pll()` +
`mipi_dsi_phy_ll_is_pll_locked()`/`are_lanes_stopped()` polling) is
purely internal to the ESP32-P4's own PLL/PHY — no panel handshake, no
dependency on panel power/attachment. Clock lane is deliberately held in
LP mode at this stage "until DPI stream is ready," so bus-creation-time
panel power state is a non-issue by design.

**Still open / not yet checked:** the remaining, not-yet-diffed
candidates between the two boards are the panel-specific `video_timing`
porch values and the vendor-specific init command sequences themselves
(naturally different per panel, but worth a careful re-check for typos
against the Waveshare/Himax reference), and anything in
`esp_lcd_panel_dpi.c` between "PHY calibration completes" and "GDMA
descriptor/linked-list actually gets armed" that could depend on
`video_timing` values specifically (e.g. a htotal/vtotal that doesn't
divide evenly, or a porch value that trips a validation branch
differently for our resolution than Tab5's). Given every other lever is
now ruled out, this and the planned oscilloscope session are the two
live leads.

**Repo state as of this writing (needs cleanup before merging):**
`src/waveshare_display.c` has several `#ifdef`-gated temporary diagnostic
blocks (`PANEL_DIAG_READ_TEST`, `PANEL_BIST_TEST`, `DSI_HOST_STATUS_TEST`,
`DSI_PATTERN_GEN_TEST`, `DSI_JD9365_COMBINED_TEST`, plus the older
`SOLID_FILL_TEST`) — see the next section for which are enabled in the
resting build.

## 2026-07-12 (continued): PHY-trigger breakthrough via Waveshare's own reference BSP — real HS bursts proven possible, but a SECOND, separate GDMA-feed bug still blocks real content

User pointed out the ESP32-P4-NANO-KIT-D (a different Waveshare board,
10.1" JD9365 panel + GT9271 touch, also 2-lane DSI) should share host-
side DSI interface config with our P4-Nano even though the panel
controller differs, and provided a link to Waveshare's own
`13_Displaycolorbar` reference example
(github.com/waveshareteam/ESP32-P4-Platform). Traced through to the
actual underlying BSP source
(`github.com/waveshareteam/Waveshare-ESP32-components`,
`bsp/esp32_p4_platform`) since the example itself just calls a BSP
function. Found `esp32_p4_platform.c`'s DSI/DPI setup, and the JD9365
10.1" panel's own header (`display/lcd/esp_lcd_jd9365_10_1/include/
esp_lcd_jd9365_10_1.h`) with two very concrete, proven-working reference
values for this exact board family:
- `JD9365_PANEL_BUS_DSI_2CH_CONFIG()`: **`lane_bit_rate_mbps = 1500`**
  (vs our `WS_DSI_LANE_MBPS = 950`, the `esp_lcd_hx8399` component's own
  2-lane default — never something we'd chosen deliberately).
- `JD9365_800_1280_PANEL_60HZ_DPI_CONFIG()`: `dpi_clock_freq_mhz = 80`,
  h_size=800/v_size=1280, hsync 20/20/40, vsync 4/10/30 (different
  resolution, not directly applicable, but the DPI-clock/lane-rate pair
  is board-level, not panel-specific).

**Tested lane rate alone first** (1500 Mbps, keeping our own
720x1920/porches, at whatever `WS_DPI_CLK_MHZ` was set to at the time —
20MHz, left over from the earlier bandwidth test): **zero effect**,
`phy_status` still permanently `0x000015BD` (all lanes stuck in LP
stop-state). Ruled out lane rate alone as sufficient.

**Tested the full combined JD9365 reference config at once** (foreign
800x1280 resolution/porches + 80MHz DPI clock + 1500Mbps lane rate, fed
through `esp_lcd_dpi_panel_set_pattern(s_panel, MIPI_DSI_PATTERN_BAR_VERTICAL)`
— the DSI host's own built-in hardware test-pattern generator, found via
the same Waveshare colorbar example, which generates pixels entirely
inside the DesignWare DSI Host IP with zero GDMA/PSRAM/framebuffer
involvement): **first non-static `phy_status` reading of the entire
session** — `0x000015B9` (clock lane briefly left stop-state,
`clkstop=0`), though it reverted moments later and data lanes never
moved in that first sample.

**Isolated further**: reverted resolution/porches back to our own real
720x1920 values (keeping 80MHz DPI clock + 1500Mbps lane rate), sampled
`phy_status`/`vid_pkt_status` every 10ms for 150ms right after enabling
the pattern generator. Result: **repeated, full three-lane HS bursts** —
`phy=0x00001529` (`clkstop=0 d0stop=0 d1stop=0`, ALL lanes simultaneously
out of stop-state) appeared in roughly half the samples, cycling with
the stuck `0x15BD` state in between (consistent with normal DSI video's
LP-idle-during-blanking / HS-burst-during-active-line behavior — NOT a
fluke or measurement artifact). Confirmed this reproduces with our
*original, unwidened* porches too (10/12/10/18/4/64), not just the
earlier-widened test values. **This is the first proof all session that
this board's DSI PHY hardware is physically capable of HS bursting at
all** — resolution and porches don't matter; it's specifically the
`dpi_clock_freq_mhz`/`lane_bit_rate_mbps` combination that determines
whether the PHY ever triggers, not either value in isolation (950Mbps
alone, 1500Mbps alone with a low DPI clock, and wide/narrow porches at
either lane rate had all previously shown zero effect).

**Applied 1500Mbps + 80MHz as the new real `WS_DSI_LANE_MBPS`/
`WS_DPI_CLK_MHZ` values** (not just the diagnostic-only pattern
generator) and re-tested with REAL LVGL/GDMA-fed rendering (pattern
generator disabled, normal `draw_bitmap` flow). **Result: `phy_status`
permanently stuck at `0x15BD` again — zero bursts across 30 consecutive
real frames (~54 seconds of continuous rendering, sampled once per
flush)**, and `vid_pkt_status` showed the DPI-to-bridge buffer
permanently EMPTY (`0x00010005`) rather than the pattern-generator's
FULL state (`0x00020005`). **This means real pixel data from our own
GDMA/PSRAM pipeline never even reaches the bridge's buffer at all**,
let alone triggers a burst — a difference in kind from the pattern
generator, which needs no external memory access and therefore never
underruns.

**Checked whether 80MHz specifically was required for the PHY-trigger
condition, or whether a lower, GDMA-friendlier clock would also work**:
re-ran the pattern-generator test at 75MHz (our long-standing prior
baseline) + 1500Mbps — **also bursts** (same intermittent
`0x1529`/`0x15BD` cycling pattern). So the PHY-trigger condition is
satisfied by 1500Mbps lane rate combined with *either* 75 or 80MHz DPI
clock — it is NOT specifically tied to 80MHz. But real GDMA-fed
rendering at 75MHz + 1500Mbps **also** showed zero bursts / permanently
empty buffer, matching the 80MHz result. **This conclusively narrows the
remaining problem to a second, separate bug**: something prevents our
own GDMA/PSRAM pixel-feed path from ever successfully depositing data
into the DSI bridge's buffer under real load, independent of whether the
PHY itself is capable of bursting once data is available (proven it is,
via the pattern generator, at the same clock/lane-rate settings).
Underrun errors (`lcd.dsi.dpi: can't fetch data from external memory
fast enough`) have been a background symptom all session at every tested
DPI clock — this new evidence suggests they may not just be "some frames
glitch" but "GDMA never succeeds in feeding the bridge at all," at least
at the higher lane rate/DPI clock settings tested so far. Not yet tested
at the *original* 950Mbps/75MHz combination with this same
`vid_pkt_status` diagnostic — worth checking whether the buffer was ever
non-empty under the old, lower-bandwidth-demand config, to see if this
GDMA-feed problem is itself bandwidth-sensitive or a harder failure.

**Resting state as of this writing**: `WS_DSI_LANE_MBPS = 1500`,
`WS_DPI_CLK_MHZ = 75`, original porches (10/12/10/18/4/64) — this is a
strict improvement over the pre-session baseline (950Mbps/75MHz) in that
it's *proven* capable of real HS bursts under the right conditions, even
though real content still doesn't reach the panel yet. `DSI_PATTERN_GEN_TEST`
and `PANEL_BIST_TEST` build flags disabled in `[env:waveshare_debug_usb]`
(both interfere with/are irrelevant to normal rendering); `PANEL_DIAG_READ_TEST`
and `DSI_HOST_STATUS_TEST` left enabled (harmless, useful for any future
debugging session). Boots clean, no crashes, reaches steady frame
rendering — confirmed via a final sanity check.

**Next steps, in priority order:**
1. Characterize the GDMA-feed failure directly — instrument the GDMA
   channel/descriptor setup itself (not just DSI host registers) to see
   whether it's actually being armed/started correctly for real
   `draw_bitmap` calls, versus the pattern generator's zero-GDMA path.
   Compare against the original 950Mbps/75MHz combo's `vid_pkt_status`
   behavior (not yet checked) as a data point on whether this is
   bandwidth-proportional or an all-or-nothing failure.
2. Oscilloscope session (D0P/D1P/CLKP, single-ended is sufficient for
   the LP-vs-HS question — see chat for the differential-vs-single-ended
   reasoning) — now even more informative, since we have a specific,
   reproducible config (1500Mbps/75-80MHz + pattern generator) known to
   produce real HS bursts to scope against, as well as the still-broken
   real-rendering config to compare.
3. Validate the diagnostic register-read methodology itself against the
   Tab5 board (known-working, different panel) if it becomes available —
   confirms `phy_status`'s bit meanings are being read correctly, closing
   a blind spot never actually verified this session.

## 2026-07-12 (continued): 200MHz PSRAM retested a third time — confirmed genuinely broken on this hardware, not a missing-config issue

User asked whether Waveshare's own reference running PSRAM at 200MHz
(their `sdkconfig.defaults` has `CONFIG_SPIRAM_SPEED_200M=y`) might
explain the GDMA-starvation problem above — a very reasonable question,
since we've been locked at 20MHz PSRAM (10x less bandwidth) since the
200MHz DQS-calibration hang was "conclusively rejected" on 2026-07-11.

Investigating turned up something concrete: `esp_psram/esp32p4/
Kconfig.spiram` defines `config SPIRAM_SPEED_200M` with `depends on
IDF_EXPERIMENTAL_FEATURES` — a real Kconfig prerequisite we never had
set. Confirmed empirically: adding `CONFIG_SPIRAM_SPEED_200M=y` alone to
`sdkconfig.defaults` and regenerating produced a sdkconfig that still
showed `CONFIG_SPIRAM_SPEED_20M=y` — the request was silently dropped,
no warning anywhere. This means **every prior 200MHz attempt on this
project, including the 2026-07-11 "10/10 hang, conclusively rejected"
test, may never have actually had 200MHz active** (unless that session's
generated sdkconfig had `IDF_EXPERIMENTAL_FEATURES` set some other way,
e.g. a stale/manually-edited sdkconfig.<env> from an earlier menuconfig
session — plausible given this exact class of stale-sdkconfig bug has
bitten this project before, see the `xTaskCreateStaticPinnedToCore`
crash episode). Genuinely unclear which was the case, but not worth
chasing further given the result below.

**Did a proper, complete retest**: added `CONFIG_SPIRAM_SPEED_200M=y`,
`CONFIG_CACHE_L2_CACHE_256KB=y`, `CONFIG_CACHE_L2_CACHE_LINE_128B=y`
(Waveshare's own L2 cache settings, which we'd also never had), and
`CONFIG_IDF_EXPERIMENTAL_FEATURES=y` (the missing prerequisite) —
matching Waveshare's reference `sdkconfig.defaults` exactly for the
PSRAM-relevant lines. Deleted the generated `sdkconfig.waveshare_debug_usb`
first (per this project's own documented stale-sdkconfig lesson) and
rebuilt clean. Confirmed via the regenerated sdkconfig that 200MHz was
genuinely active this time (`CONFIG_SPIRAM_SPEED_200M=y`,
`CONFIG_SPIRAM_SPEED=200`, not silently reverted). **Result: 5/5 resets
hung in the identical `MSPI DQS: set to best phase: 0` retry loop as
every previous attempt.**

**CORRECTION (same day, user pushback) — this was NOT a complete config
match, retract "definitively"/"conclusively" language above.** User
pushed back on accepting a hardware-defect conclusion this readily, and
was right to: two real gaps in the test. (1) Never added
`CONFIG_SPIRAM_XIP_FROM_PSRAM=y`, which IS in Waveshare's reference —
skipped it as "unrelated to bandwidth," but XIP changes how PSRAM is
mapped/accessed during early boot and could plausibly affect calibration
robustness, not just runtime throughput. (2) Tested on the **debug**
build (`waveshare_debug_usb`), not matching their
`CONFIG_COMPILER_OPTIMIZATION_PERF=y` — if DQS calibration involves
timing-sensitive retry loops, debug-level optimization vs. a performance
build could genuinely change whether calibration succeeds. So: 200MHz
PSRAM remains **genuinely unresolved**, not closed. A real "different
plan" retest would need XIP_FROM_PSRAM added AND a non-debug/perf-
optimized build — not yet done. Also worth noting: user independently
found the `IDF_EXPERIMENTAL_FEATURES` dependency in an earlier session
whose context was lost (see chat) — this wasn't a novel discovery this
session, just re-derived independently. **User asked to set 200MHz
aside for now and focus on getting any pixels on screen by other means**
— see the pattern-generator lead below, which doesn't depend on
resolving this at all.

**Reverted cleanly** back to `CONFIG_SPIRAM=y` only (20MHz default, the
stable baseline) in `sdkconfig.defaults` — removed the 200MHz/L2-cache/
experimental-features lines entirely rather than leaving them commented
out and inert, to avoid confusing a future session about what's
currently active. Regenerated sdkconfig confirmed back to
`CONFIG_SPIRAM_SPEED_20M=y`. Reflashed and confirmed clean boot, no DQS
hang, `ws_display_init complete`, no crash — board is back to the same
resting state as before this specific test (1500Mbps lane rate, 75MHz
DPI clock, GDMA-feed bug still unresolved, panel still blank).

## 2026-07-13: 200MHz PSRAM crash root-caused and FIXED — real, standalone win; GDMA-to-bridge gap still separate and unresolved

User pushed back further on giving up on 200MHz, recalled it may have
worked before in a lost session, and asked whether the crash could be a
watchdog timeout from the long tuning duration, suggesting we
short-circuit tuning with known-good hardcoded values (phase=0,
delayline=16 — the values every observed tuning pass converged on).

**Tested the "reduce tuning duration" theory directly, without
hardcoding** (safer — still a real, self-calibrating sweep): patched
`MSPI_TIMING_DELAYLINE_TEST_NUMS` (repeat count per delayline candidate,
in `esp_hw_support/port/esp32p4/mspi_timing_tuning_configs.h`) from 100
down to 8 via `scripts/patch_espidf_builder.py` (idempotent, same
pattern as the existing patches) — cut tuning time ~12x (~11s -> under
1s). **Crash was 100% unchanged** — identical assert, same rate (now
just cycling much faster). This conclusively ruled out tuning
duration/timing-race as the cause. Also ruled out the L2 cache
line/capacity settings (tested both Waveshare's 256KB/128B and ESP-IDF
defaults — crash identical either way). So the crash is tied specifically
to `CONFIG_SPIRAM_SPEED_200M` being active, independent of how it's
reached.

**Root-caused via direct instrumentation** (temporary printfs, since
removed) rather than further speculation: the crash —
`assert failed: xTaskCreateStaticPinnedToCore freertos_tasks_c_additions.h:299
(xPortcheckValidStackMem(puxStackBuffer))` at FreeRTOS scheduler start —
is FreeRTOS's own second-core IDLE TASK failing a stack-memory validity
check. Traced through `vApplicationGetIdleTaskMemory()`
(`components/freertos/port_common.c`), which allocates the idle task's
stack via a generic, uncapped `pvPortMalloc()`. At 200MHz specifically,
this allocation lands in **TCM** (Tightly-Coupled Memory) — a real,
valid, always-accessible on-chip memory region, registered in the heap
allocator's own capability table with `MALLOC_CAP_INTERNAL`
(`components/heap/port/esp32p4/memory_layout.c:60`). But
`xPortcheckValidStackMem()` (in `components/freertos/heap_idf.c`)
rejects it — and here's the real gap: our config has
`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y` (confirmed via generated
sdkconfig — not something we set explicitly; a pre-existing default),
which makes that check compile down to a single call:
`return esp_ptr_byte_accessible(ptr);` (confirmed via disassembly of the
compiled object — a 2-instruction wrapper, no branching). That function
(`components/esp_hw_support/esp_memory_utils.c`) checks
`SOC_BYTE_ACCESSIBLE_LOW/HIGH`, RTC memory, and PSRAM — but **never
TCM**, despite `esp_ptr_in_tcm()` existing right there in the same
header for exactly this purpose. A genuine, upstream ESP-IDF gap for
chips with TCM (ESP32-P4), not something specific to our board.

(First attempt patched the WRONG function — `esp_ptr_internal()` — based
on incomplete reasoning about which check was actually active; it has
its own identical TCM gap and might independently be worth fixing
upstream someday, but it's dead code under our `ALLOW_EXT_MEM=y` config,
so patching it alone did nothing. Also hit a confusing false trail where
edits appeared not to take effect — turned out to be exactly this dead-
code issue, not a build-system staleness bug as initially suspected;
worth remembering if this happens again: check preprocessor conditions
before assuming the build system is broken.)

**Fix**: patched `esp_ptr_byte_accessible()` to also check
`esp_ptr_in_tcm()`, via `scripts/patch_espidf_builder.py` (marker
`[patched 2026-07-13] esp_ptr_byte_accessible TCM fix (vehicle-dashboard)`
— reproducible, not a stray framework edit; user explicitly prefers the
patch-script approach over hand-editing files outside `src/`). **Verified
5/5 clean boots at 200MHz with the fix in place** (vs. 100% reproducible
crash without it, across many tests this session). This is a genuine,
standalone bug fix — reusable regardless of what happens with the
display investigation.

**Checked whether 200MHz also resolves the GDMA-starvation problem from
the earlier PHY-trigger breakthrough section — partially, and revealed a
new, more precisely localized gap.** With 200MHz PSRAM + the TCM fix +
our proven PHY-trigger config (`WS_DSI_LANE_MBPS=1500`,
`WS_DPI_CLK_MHZ=75`): **zero underrun errors** across a 75-second, 30-
real-frame capture — the first time all session real continuous
rendering has produced NO `lcd.dsi.dpi: can't fetch data from external
memory fast enough` errors at all. Real, meaningful progress — confirms
200MHz genuinely gives GDMA enough bandwidth to keep the framebuffer
pipeline fed without complaint.

**But `MIPI_DSI_HOST.phy_status` is still permanently stuck
(`0x000015BD`, all lanes in stop-state) and `vid_pkt_status` still shows
the DSI bridge's own buffer permanently EMPTY (`0x00010005`) across all
30 frames** — unlike the pattern generator, which reliably transitions
this same buffer to FULL and produces real bursts at the identical
lane-rate/DPI-clock settings. This is a new, sharper distinction: "zero
underrun" (the LCD/DPI peripheral's own fetch-from-PSRAM stage
succeeding) is NOT the same thing as "the DSI bridge's buffer receiving
that data" (a later, separate stage that feeds the actual PHY). GDMA/
PSRAM now successfully feeds the framebuffer pipeline — but that data
still isn't reaching the bridge that drives the physical lanes. This is
a different, more specific gap than "bandwidth starvation," now that
bandwidth is provably no longer the constraint.

**Resting state as of this writing**: `CONFIG_SPIRAM_SPEED_200M=y` +
`CONFIG_IDF_EXPERIMENTAL_FEATURES=y` in `sdkconfig.defaults` (200MHz,
with the TCM crash fix active via the patch script — this combination is
now considered STABLE and worth keeping, unlike earlier in the session).
`WS_DSI_LANE_MBPS=1500`, `WS_DPI_CLK_MHZ=75`, original porches.
`DSI_PATTERN_GEN_TEST` disabled (was re-enabled temporarily for this
test, now off again for normal rendering). `PANEL_DIAG_READ_TEST` and
`DSI_HOST_STATUS_TEST` still enabled (harmless, useful diagnostics).
Boots clean, zero underrun, zero crashes — but panel still shows
nothing, and the DSI bridge buffer-empty finding above is the new,
sharpest lead for next steps.

**Next steps, updated priority order:**
1. Figure out why the DSI bridge's buffer stays empty despite GDMA
   successfully feeding the framebuffer with zero underrun — something
   between the LCD/DPI peripheral's successful fetch and the bridge
   actually consuming/using that data. Worth checking GDMA descriptor/
   linked-list configuration specifically (is it actually wired to feed
   the bridge, or just a local framebuffer copy?), and whether there's a
   separate bridge-side "start"/enable trigger distinct from what
   `dpi_panel_init()` already does.
2. Oscilloscope session — still valuable, now with an even cleaner
   before/after: pattern-generator (proven bursting) vs. real rendering
   (proven zero underrun but zero bridge activity) at the identical
   electrical settings.
3. Validate the diagnostic register-read methodology against Tab5, if
   available — still an open blind spot.

## 2026-07-13 (continued): reading a community DSI reference project — one theory ruled out, ESP-IDF version question reopened carefully

User found and shared notes citing `embenix/ESP32-P4-DSI-Support-Hub`
(verified real, genuinely Waveshare-panel-relevant — supports Waveshare/
Luckfox/DFRobot/SeeedStudio DSI panels on ESP32-P4) and
`PavelMostovoy/ESP32-P4-minimal-DSI-demo` (also verified real). Fetched
the DSI-Support-Hub repo's README content:
- **Claims ESP-IDF v5.5.3+ or v6.0 required**, with v6.0 needing
  "modified components... for compatibility with ESP-IDF v6.0's new
  color/display API" (matches what we'd already seen in Waveshare's own
  BSP source — `esp_lcd_dpi_panel_config_t` gaining `.in_color_format`
  vs. our `.pixel_format` at IDF 6.0).
- **Cites 600–1500 Mbps as their own tested-stable lane bitrate range**
  — corroborates our 1500Mbps PHY-trigger finding is a reasonable value,
  not an outlier.
- **Warns: "fully power-cycle the system (board and display) to clear
  any latched state in the panel or bridge"** when switching DSI
  configs on the same panel. **Tested — user did a full power cycle
  (not just a soft/RTS reset). No change.** This rules out "latched
  bridge/panel state" as an explanation, cleanly. We've changed DSI
  config dozens of times this session via soft resets only, so this was
  worth checking properly — now closed.

**On the ESP-IDF version claim — user correctly pushed back before I
went further**: we already tried upgrading to ESP-IDF 5.5.4 on
2026-07-11 (see "ESP-IDF version upgrade attempt" section above) and
hit a link failure (`_bss_start_low`/`_heap_start_high`/etc. undefined).
Sent a fork to check whether that's specific to 5.5.4 or already present
in 5.5.3 (the version the community repo actually recommends).

**Fork's result: the original diagnosis was wrong, and the practical
path is narrower than hoped.** Directly compared ESP-IDF's own source
(`components/esp_system/port/cpu_start.c`,
`components/esp_system/ld/esp32p4/sections.ld.in`) across the 5.4.2,
5.5.3, and 5.5.4 git tags — **the low/high memory-region symbols are
identical in all three**, including our currently-working 5.4.2. So
"5.5.4 introduced a new memory-region split" (the 2026-07-11 write-up's
stated cause) is not accurate — that structure has existed since at
least 5.4.2. The link failure is more likely a **pioarduino packaging/
SCons-integration quirk specific to that one platform release
(`55.03.39`)**, not a fundamental ESP-IDF architecture incompatibility.
That's actually mild good news for tractability, if anyone wants to
dig into it properly later.

**But it doesn't matter for "just try 5.5.3" specifically — no
pioarduino release pins exactly 5.5.3.** Checked
`sivar2311/platform-espressif32-versions`'s mapping table: releases go
...→ 55.03.36/55.03.37 (ESP-IDF 5.5.2) → 55.03.38/55.03.39 (ESP-IDF
5.5.4) — **5.5.3 is skipped entirely** in pioarduino's release
lineage. Real options are 5.5.2 (below the community repo's stated
5.5.3 minimum, untested with our build, may lack whatever DSI fix
matters) or 5.5.4 (confirmed link failure, though now understood as
likely a fixable packaging quirk rather than a hard wall). Toolchain
pin (`platformio/toolchain-riscv32-esp @ 14.2.0+20241119`) breaks
immediately on either newer platform (`Missing toolchain directory
'None'`) — same as 2026-07-11, would need dropping again regardless of
which version is tried.

**Status: not attempted, awaiting user decision** — genuinely more
uncertain than the original "just try 5.5.3" framing suggested. Given
this project's history with build-system rabbit holes, don't proceed
without explicit user sign-off on which path (if any) to pursue.

**Power-cycle test methodology note**: user's power-cycle sequence —
display bench supply off first, then P4-Nano; reverse order (P4-Nano
first, then display) powering back up — is deliberate, not incidental,
per the DSI-cable-backfeed issue documented in
[[project_display_power_wiring]] (display backfeeds the P4-Nano's
ESP_3V3 rail via the DSI cable if sequenced wrong). Confirms the
power-cycle test that showed "no change" (ruling out the latched-state
theory above) was done correctly, not confounded by improper
sequencing — the negative result stands as reliable.

## 2026-07-13 (continued): ESP-IDF 5.5.4 attempted on a branch — confirmed same link failure; TCM fix corrected (was incomplete)

Per user's explicit request, committed the session's work to `gvret`
(commit `d866c29`) and created branch `esp-idf-5.5-upgrade` to test
5.5.4 without risking the working state.

**5.5.4 attempt: failed identically to 2026-07-11.** Same undefined
references (`_bss_start_low` etc.), toolchain pin had to be dropped
again. Checked for an ESP-IDF 6.x pioarduino option per user's fallback
suggestion — **none exists**; the "6.x" entries in pioarduino's version
table are the old platformio-native `espressif32` package's own
numbering, mapping to ancient ESP-IDF 4.4.x (confirmed the naming
collision the fork had already flagged). Committed the failed attempt
to the branch (`a340581`) for whoever wants to actually solve the
linker script gap later, and returned cleanly to `gvret`.

**Important correction to the 200MHz TCM fix, found immediately after
returning to `gvret` for a final sanity check**: the `esp_ptr_byte_
accessible()` patch alone was INCOMPLETE. A fresh rebuild+flash
produced a *different* assert — `xPortCheckValidTCBMem` instead of
`xPortcheckValidStackMem` — same TCM root cause, but hitting the idle
task's TCB buffer instead of its stack buffer this time.
`xPortCheckValidTCBMem()` has no `ALLOW_EXT_MEM` bypass (unlike the
stack check), so it unconditionally requires `esp_ptr_internal(ptr)` —
which still didn't recognize TCM, since that fix was reverted earlier
in the session under the (now proven wrong) assumption it was dead
code. **Added the companion `esp_ptr_internal()` patch to
`scripts/patch_espidf_builder.py`** (marker `[patched 2026-07-13]
esp_ptr_internal TCM fix`). **Verified 5/5 clean boots at 200MHz with
both fixes together** — this is now the actually-complete fix. Lesson:
when a fix appears to work, a handful of resets isn't necessarily
enough if the failure can land on more than one code path with the same
root cause — worth remembering before declaring victory too early next
time.

Committed as `c35f49d`. Board and `gvret` branch left in this
confirmed-working state (200MHz PSRAM, both TCM patches, 1500Mbps/75MHz
DSI PHY-trigger config) for whenever the session resumes.

## 2026-07-13 (continued): exhaustive DSI Bridge register investigation — every config knob checked correct, real gap now precisely bounded

Per user's request, went back to diagnosing the DSI bridge problem (why
`MIPI_DSI_HOST.vid_pkt_status`'s shared buffer stays empty under real
rendering despite the DPI peripheral feeding data with zero underrun at
200MHz) — and to specifically compare our 5.4.2 bridge driver code
against the locally-cached ESP-IDF 5.5.3/6.0.0 copies (found already on
this machine from unrelated prior work — see
`~/.platformio/packages/framework-espidf@3.50503.0` = 5.5.3,
`@4.60000.0` = 6.0.0, confirmed via `esp_idf_version.h`).

**Diffed `esp_lcd_panel_dpi.c` between 5.4.2 and 5.5.3.** Two real
differences found:
1. 5.5.3 adds a genuinely separate, dedicated CPU-level interrupt
   handler for the bridge's own IRQ line (`esp_intr_alloc(soc_mipi_dsi_
   signals[bus_id].brg_irq_id, ...)`, `mipi_dsi_bridge_isr_handler()`).
   5.4.2 only reads/clears the bridge's interrupt status register as a
   side effect of the *unrelated* DMA-transfer-completion callback —
   never registers a real ISR on the bridge's own interrupt source at
   all.
2. 5.5.3 splits color format configuration into separate
   `mipi_dsi_brg_ll_set_input_color_format()` /
   `_set_output_color_format()` calls. 5.4.2 only has
   `mipi_dsi_brg_ll_set_input_color_space()` — confirmed via LL header
   diff that `set_output_color_format()` doesn't exist in 5.4.2's API
   surface at all.

**Traced the actual hardware register layout** (`soc/esp32p4/register/
soc/mipi_dsi_bridge_struct.h`, `hal/esp32p4/include/hal/mipi_dsi_brg_ll.h`)
to understand exactly what these differences touch, found the bridge's
register struct is directly accessible via `extern dsi_brg_dev_t
MIPI_DSI_BRIDGE;` (same pattern as `MIPI_DSI_HOST`), and built a
comprehensive live diagnostic (`DSI_BRIDGE_STATUS_TEST` build flag) to
read every relevant bridge register directly off real hardware, both
right after init and during real ongoing rendering. Findings, all
checked and ruled out one by one:

- **`pixel_type.raw_type = 2` (RGB565) — already correct.** The
  "missing output color format" theory from the diff doesn't hold up on
  real hardware: `set_input_color_space()` (the one call 5.4.2 does
  make) already lands on the correct RGB565 encoding, and this chip's
  actual register (per the SOC header) only has one shared `raw_type`
  field, not separate input/output type fields — the 5.5.3 API split
  may be cosmetic/for-a-different-variant rather than fixing a real gap
  here.
- **`en.dsi_en=1`, `dpi_misc_config.dpi_en=1`** — both master-enable and
  DPI-output-enable bits correctly set (confirmed via source: both are
  set at panel *creation* time, in the function that becomes
  `esp_lcd_new_panel_dpi`'s core, well before `dpi_panel_init()` runs;
  the only place either gets cleared is `dpi_panel_del()`, never
  called).
- **All four bridge-side timing registers (`dpi_v_cfg0/1`,
  `dpi_h_cfg0/1`) exactly match our real panel config** — vtotal=2006,
  vdisp=1920, vsync=18, vbank=4, htotal=752, hdisp=720, hsync=10,
  hbank=12 — every single value matches our `WS_HSYNC_*`/`WS_VSYNC_*`
  defines and the documented 720×1920 panel geometry precisely. No
  timing mismatch between what the bridge thinks the frame geometry is
  and what we actually configured.
- **`raw_num_cfg.raw_num_total = 345600`** — exactly matches
  720×1920×16bpp/64, confirming `mipi_dsi_brg_ll_set_num_pixel_bits()`
  correctly computed our real frame size.
- **`mem_clk_ctrl` (two "force FIFO clock on" bits, both default 0) —
  forced both bits on directly, zero effect.** Ruled out dynamic
  clock-gating as the cause.
- **`dpi_lcd_ctl` (real standard DPI protocol signals: `dpishutdn`,
  `dpicolorm`, `dpiupdatecfg`) all read 0** — `dpishutdn` in particular
  would directly explain "display told to stop" if set, but it isn't.
- **`raw_buf_credit_ctl` at hardware default** — confirmed irrelevant
  per its own register description ("valid only when dsi_bridge as
  flow controller"); we use DMA as flow controller
  (`mipi_dsi_brg_ll_set_flow_controller(..., MIPI_DSI_LL_FLOW_
  CONTROLLER_DMA)`), so this register's value doesn't matter for us.
- **Installed a real, dedicated interrupt handler on the bridge's own
  IRQ line from app code directly** (`esp_intr_alloc(ETS_DSI_BRIDGE_
  INTR_SOURCE, ...)` — the interrupt source constant is a real hardware/
  interrupt-matrix definition, confirmed present in 5.4.2 too, so this
  didn't need a framework patch). Allocation succeeded (`ESP_OK`), but
  **the ISR never fires** (`isr_count=0` across 30 real frames) — this
  register only has an `underrun` interrupt source, no "packet
  transmitted"/"drain complete" event exists to hook, so a dedicated
  ISR wouldn't affect data flow even if wired up. Theory ruled out.

**What we now know for certain, with very high confidence:** the
bridge's own internal raw pixel buffer (`fifo_flow_status.raw_buf_depth`)
IS actively receiving real data — sampled continuously fluctuating in
the 770-1021 range (out of ~1024 capacity) across 30 real frames, never
zero, never static — proving the full GDMA→DPI-peripheral→bridge-raw-
buffer chain works end to end, all the way up to the bridge's own input
stage. Every bridge-side configuration register we can find and
interpret (enable, format, timing, clocking, DPI protocol signals, flow
control, interrupts) is correctly configured and matches our real panel
geometry exactly. Yet `MIPI_DSI_HOST.vid_pkt_status` (the shared
buffer between the bridge's *output* and the Host's transmit path)
never receives anything, and the DSI PHY never bursts under real
rendering. One mildly suspicious observation: `raw_buf_depth` never
drops below ~770 across many samples — consistent with the buffer being
permanently backed up near its ceiling rather than genuinely cycling
through fill/drain, though this isn't independently confirmed as
meaningful (could just be a genuinely fast, healthy steady-state).

**Conclusion: the remaining gap is now about as precisely bounded as
register-level investigation without an actual hardware TRM can get
it.** It sits specifically in the bridge's internal raw-pixel-to-DSI-
packet conversion step — not in configuration (everything checked is
correct), not in an obviously-missing enable/interrupt/clock (all
checked and ruled out). This is either a genuine, undocumented silicon
errata specific to this ECO2 chip revision with no software workaround
we can find from public headers, or something that would only be
visible electrically (the oscilloscope session). **This is now the
strongest case yet for prioritizing the scope session** — every
software-side lever that can be identified from public ESP-IDF source
has been tried, tested, and ruled out or confirmed correct.

**Repo state**: `DSI_BRIDGE_STATUS_TEST` build flag added to
`[env:waveshare_debug_usb]`, diagnostic code left in place in
`waveshare_display.c` (register dump + `mem_clk_ctrl` force-on test +
dedicated bridge ISR allocation) for reuse in a future session.
Committed as `d62694c`.

## 2026-07-13 (continued): downloaded the actual ESP32-P4 TRM — three more theories checked and ruled out, one measurement-methodology question definitively closed

User correctly pointed out that everything read so far (`soc/*_struct.h`
header comments) is ESP-IDF's auto-generated register descriptions, not
Espressif's actual Technical Reference Manual — and asked whether any
of this related to the HX8399-C (it doesn't; confirmed the entire
investigation is host-side, the panel is proven healthy via LP-mode
reads and its own datasheet, already fully leveraged). Downloaded the
real TRM (`documentation.espressif.com/esp32-p4_technical_reference_
manual_en.pdf`, saved to scratchpad, `pdftotext -layout` extracted for
searching) and found Chapter 42 "MIPI DSI" has a genuine "Programming
Procedures" section (42.6) with real usage-sequence narrative, not just
bit-field descriptions. Checked three concrete new leads from it:

1. **42.6.3 "Release global reset" — the very first documented step of
   DSI Host bring-up.** Found a genuine, separate system-level reset
   control (`HP_SYS_CLKRST_RST_EN_DSI_BRG`, TRM confirms 0=released/
   1=held-in-reset) distinct from anything we'd checked in the DSI
   Bridge's own register block all session. Traced the actual release
   call (`mipi_dsi_ll_reset_register()` in `hal/esp32p4/include/hal/
   mipi_dsi_ll.h`, a clean assert-then-immediately-release pulse) and
   confirmed it IS called, early, in `esp_lcd_new_dsi_bus()`
   (`esp_lcd_mipi_dsi_bus.c:43`) — which we know runs first in our own
   `prv_panel_init()`. Ruled out.

2. **42.6.2/Table 42.4-4 "hsfreqrange" D-PHY frequency-range selection**
   — our 1500Mbps lane rate sits at the very top edge of the lookup
   table (1450-1500 → `hsfreqrange=111100`/decimal 60), a plausible
   off-by-one/boundary bug candidate. Checked `mipi_dsi_hal.c`'s actual
   selection code — a proper table-driven lookup with inclusive
   `start_mbps`/`end_mbps` bounds, not hand-rolled boundary math.
   Confirmed via an ALREADY-CAPTURED debug log line from earlier
   tonight (`HAL_LOGD("dsi_hal", "phy pll: ...")`, present in this
   build because it already runs at debug log level): our actual
   selected value was `hsfreqrange=60` — exactly matching the TRM's
   documented `111100` for 1450-1500Mbps. Also confirmed the PLL M/N
   math (`M=150, N=2`) computes to a mathematically exact 1500Mbps
   (`f_vco = M/N × f_ref = 150/2 × 20MHz = 1500MHz`, zero rounding
   error). Both ruled out — this calculation is correct.

3. **Measurement methodology — definitively closed, not just
   theorized.** TRM section 42.4.3.1.4 ("Control Mode") explicitly
   confirms LP-11/stopstate is the *normal resting state between
   transactions* — "the MIPI BD D-PHY remains in Control mode by
   default while driving the stop state LP-11 on the lines until a
   request is made. Any request must start from and end in this
   state." This directly validates a concern the user raised: if real
   HS bursts happen but are brief relative to our ~10-80ms polling
   interval, slow sampling would almost always catch the lines back at
   idle and wrongly read "always stuck" as "never even tries." Tested
   this directly and conclusively: added a tight, zero-delay polling
   loop (`DSI_TIGHT_POLL_TEST` build flag) — 100,000 back-to-back
   `phy_status`/`vid_pkt_status` reads, no `vTaskDelay` at all, fired
   right after a real `draw_bitmap()` call during active rendering.
   **Result: 0 out of 100,000 samples showed any burst activity or
   buffer-full state.** This rules out polling granularity as an
   explanation — not a measurement artifact, the PHY genuinely never
   attempts a single HS burst during real rendering, confirmed at a
   sampling density that would have caught even a very brief,
   legitimate burst with high probability.

**Where this leaves things**: every concrete, checkable theory
generated from the actual Espressif TRM (not just header-comment
guesswork) has now been tested and ruled out or confirmed correct:
global reset (released correctly), D-PHY frequency range and PLL math
(exactly correct), and the measurement methodology itself (confirmed
sound via tight polling, not an artifact of slow sampling). Combined
with the prior session's exhaustive bridge-register-configuration
sweep (all correct), this is now about as complete a software-side
case as can be built from public documentation. The TRM is saved
locally (`/tmp/.../scratchpad/esp32p4_trm.pdf` and `.txt` — NOTE:
scratchpad is session-scoped/ephemeral, re-download if starting a new
session and this becomes relevant again) and worth searching further
for anything genuinely missed (haven't yet read the full D-PHY
initialization chapter narrative end-to-end, only the specific
sections these three hypotheses pointed at) if picking this back up.

## 2026-07-13 (continued, per user's explicit go-ahead to keep doing methodical TRM work solo): one real behavioral change found — clock lane forced to continuous HS — but doesn't complete the fix; root cause narrowed to the exact TRM-documented handoff mechanism

Continued reading the TRM systematically rather than stopping. Checked
three more things:

1. **PLL loop-filter/charge-pump parameters (42.4.3.2, test codes
   0x10/0x11/0x12 — vcorange/icpctrl/lpfctrl)**: confirmed our driver
   never touches these at all (`grep` for 0x10/0x11/0x12 in
   `mipi_dsi_hal.c` — no matches). Per the TRM, these auto-default based
   on `hsfreqrange` when left untouched. **Ruled out by a logic shortcut,
   not more register-reading**: since the pattern generator successfully
   bursts at the exact same PLL/PHY/hsfreqrange configuration our real
   rendering uses, the PLL tuning is already empirically proven adequate
   — whatever "automatic" values are in effect are good enough, because
   we've seen real HS output at this exact frequency. This also
   reframes the whole investigation usefully: since PLL/PHY config is
   *identical* between the working pattern-generator path and the
   broken real-rendering path, the differentiator must be specifically
   in how bridge-sourced data reaches the transmit pipeline, not in PHY
   calibration at all.

2. **Burst vs Non-Burst video transmission mode (42.4.2.1)**: TRM warns
   the wrong mode selection "may [cause] pixel data to be lost, causing
   malfunction of the display peripheral" — a precise-sounding match to
   our symptom. Checked the actual enum
   (`mipi_dsi_ll_video_burst_type_t`) and confirmed
   `MIPI_DSI_LL_VIDEO_BURST_WITH_SYNC_PULSES` (what our driver selects)
   really is genuine Burst Mode, not a confusingly-named Non-Burst
   variant as the naming briefly suggested. Burst Mode is also the
   TRM's own recommended choice given our bandwidth margins (1500Mbps
   lane rate vs. ~585Mbps/lane minimum needed). Ruled out.

3. **42.4.3.1.5 "High-Speed Data Transfer" — direct TRM confirmation of
   the exact mechanism, matching this session's own independent
   localization.** Verbatim: "DSI HOST sends a request for High-Speed
   Data Transfer **after receiving data from DSI Bridge**." This is
   authoritative confirmation (not inference from register comments)
   that the Host does not autonomously decide to burst — it waits for
   the Bridge to hand data over, and only then requests HS transfer.
   Given everything already proven (bridge's raw buffer genuinely
   filling with real pixel data; Host correctly, blamelessly waiting
   for a handoff that never completes), this is exactly consistent with
   the whole night's findings, from the TRM's own authoritative
   language rather than our own inference.

**Found one genuine, reproducible behavioral change while investigating
the clock-lane state machine (42.4.3.1.5, `lpclk_ctrl` register).** Our
driver sets the clock lane to `MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO`
(`auto_clklane_ctrl=1`, `phy_txrequestclkhs=1`) in `dpi_panel_init()`.
Forced explicit continuous-HS mode instead
(`auto_clklane_ctrl=0`, same `phy_txrequestclkhs=1`) directly via a
raw register write. **Result: the clock lane (`clkstop` bit) left
stop-state and stayed there — consistently across 30 real-rendering
samples and a subsequent 90-second, zero-underrun extended capture.**
This is the first time all session any register write has produced a
persistent (not just momentary, as with the pattern generator) PHY
state change during real rendering. However: **data lanes never
followed** (`d0stop`/`d1stop` stayed at 1 for the entire 90-second
capture, zero exceptions) and `vid_pkt_status` never showed the buffer
filling. Checked for an analogous "force HS" mechanism for the data
lanes specifically (searched LL header, register struct, and TRM for
`forcetxstopmode`/`txrequestdatahs`/similar) — **none exists**, which
is actually expected and consistent, not a dead end: per the TRM
passage above, data-lane HS entry isn't a host-controllable "just force
it" setting the way the clock lane is — it's entirely gated behind the
bridge successfully handing off data, the same mechanism already
identified as the real blocker. The clock lane has its own independent
control path (continuous vs. non-continuous clock mode is a normal,
separate DSI configuration choice); the data lanes don't.

**Where this leaves things**: a genuine, reproducible, novel electrical
change was found and confirmed real (not a config-reading mistake —
the PHY register state persistently and measurably changed as a direct
result of our write). It doesn't fix the display on its own, and its
significance is that it validates our register access/understanding is
functioning correctly at a basic level, while also cleanly confirming
(rather than contradicting) the bridge-handoff root-cause localization
already established. The forced-clock-lane change is left in the code
(guarded by `DSI_BRIDGE_STATUS_TEST`) since it's real, harmless
(zero crashes, zero underrun across 90s), and worth keeping for a
future session that might find the actual bridge-handoff fix and want
to test combinations. Not committed yet — sitting as uncommitted
changes on `gvret` (the `HX8399_SELFTEST_RETRIGGER` panel self-test
re-check and this forced-clock-lane test) pending review.
