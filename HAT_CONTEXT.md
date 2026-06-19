# ESP32-P4-Nano Hat — Design Context (Updated 2026-06-19)

## Goal
A 2-layer HAT PCB for the Waveshare ESP32-P4-Nano consolidating all vehicle interface
circuitry. Plugs onto P4-Nano expansion headers, exposes all vehicle wiring via LM3.5
clone 3.5mm pitch screw terminals (side-entry, pluggable).

---

## Physical Configuration

### Board
- **Size**: 99 × 85mm, 2-layer, JLCPCB Economic assembly (THT parts hand-soldered)
- **Origin**: Top-left corner at KiCad absolute (66.04, 44.45)
- **Pi HAT mounting holes**: M2.5, 58×49mm pattern centred on board
  - Top-left: (86.54, 62.45), Top-right: (144.54, 62.45)
  - Bot-left: (86.54, 111.45), Bot-right: (144.54, 111.45)
- **No Nano mounting holes** — P1/P2 friction + enclosure backshell retains Nano

### P4-Nano Orientation
- Component-side **DOWN**, rotated **90° CW**
- **P1** (26-pin) along **right edge** of Nano footprint, pin 1 top-right
- **P2** (26-pin) along **left edge** of Nano footprint, pin 1 top-right
- **DSI + Ethernet + USB-Host** exit **bottom-left** of Nano
- **USB-C** exits **top** of Nano (into top strip — clearance required)
- Nano body: 50×50mm, centred on board at approximately (115.54, 86.95)

### J1/J2 Header Positions (confirmed by physical measurement + 1:1 print)
- **J1 (P1, right edge)**: x=137.795, y=67.95 — on B.Cu, flipped
- **J2 (P2, left edge)**: x=93.285, y=67.95 — on B.Cu, flipped
- Pin 1 of both headers at y=67.95 (6mm from Nano top edge at y=61.95)
- Header span: y=67.95 to y=98.43 (13 pins × 2.54mm)
- **Tall female sockets required**: minimum 11.5mm height to clear USB-Host (14.1mm)

---

## Connector Layout

### Left Flank (x≈71, side-entry facing left, orientation=-90)
Chained together at 3.5mm pitch:
- **J_DISP_PWR1** (2-pos): y=75.3 — +5V/GND for display, GND=pin1, 75mm bare-wire stub
- **J_CAN1** (3-pos): y=82.3 — CANH/CANL/SHIELD
- **J_VSS_IN1** (3-pos): y=92.8 — VSS/GND/+12V

### Right Flank (x≈160.2, side-entry facing right, orientation=+90)
Chained together at 3.5mm pitch:
- **J_DIM1** (2-pos): y=121.2 — DIM/GND (ADC input, kept away from MR switching)
- **J_EPB1** (4-pos): y=114.2 — EPB_OUT/GRN/RED/GND (pin1 at top)

### Bottom Strip (side-entry facing down)
Chained at 3.5mm pitch, centred:
- **J_MR1–J_MR4** (four 2-pos) — SW_OUT/GND per MagneRide channel
- **J_12V_IN1** (2-pos) — +12V_INT/GND, FUSE 10A, + on pin1, PULL FUSE BEFORE USB

### Top Strip
- **J3 / J_STEMMA1** — JST-SH BM04B 4-pin STEMMA QT, I2C (SCL/SDA/3V3/GND)
  Future: Adafruit MCP23017 breakout for stalk/clock spring inputs

---

## Subsystem Zones (Top Strip, left to right)
1. **J_DISP area** — far left near FPC/display adapter. See "DSI Pass-Through
   Connectors" section below for the two FPC connectors and routing this
   zone now needs (added 2026-06-18, after discovering both candidate
   off-the-shelf 15-to-22-pin adapter cables had bad/wrong-standard wiring).
2. **CAN IC** — U1 SN65HVD230 SOIC-8
3. **3V3 BUCK** — U2 LMR14030SDDAR SOIC-8, L1 22uH SRR1260,
   C_IN1 10uF 1210, C_IN2 100nF 0402, C_OUT1 22uF 1210,
   C_OUT2/C_BOOT 100nF 0402, R_FB1 1MΩ, R_FB2 300kΩ
   Vout = 0.75 × (1 + 1000/300) = 3.25V ✓
