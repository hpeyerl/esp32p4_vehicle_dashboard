# Pi5 proven-working display + touch reference

These files are the **actual, proven-working** Waveshare 12.3" DSI panel + GT911
touch configuration captured from the **Raspberry Pi 5** boot partition
(`/media/hpeyerl/bootfs`) on 2026-08-23. They are kept here **verbatim** so the
GT911 touch bring-up never has to be re-derived again.

Touch **only ever worked on the Pi5.** It never worked on the ESP32-P4, and (as
of this writing) does not yet work on the Radxa CM3. The ESP32-P4 firmware's
touch code (`src/waveshare_display.c`) is an **unproven attempt** — do not treat
it as a reference for touch. This directory is the reference.

## Files
- `pi5-config.txt` — the Pi5's `/boot/firmware/config.txt`.
- `pi5-cmdline.txt` — kernel cmdline.
- `vc4-kms-dsi-waveshare-panel-v2.decompiled.dts` — `dtc -I dtb -O dts` of
  `/boot/firmware/overlays/vc4-kms-dsi-waveshare-panel-v2.dtbo`, the overlay the
  Pi5 loaded. **This is the ground truth for touch.**

## What actually enabled touch on the Pi5

`config.txt` loads:

```
dtoverlay=vc4-kms-dsi-waveshare-panel-v2,12_3_inch_a_4lane,dsi0
```

That overlay declares two I²C devices on the DSI connector's I²C bus:

```dts
display_mcu@45 {
    compatible = "waveshare,touchscreen-panel-regulator";
    reg = <0x45>;
    gpio-controller;          // <-- the panel MCU is a kernel GPIO controller
    #gpio-cells = <2>;
    enable-gpio = <&display_mcu 2 0>;
};

goodix@5d {
    compatible = "goodix,gt9271";
    reg = <0x5d>;
    reset-gpio = <&display_mcu 9 0>;   // <-- the ONLY touch control line: RESET
};
```

### The key facts (confirmed, not theory)

- The panel MCU at **I²C 0x45** is exposed to the kernel as a **GPIO
  controller** via the `waveshare,touchscreen-panel-regulator` driver.
- The GT911 has **exactly one control line: RESET = MCU GPIO line 9**
  (active-high). It has **no `irq-gpios`, no interrupt of any kind.** The GT9271
  simply answers at its default address **0x5D**.
- The MCU GPIO line map (cross-confirmed by the panel node in the same overlay
  and by the ESP32-P4 register defines):

  | MCU GPIO line | function    | register / bit (MCU 0x45) |
  |---------------|-------------|---------------------------|
  | 0             | AVDD        | reg 0x95 bit0             |
  | 1             | LCD reset   | reg 0x95 bit1             |
  | 2             | BL enable   | reg 0x95 bit2             |
  | 4             | IOVCC       | reg 0x95 bit4             |
  | **9**         | **GT911 reset** | **reg 0x94 bit1** (high byte; `0x94=0x03` ⇒ released) |

## Why touch is broken on the CM3, and how to fix it

On the Pi5 the **goodix driver pulses reset line 9 when it probes**, which boots
the GT911. On the CM3:

- the touch node (`goodix,gt9xx`) declares **no `reset-gpio`**, and
- nothing else pulses MCU line 9 (`ws-panel-power.sh` just leaves `0x94=0x03`
  statically high).

So the GT911 is **never reset, never boots, and NAKs** on every I²C read. That is
the entire bug — it is **not** power ordering, **not** an interrupt, **not** an
I²C address strap. (Those were all wrong guesses made along the way.)

### Fix, in order of preference

1. **Reversible test first (no reboot).** With panel power already up, pulse
   reset line 9 and read the chip's product ID:

   ```
   i2cset    -y -f 0 0x45 0x94 0x01          # line 9 low  = reset asserted
   i2cset    -y -f 0 0x45 0x94 0x03          # line 9 high = reset released
   i2ctransfer -y -f 0 w2@0x5d 0x81 0x40 r4  # expect  39 32 37 31  ("9271")
   ```

   If it returns `39 32 37 31`, the chip is alive and the root cause is confirmed.

2. **Quick permanent fix:** add that reset pulse to `ws-panel-power.sh` *before*
   the goodix driver binds.

3. **Proper permanent fix:** replicate this overlay on the CM3 — declare
   `display_mcu@45` (as `waveshare,touchscreen-panel-regulator`, `gpio-controller`)
   plus `goodix@5d` with `reset-gpio = <&display_mcu 9 0>`, and let the driver do
   the reset itself. **Blocker / open question:** this needs the
   `waveshare,touchscreen-panel-regulator` kernel driver present in the CM3's
   Rockchip 6.1.84 BSP kernel. It is **probably absent** — the CM3 drives the MCU
   from the userspace `ws-panel-power.sh`, which is exactly what you'd do if the
   kernel driver weren't available.

After the chip answers, the remaining work is the 270° touch-coordinate rotation
to match the display.

## CAN — do NOT use this as the dual-CAN reference

`config.txt` also shows the Pi5's CAN setup:

```
dtoverlay=mcp2515-can0,oscillator=12000000,interrupt=25
```

**The Pi5 never had the dual-CAN HAT** — this is a single-channel MCP2515 at
12 MHz. It is here only for completeness. It is **not** authoritative for the CM3,
which runs a 2-channel MCP2515 setup at a different oscillator frequency. See the
CM3 CAN DTS notes for the real dual-CAN configuration.
