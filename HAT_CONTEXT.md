# ESP32-P4-Nano Hat — KiCad Design Context

## Goal
Design a "hat" PCB for the Waveshare ESP32-P4-Nano that consolidates all external vehicle
interface circuitry onto one board. The hat plugs onto the P4-Nano's expansion headers and
exposes all vehicle wiring via **screw terminals** (no .1" headers for external cabling).

This replaces several loose add-on boards currently wired point-to-point.

---

## Host Board
**Waveshare ESP32-P4-Nano**
- Schematic and pinout: https://www.waveshare.com/wiki/ESP32-P4-Nano
- The hat must mate with the P4-Nano's expansion connector(s) — verify exact header
  positions, pitch, and mechanical envelope from the Waveshare wiki before laying out
  the PCB outline.

---

## Subsystems to include on the hat

### 1. CAN Bus Transceiver
- IC: SN65HVD230 (3.3V CAN transceiver, already purchased and wired loose)
- ESP32-P4 signals: GPIO 53 = TWAI TX, GPIO 48 = TWAI RX
- External: CANH / CANL to a 2-position screw terminal (plus optional shield/GND terminal)
- Power: 3.3V from P4-Nano
- Optional: 120 Ω termination resistor footprint with a solder-bridge jumper to enable/disable

### 2. Electronic Parking Brake (EPB) Interface
- Signal-level only — no power switching required
- ESP32-P4 signals:
  - GPIO 6:  output, normally HIGH; pulse LOW ~200ms to "press" EPB button (open-drain to vehicle)
  - GPIO 2:  input + pullup, active-low — EPB green LED wire (brake RELEASED)
  - GPIO 3:  input + pullup, active-low — EPB red LED wire (brake APPLIED)
- External: 3-position screw terminal (OUT, GREEN, RED) + GND
- Protection: TVS or 5V zener clamp on each input; series resistor on output
- Note: GPIO 6 output drives a brown wire that sinks current to "press" the EPB button.
  The vehicle side is a NO momentary contact — an NPN transistor or small MOSFET to
  pull the wire to ground is appropriate. Do NOT rely solely on GPIO drive strength.

### 3. MagneRide Suspension Control (2 channels)
- Controls magnetic shock absorbers via PWM duty cycle at 25 kHz (LEDC peripheral)
- Each channel: 3–5 A at 12–14 V (vehicle battery voltage)
- ESP32-P4 signals: 4× PWM GPIO outputs — CH1=GPIO45, CH2=GPIO46, CH3=GPIO47, CH4=GPIO33
  (CH3/CH4 reserved for future scope; footprint and terminals included, unpopulated by default)
- Per-channel circuit:
  - MOSFET: D4184, AOD4184, or LR7843 (logic-level N-channel, TO-252 or similar)
  - Gate resistor: 10–22 Ω to limit switching transients
  - Flyback diode: 1N5408 or UF5408, placed as close to the shock connector as possible
  - Bulk capacitor across drain supply rail (e.g. 100 µF electrolytic) per channel
- External: per channel — 2-position screw terminal (12V switched output + GND return)
  Plus a shared 12V input screw terminal (fused externally, but add a polarity protection
  diode or P-FET in series if space allows)
- Thermal: MOSFETs dissipate ~0.5–1 W at full load with these parts; copper pour under
  thermal pad is sufficient, no heatsink needed at this current level
- Duty cycle range: ~35% = comfort (soft), higher = stiffer/sport
- Safety: firmware ramps duty gradually — no instant step changes

**GPIO assignment note:** All assignments are verified against the P4-Nano expansion header
pinout (ESP32-P4-NANO-details-inter.jpg). GPIOs 9 and 10 are NOT broken out on the headers
and must not be used. All pins below are confirmed present on the headers.

### 4. VSS (Vehicle Speed Sensor) Input
- Signal: reed switch pulse, ~12 pulses per meter of vehicle travel (verify with owner)
- ESP32-P4 signal: GPIO 5, input with internal pullup already configured in firmware
- External: 2-position screw terminal (VSS signal + GND)
- Protection: 5V zener + series resistor to handle any inductive spikes on the reed switch wire
- The reed switch is a dry contact — one side to GND, other side to GPIO 5 terminal

---

## Design Constraints

- **All external wiring uses screw terminals** — no .1" headers for anything that goes
  to the vehicle. Phoenix Contact or equivalent 3.5 mm or 5.0 mm pitch depending on
  current rating (use 5.0 mm for MagneRide power, 3.5 mm acceptable for signal lines).
- **P4-Nano header connectors**: use standard 2.54 mm female pin headers to mate with
  the P4-Nano's male expansion pins. Verify pin count and position from Waveshare wiki.
- **Power**: derive 3.3V from P4-Nano header (confirm available current). MagneRide 12V
  comes from vehicle via its own screw terminal — do not route through the P4-Nano.
- **Board outline**: size to match or slightly overhang the P4-Nano footprint. Confirm
  P4-Nano dimensions from Waveshare wiki (approximately 25.4 × 50.8 mm — verify).
- **Mounting**: plan for at least 2 M3 standoff holes aligned with P4-Nano mounting holes.
- **Labeling**: silkscreen all screw terminals clearly (signal name + polarity).

---

## GPIO Summary Table (verified against P4-Nano expansion header pinout)

| GPIO | Direction | Function                        | Terminal |
|------|-----------|---------------------------------|----------|
| 2    | IN        | EPB green LED (brake released)  | EPB screw term |
| 3    | IN        | EPB red LED (brake applied)     | EPB screw term |
| 5    | IN        | VSS reed switch                 | VSS screw term |
| 6    | OUT       | EPB button (active-low pulse)   | EPB screw term |
| 20   | IN        | Display backlight dimmer (ADC)  | Dimmer screw term |
| 45   | OUT       | MagneRide CH1 PWM               | 12V screw term |
| 46   | OUT       | MagneRide CH2 PWM               | 12V screw term |
| 47   | OUT       | MagneRide CH3 PWM (reserved)    | 12V screw term (DNP) |
| 33   | OUT       | MagneRide CH4 PWM (reserved)    | 12V screw term (DNP) |
| 48   | IN        | TWAI RX                         | internal (SN65HVD230) |
| 53   | OUT       | TWAI TX                         | internal (SN65HVD230) |
| 54   | —         | C6 Reset/EN (do not connect)    | — |

DNP = Do Not Populate; footprint present for future use.

---

## Vehicle Harness Interface (EVJ-55 Wiring Diagram)

The hat + P4-Nano assembly is represented in EVJ-55 as component `DashDisplay`
(label: `DashDisplay`, name: `ESP32-P4-Nano + Hat`). All 14 pins are wired:

| Pin | Function | Connected to in EVJ-55 |
|-----|----------|------------------------|
| 1 | Sw12v+ | F21 (Controls 5A fuse) OUT ← IGN+ |
| 2 | Gnd | Gnd bus |
| 3 | CANHi | CAN bus |
| 4 | CANLo | CAN bus |
| 5 | EPB Out | PBCtrl pin 5 (Button) |
| 6 | EPB Grn | PBCtrl pin 7 (LEDGreen) |
| 7 | EPB Red | PBCtrl pin 8 (LEDRed) |
| 8 | VSS | VSS Reed Switch (Signal) |
| 9 | MgRide CH1 | MgRideL (Out+) |
| 10 | MgRide CH2 | MgRideR (Out+) |
| 11 | MgRide CH3 | — (DNP) |
| 12 | MgRide CH4 | — (DNP) |
| 13 | Dimmer | Dimmer (Signal) |
| 14 | MagRide 12V+ | Bat+ (unswitched 12V for MOSFET supply) |

---

## First Steps for the KiCad Session
1. Fetch the P4-Nano schematic PDF and expansion header pinout from the Waveshare wiki.
   (GPIO assignments already verified against ESP32-P4-NANO-details-inter.jpg — see table above.)
2. Create or import a KiCad footprint for the P4-Nano header positions and board outline.
3. Place screw terminal footprints grouped by subsystem:
   - Power: Sw12v+, MagRide 12V+, GND (×multiple)
   - CAN: CANH, CANL (2-pos, 5.0 mm pitch)
   - EPB: Out, Green, Red, GND (4-pos, 3.5 mm pitch)
   - VSS: Signal, GND (2-pos, 3.5 mm pitch)
   - MagneRide CH1–CH4: Out+, GND per channel (2-pos, 5.0 mm pitch; CH3/CH4 DNP)
   - Dimmer: Signal, GND (2-pos, 3.5 mm pitch)
4. Route power planes: 3.3V (logic), 12V (MagneRide supply only), GND.
5. Place and route SN65HVD230, MOSFETs (D4184/AOD4184/LR7843), flyback diodes, gate resistors.
6. DRC + check thermal copper under MOSFET pads.