4. **5V BUCK** — U3 LM2596S-5 TO-263-5, L2 68uH SRR1260,
   C2/C3 100uF radial THT, D2 SS34 SMA
   ON/OFF pin → GPIO4 (was hard-tied GND) + R_5V_EN1 10k pull-up to
   3V3 — fail-safe default OFF, firmware drives LOW to enable display power
5. **J3 STEMMA QT** — top strip right area

## Bottom Strip Zones
- **MagneRide MOSFETs**: Q1–Q4 AOD4184A TO-252, one per channel
  - Gate resistors R_MR1_G–R_MR4_G: 10Ω 0402 (C138066)
  - Gate pulldowns R_MR1_PD–R_MR4_PD: 10kΩ 0402
  - Flyback diodes D_MR1–D_MR4: UF5408 DO-201AD vertical CathodeUp THT
  - Bulk caps C_MR1–C_MR4: 100uF radial THT, one per MOSFET drain
  - F.Cu copper pour island +12V_INT under MOSFET drains + thermal vias to B.Cu
- **Power input**: D1 P4SMA40A TVS, D_REVP1 SS34 reverse polarity

## Right Flank Components (near J_EPB/J_DIM)
- **Q5** 2N7002 SOT-23 — EPB open-drain output
- **DZ1, DZ2** BZX84-C5V1 SOT-23 — EPB input clamps
- **R_EPB_G1** 1kΩ 0402 (C11702) — EPB gate
- **R_EPB_GRN1, R_EPB_RED1** 33Ω 0402 (C25105) — EPB input series
- **R_EPB_PD1** 10kΩ 0402 — EPB pulldown
- **D_Z_DIM1** BZX84C5V1W SOT-23 — dimmer ADC clamp
- **R_DIM_10K1** 10kΩ 0402 — dimmer pulldown
- **R_VSS_10K1** 10kΩ 0402 — VSS pulldown
- **D_Z_VSS1** BZX84C5V1W SOT-23 — VSS clamp

---

## Keepout Zones (to be placed in PCB editor)
- **USB-C clearance** (Eco1.User): above Nano top edge, x≈104–117, projects ~8mm up
- **ETH+USB-Host** (Dwgs.User note): bottom-left corner of Nano, exits downward
  approximately x=90.54–112, y=111.95–126 — avoid tall components here

---

## Power Architecture
- **+12V_INT**: vehicle battery via J_12V_IN, fused 10A externally
  - D_REVP1 reverse polarity protection
  - D1 TVS transient suppression
  - Feeds MagneRide MOSFETs and buck regulators
- **+3.3V**: LMR14030 buck from +12V_INT, ~1A capacity
- **+5V**: LM2596S-5 buck from +12V_INT, ~3A capacity → J_DISP_PWR
  - ON/OFF gated by GPIO4 (P1 pin12) + 10k pull-up to 3V3 (default OFF,
    firmware drives LOW to enable). Lets firmware power-cycle the display
    for hang recovery, and removes the display's onboard 3.3V regulator's
    source (stops it backfeeding ESP_3V3 via the DSI cable) when off.
- **GND**: B.Cu full pour
- **+12V_INT pour**: F.Cu island in MagneRide zone only

---

## GPIO Summary

| GPIO | Direction | Function | Net |
|------|-----------|----------|-----|
| 2 | IN | EPB green LED (brake released) | EPB_GRN |
| 3 | IN | EPB red LED (brake applied) | EPB_RED |
| 4 | OUT | Display 5V rail enable (active-low, 10k pull-up = default OFF) | 5V0_ENABLE |
| 5 | IN | VSS reed switch | VSS |
| 6 | OUT | EPB button (active-low pulse) | EPB_OUT |
| 7 | I2C SDA | GT911 touch (via DSI FPC, P1 pin3) — RESERVED, not a HAT signal | SDA |
| 8 | I2C SCL | GT911 touch (via DSI FPC, P1 pin5) — RESERVED, not a HAT signal | SCL |
| 20 | IN (ADC) | Display backlight dimmer | DIM |
| 32 | OUT | MagneRide CH4 PWM (DNP) | MR_CH4 |
| 45 | OUT | MagneRide CH1 PWM | MR_CH1 |
| 46 | OUT | MagneRide CH2 PWM | MR_CH2 |
| 47 | OUT | MagneRide CH3 PWM (DNP) | MR_CH3 |
| 48 | IN | TWAI RX | TWAI_RX |
| 53 | OUT | TWAI TX | TWAI_TX |

