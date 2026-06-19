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
