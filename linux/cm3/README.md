# CM3 Debian bring-up

Getting the dashboard onto the **production compute**: a Radxa **CM3** (RK3566
quad-core, **2 GB RAM / 16 GB eMMC / WiFi**) on a Raspberry Pi **CM4 IO** board.
Chosen over the Pi 5 because it can **suspend-to-RAM** (instant-on) and sips ~2–3 W.

**Strategy: Debian first, Buildroot later.** Get the display + CAN + dashboard
working on Radxa's official (BSP-kernel) Debian, *then* bake a lean read-only
Buildroot image for the corruption-proof, fast-boot production state. Don't skip
to Buildroot — you'd be debugging DSI on a minimal image with no tooling.

This directory stages everything so day-one is flashing, not researching.

---

## The bring-up ladder

### 1. Base image
Radxa's official **Debian for CM3 / CM3-IO** (Rockchip BSP kernel — it has the
`simple-panel-dsi` DT init-sequence support and the `mcp251x` driver we need;
mainline does **not** have the DT-init panel path). Download, **decompress to a
raw `.img`** — `rkdeveloptool` does not decompress.

### 2. Flash to eMMC (maskrom + rkdeveloptool)
16 GB eMMC on-module, so flash it directly — no SD in the boot path.

```sh
# 1. Put the CM3 in MASKROM (hold the maskrom/boot pins on the CM4IO while
#    powering, or use the CM4IO boot switch). Then:
rkdeveloptool ld                 # expect: DevNo=1 ... Maskrom  Vid=0x2207,Pid=0x350a
# 2. Push the SPL loader, then write the image:
rkdeveloptool db rk356x_spl_loader_ddr1056_v1.12.109_no_check_todly.bin
rkdeveloptool wl 0 radxa-cm3-debian.img
rkdeveloptool rd                 # reboot
```
(Alt path: flash an SD, boot from it, then `dd` the image onto the eMMC from the
running system — handy for iterating before committing to eMMC.)

### 3. Display — the hard rung (but we're armed)
The panel is the same Waveshare 12.3" **HX8399-C**, 720×1920 portrait, that the
Pi 5 already drives. **We own the exact init + timings.** Land the FPC on the
CM4IO **DISP1** (4-lane) connector — **DISP0 is 2-lane-only** (learned the hard way).

Two paths, in order of preference:

- **A — `simple-panel-dsi` DT overlay (no kernel build).** `overlays/waveshare-12in3-dsi1.dtso`.
  The panel's `panel-init-sequence` is **generated** from our C init by
  `gen_panel_init_seq.py` → `overlays/hx8399-init-seq.dtsi` (47 commands, no hand
  transcription). Timings/format/lanes are filled; only board plumbing (which DSI
  controller = DISP1, panel reset GPIO, backlight, VOP2 route) is TODO. **Start here.**
- **B — port the Pi panel driver.** `raspberrypi/linux .../panel-waveshare-dsi-v2.c`
  (`ws_panel_12_3_a_4lane_init`) is a self-contained DRM panel driver — build it
  as a module against the Radxa BSP kernel. Reuses the exact code; falls back to
  this if the DT-init path misbehaves.

**Rotation:** UI is 1920×720 landscape; panel is 720×1920. As on the Pi, rotate
270° in software via **fbdev** (LVGL's fbdev flush rotates; DRM can't). Alternatively
a DRM plane rotation / `rotation` panel prop — decide once the panel lights.

Regenerate the init any time the C changes:
```sh
python3 gen_panel_init_seq.py > overlays/hx8399-init-seq.dtsi
```

### 4. CAN
`overlays/mcp2515-cm3.dtso` — MCP2515 on **SPI3**, INT on **GPIO0_C3** (HAT pin 15),
12 MHz crystal (current RS485-CAN HAT; the final 2-CH HAT+ is 16 MHz). Confirm the
`spi3m0` pinmux group name against the base DTS, then:
```sh
sudo ip link set can0 up type can bitrate 500000
candump can0
```

### 5. Dashboard app
Builds natively on the CM3 (aarch64) — it's already generic Linux:
```sh
cd linux && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
```
fbdev `/dev/fb0` + evdev touch + SocketCAN `can0` + HTTP `:8080`. Watch the fb
geometry/rotation; touch is GT911 auto-detected.

### 6. Appliance
Adapt `../deploy/` (currently Raspberry-Pi-OS specific):
- systemd `dashboard.service` — carries over unchanged (path substitution).
- Boot config — Radxa/Debian uses **U-Boot + extlinux**, *not* `/boot/firmware/cmdline.txt`;
  console-blanking/quiet flags go in `/boot/extlinux/extlinux.conf` or the U-Boot env.
- Plymouth — theme carries; enabling early-splash differs (initramfs on Radxa BSP).
- **Falcon mode** (SPL boots the kernel directly, skipping full U-Boot) is the
  fast-boot lever for instant-on — being explored separately; keep the DT/rootfs
  Falcon-compatible.

---

## What we already have (assets)
- **HX8399 init + timings** — from the Pi driver, via `src/waveshare_display.c`,
  emitted to DT by `gen_panel_init_seq.py`. This is the crown jewel; it's what
  turned the P4 DSI saga from months into a known quantity.
- **The dashboard app** — generic Linux, builds on aarch64 as-is.
- **CAN map** (`zombie_can_map.*`) and the **deploy bundle** (`../deploy/`, to adapt).

## Open questions (resolve on hardware / from the Radxa CM3 CM4IO DTS)
1. Which RK3566 DSI controller is wired to the CM4IO **DISP1** (4-lane)? (`&dsi1`?)
2. Radxa CM3 Debian kernel version — confirm it's the BSP kernel with
   `simple-panel-dsi` init-sequence + `mcp251x`.
3. Panel **reset** GPIO + **backlight** enable routing (CM4IO vs HAT).
4. `spi3m0` pinmux group name; CS0/CS1 routing.
5. Rotation: fbdev software (proven) vs DRM plane vs panel `rotation` prop.
6. Falcon-mode SPL config for CM3 U-Boot (Herb's thread).

## Files here
| File | Purpose |
|------|---------|
| `gen_panel_init_seq.py` | C init array → Rockchip `panel-init-sequence` DT bytes |
| `overlays/hx8399-init-seq.dtsi` | generated init sequence (regenerate; don't hand-edit) |
| `overlays/waveshare-12in3-dsi1.dtso` | DSI panel overlay (panel done, board plumbing TODO) |
| `overlays/mcp2515-cm3.dtso` | MCP2515 CAN on SPI3 |

## Sources
- [Radxa CM3 IO USB install (maskrom)](https://wiki.radxa.com/Rock3/installusb-install-radxa-cm3-io)
- [rkdeveloptool — Radxa Docs](https://docs.radxa.com/en/compute-module/cm5/radxa-os/low-level-dev/rkdeveloptool)
- [Radxa install-OS](https://docs.radxa.com/en/rock3/rock3c/getting-started/install-os)
- [Rockchip `simple-panel-dsi` / `panel-init-sequence` binding](https://patchwork.kernel.org/project/linux-rockchip/patch/1468984730-23186-2-git-send-email-mark.yao@rock-chips.com/)
- Panel init origin: `raspberrypi/linux` `drivers/gpu/drm/panel/panel-waveshare-dsi-v2.c` (`ws_panel_12_3_a_4lane_init`)