---

## BOM Summary (JLCPCB LCSC parts)

| Ref | Value | LCSC | Package |
|-----|-------|------|---------|
| U1 | SN65HVD230 | C12084 | SOIC-8 |
| U2 | LMR14030SDDAR | C182078 | SOIC-8 |
| U3 | LM2596S-5 | C194349 | TO-263-5 |
| Q1-Q4 | AOD4184A | C99124 | TO-252-2 |
| Q5 | 2N7002 | C8545 | SOT-23 |
| L1 | 22uH SRR1260 | C3911669 | SMD |
| L2 | 68uH SRR1260 | C2041353 | SMD |
| D1 | P4SMA40A | C506023 | SMA |
| D2, D_REVP1 | SS34 | C8678 | SMA |
| D_MR1-4 | UF5408 | C424502 | DO-201AD Vertical |
| DZ1, DZ2 | BZX84-C5V1 | C27131 | SOT-23 |
| D_Z_DIM1, D_Z_VSS1 | BZX84C5V1W | C27131 | SOT-23 |
| C_IN1, C_OUT1 | 10uF/22uF 1210 | C386170/C19659 | 1210 |
| C_IN2, C_OUT2, C_BOOT | 100nF 0402 | C49678 | 0402 |
| C2,C3,C_MR1-4 | 100uF | C2960218 | Radial THT |
| R_FB1 | 1MΩ 0402 1% | C138033 | 0402 |
| R_FB2 | 300kΩ 0402 1% | C138011 | 0402 |
| R_MR gate | 10Ω 0402 1% | C138066 | 0402 |
| R_MR PD, R_EPB_PD, R_DIM, R_VSS, R_5V_EN1 | 10kΩ 0402 | C25744 | 0402 |
| R_EPB_G | 1kΩ 0402 | C11702 | 0402 |
| R_EPB_GRN/RED | 33Ω 0402 | C25105 | 0402 |
| J3/J_STEMMA | JST SH BM04B | C424993 | SMD vertical |
| J1, J2 | 2×13 socket DNP | — | PinSocket_2x13_P2.54mm |
| J_DSI_NANO | BXCONN FC-10D15P11H25, 15P 1mm bottom contact, SMD right-angle | C23397345 | FPC ZIF |
| J_DSI_DISP | HanElectricity 05A20L22P, 22P 0.5mm bottom contact, SMD right-angle | C22435644 | FPC ZIF |

**All screw terminals**: DNP, LM3.5 clone 3.5mm pitch, hand-soldered from stock
**THT parts** (hand-solder): C2/C3/C_MR1-4 electrolytic caps, D_MR1-4 diodes, J1/J2 headers

---

## DSI Pass-Through Connectors (P4-Nano ↔ Display) — added 2026-06-18

**Why this exists:** the display only needs +5V/GND from the hat
(`J_DISP_PWR`) for power — the actual DSI video signal never needs to pass
near the hat at all electrically. But after burning a full day chasing a
DSI hang that turned out to be a bad/wrong-standard 22-pin-to-15-pin
adapter cable (see `CONTEXT.md` → "DSI Adapter Cable Pinout" for the full
story), routing the DSI signals *through* the hat — with the correct
mapping baked into PCB traces instead of relying on an off-the-shelf
cable's unstated internal wiring — turns one hard, error-prone cable
problem into two trivial ones.

### Connectors needed

- **J_DSI_NANO** — 15-pin, 1mm pitch FPC, matching the P4-Nano's own DSI
  connector (labeled "J1 / 15PIN--PI4B" on the Nano's schematic). Fed by a
  **short, straight-through, same-pitch, same-pin-count pigtail** from the
  Nano's DSI port — about the lowest-risk cable there is, easy to verify
  with one continuity check if in doubt. **Confirmed part (2026-06-18)**:
  BXCONN `FC-10D15P11H25`, LCSC `C23397345` — bottom contact, SMD
  right-angle, slide lock, 1455 in stock, ~$0.09/unit. (An alternative,
  Hirose/HDGC `1.0K-FX-15PWB` C2914074, has identical spec but only 3
  units in stock — passed over for that reason.)
- **J_DSI_DISP** — 22-pin, 0.5mm pitch FPC, matching the display's DSI
  connector. Takes whatever cable/tail comes off the Waveshare 12.3"
  display. **Confirmed part (2026-06-18)**: HanElectricity `05A20L22P`,
  LCSC `C22435644` — bottom contact (matches Waveshare's own Pi reference
  photo, confirmed contacts-down earlier this session), SMD right-angle,
  hinged-lid actuator, 753 in stock, ~$0.07/unit.
- Both connectors confirmed bottom-contact — consistent orientation
  convention between the two, which simplifies sourcing/checking the
  pigtail cables. Still worth a physical/datasheet double-check against
  the real Nano and display connectors before ordering in volume, the way
  the J1/J2 header positions were confirmed by 1:1 print earlier in this
  doc — but both picks are well-specified, in-stock, real parts now.

### Routing table (PCB traces between J_DSI_NANO and J_DSI_DISP)

**Verification history (2026-06-18):** the original table below this line
(now superseded) was derived from a Raspberry Pi forum post screenshot
claiming CM4IO pinout. Cross-checked against two independent primary
sources before trusting it for PCB traces:
1. Official CM4IO datasheet, Figure 4, page 10
   (`https://datasheets.raspberrypi.com/cm4io/cm4io-datasheet.pdf`) —
   read directly off the published schematic diagram.
2. P4-Nano's own schematic, 15-pin connector — read directly by Herb.
Both reads are structurally consistent (GND every 3rd/4th pin, SCL/SDA/3V3
grouped at one end) and physically consistent with the observed
bottom-contact/top-contact flip on the Waveshare-supplied cable (pin 1 on
one end mates with pin 22 on the other — full mirror, not a straight
pin-N-to-pin-N mapping). The forum-derived table was **wrong** — different
GND positions, different NC positions, SCL/SDA at opposite end. This combo
of bad table + unverified off-the-shelf cable is the likely root cause of
the DSI hang that cost a full day earlier in bring-up.

J_DSI_NANO is CM4IO/Pi5 DISP0 (2-channel, 2 data lanes: D0, D1 + CLK — no
D2/D3 broken out on this interface at all, consistent with the Nano's
15-pin connector only carrying D0/D1/CLK). Waveshare's display is
explicitly CM4-compatible (2-lane), so this is the correct interface for
this pairing — no need to chase the 4-lane DISP1/CAM1 pinout.

**J_DSI_NANO (15-pin, 1mm pitch, Nano-side):**

| Pin | Signal |
|---|---|
| 1,4,7,10,13 | GND |
| 2,3 | D1_N, D1_P |
| 5,6 | CLK_N, CLK_P |
| 8,9 | D0_N, D0_P |
| 11 | SCL |
| 12 | SDA |
| 14,15 | 3V3 |

**J_DSI_DISP (22-pin, 0.5mm pitch, display-side) — contact-side-corrected
(mirrored) mapping**, accounting for the bottom/top contact flip observed
on the Waveshare cable (pin layout is a full end-to-end mirror: pin N on
the as-read CM4IO figure corresponds to pin (23-N) once the connector is
flipped to bottom-contact to match J_DSI_NANO's orientation):

| Pin | Signal |
|---|---|
| 1 | 3V3 |
| 2 | SDA |
| 3 | SCL |
| 4,7,10,13,16,19,22 | GND |
| 5,6,8,9,11,12 | NC |
| 14,15 | CLK_P, CLK_N |
| 17,18 | D1_P, D1_N |
| 20,21 | D0_P, D0_N |

**Routing table (PCB traces), matched by signal name:**

Differential pairs — **polarity matters**, a P/N swap silently reintroduces
the same class of bug that cost a full day of debugging:

| J_DSI_NANO pin | Signal | → | J_DSI_DISP pin | Signal |
|---|---|---|---|---|
| 2 | D1_N | → | 18 | D1_N |
| 3 | D1_P | → | 17 | D1_P |
| 5 | CLK_N | → | 15 | CLK_N |
| 6 | CLK_P | → | 14 | CLK_P |
| 8 | D0_N | → | 21 | D0_N |
| 9 | D0_P | → | 20 | D0_P |

Single-ended:

| J_DSI_NANO pin | Signal | → | J_DSI_DISP pin | Signal |
|---|---|---|---|---|
| 11 | SCL | → | 3 | SCL |
| 12 | SDA | → | 2 | SDA |
| 14, 15 | 3V3 | → | 1 | 3V3 (both Nano 3V3 pins may land on the same pad/net) |

**GND**: bus J_DSI_NANO pins 1,4,7,10,13 together, bus J_DSI_DISP pins
4,7,10,13,16,19,22 together, single net tying the two buses. Same GND
plane as the rest of the board — no separate jumper needed once it's all
one net in the PCB editor.

**Leave unconnected**: J_DSI_DISP pins 5,6,8,9,11,12 (NC on this
2-channel/2-lane interface) — the Nano has no D2/D3 lanes, nothing on
J_DSI_NANO needs to land on these. Display is confirmed CM4-compatible
(2-lane) per Waveshare's own documentation, so this is expected and correct,
not a gap to chase down.

**Still to verify before trusting this for trace routing**: physically
confirm pin 1 location/orientation marking on both real connectors (not
just datasheet figures) — datasheet pin numbering conventions occasionally
differ from silkscreen/physical pin-1 marking. The hack breakout board
Herb is building should do exactly this as its first job, ideally with a
continuity buzzer back to the Nano's known-good GND pins as a sanity check
before any signal pins are trusted.

### Routing notes
- These are 950Mbps differential pairs (D0, D1, CLK) — keep P/N lengths
  matched within each pair, keep traces as short as practical, avoid
  unnecessary layer changes/vias on these specific traces if the layout
  allows it.
- Physical placement: near the existing "J_DISP area" (far left of top
  strip, per Subsystem Zones above) — that's already where the Nano's own
  DSI/Ethernet/USB-Host connectors exit (bottom-left of the Nano footprint,
  per Physical Configuration), so it's the natural place for J_DSI_NANO to
  sit close to its source.

---

## Current PCB Status (2026-06-17)
- Schematic: ERC clean
- PCB scaffold v0.5 generated, footprints imported via Update PCB from Schematic
- **Placement in progress** — partially placed, session ended mid-placement

### Placed and positioned:
- J1/J2 headers (B.Cu, positions confirmed by 1:1 print)
- J_DISP_PWR, J_CAN, J_VSS (left flank, chained)
- J_EPB, J_DIM (right flank, chained)
- J_MR1-4, J_12V_IN (bottom strip, chained)
- J3 STEMMA (top strip)
- Q1-Q4 MOSFETs (bottom strip, rough position)
- U1, U2, L1 (top strip, rough position)

### Still to place:
1. C_MR1-4 — move near Q1-Q4 in bottom strip (one per MOSFET drain)
2. U3 + L2 — move into 5V buck zone in top strip
3. Q1-Q4 + D_MR1-4 — group together, diodes tight to drains
4. Q5 + R_EPB_G/GRN/RED/PD + DZ1/DZ2 + D_Z_DIM1 — near J_EPB/J_DIM right flank
5. D1 TVS + D_REVP1 — near J_12V_IN
6. Define ETH/USB-Host/USB-C keepout zones on Dwgs.User/Eco1.User
7. All remaining passives (R_FB1/2, R_MR gate/PD, R_VSS, R_DIM, D_Z_VSS1, C_IN/OUT/BOOT)
8. **UPDATED (2026-06-19)**: J_DSI_NANO + J_DSI_DISP — place near existing J_DISP
   area (far left, top strip). Pinout for both connectors now verified
   against official Raspberry Pi CM4IO datasheet Figure 4 + P4-Nano's own
   schematic (see "DSI Pass-Through Connectors" section — supersedes the
   forum-screenshot-derived table that was wrong). **Before committing to
   PCB traces**: physically verify pin 1 / contact-side orientation on the
   hack breakout board first — this is the step that was skipped last time
   and cost a full day. Schematic not yet updated with these connectors —
   PCB placement paused until display bring-up (and breakout board
   verification) is complete.

### After placement:
- Routing: GND pour B.Cu, +12V_INT island F.Cu MagneRide zone
- Critical: SW node U2→L1 short fat trace, FB divider away from SW node
- Net classes already defined: Default 0.25mm, Power 1.0mm, 12V_Power 2.0mm
