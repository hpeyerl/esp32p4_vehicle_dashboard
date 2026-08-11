# CM3 day-one flash & bring-up runbook

Copy-paste sequence for when the Radxa CM3 lands. Companion to `README.md` (which
explains *why*); this is the *what to run*. Prepped 2026-08-11.

## 0. Host prep (dev box, x86_64) — DONE / staged
- **`rkdeveloptool` built** (v1.32) → `~/bin/rkdeveloptool` (not in apt; built from
  `rockchip-linux/rkdeveloptool`, libusb-1.0-0-dev already present). Ensure `~/bin`
  is on `PATH`. **Maskrom USB needs root** → run flash cmds with `sudo`, or install a
  udev rule for Vid `2207`.
- **Image downloading** → `~/cm3-flash/radxa-cm3-bookworm-kde-r2.img.xz`
  (+ `image.sha512sum`). This is the `rsdk-r2` bookworm KDE image we inspected
  (kernel 6.1.84 BSP: `CAN_MCP251X=m`, `DRM_ROCKCHIP`, `DRM_PANEL_SIMPLE`).
  Bring-up image (has tooling); lean Buildroot is the later production target.
- `device-tree-compiler`, `can-utils`, `python3-can` are apt-available on the dev box.

## 1. Image — verify & decompress
```sh
cd ~/cm3-flash
sha512sum -c image.sha512sum          # (edit the filename in it to match if needed)
unxz -k radxa-cm3-bookworm-kde-r2.img.xz     # -> .img  (needs ~10 GB free; 29 G avail)
```

## 2. SPL loader (rkbin)
```sh
cd ~/cm3-flash
wget https://github.com/rockchip-linux/rkbin/raw/master/bin/rk35/rk356x_spl_loader_ddr1056_v1.12.109_no_check_todly.bin
# (any recent rk356x_spl_loader_*.bin works; this is the one the README references)
```

## 3. Flash to eMMC (maskrom)
Put the CM3 in **MASKROM** (CM4IO boot/nRPIBOOT jumper while powering via USB-OTG), then:
```sh
export PATH=~/bin:$PATH
sudo rkdeveloptool ld     # expect: ... Maskrom  Vid=0x2207,Pid=0x350a
sudo rkdeveloptool db rk356x_spl_loader_ddr1056_v1.12.109_no_check_todly.bin
sudo rkdeveloptool wl 0 radxa-cm3-bookworm-kde-r2.img
sudo rkdeveloptool rd     # reboot
```
(Iterate-faster alt: flash an SD, boot it, `dd` the img onto eMMC from the running CM3.)

## 4. First boot — packages & services (ON THE CM3)
The dev box has **no CAN interface**, so `oic` provisioning and CAN sniffing happen
**on the CM3 over SSH**. Get SSH + tooling up first:
```sh
sudo apt update
sudo apt install -y can-utils tcpdump python3-can pipx openssh-server tmux
sudo systemctl enable --now ssh
pipx install openinverter-can-tool          # provides `oic`
```
- **can-utils** = `candump` / `cansend` / `cangen` (candump is the "tcpdump for CAN");
  `tcpdump` proper is there too for the HTTP/BMS side.
- **SSH must be up so you can `oic` remotely.** Order it so it does NOT gate
  time-to-screen: keep the dashboard on the critical path (fbdev), let ssh/network
  come up in parallel/after. In the appliance unit:
  `dashboard.service` → `After=multi-user.target`-ish but owns `/dev/fb0`; ssh/network
  are `WantedBy=multi-user.target` and must not block first-frame render. (See the
  platform-decision note: "network NEVER gates time-to-screen.")

## 4.5 CAN HAT+ back-pad re-strapping (HARDWARE — before stacking)
The 2-CH CAN HAT+ **ships on SPI1** (pins 35/38/40). Re-strap its 7 back solder pads
onto the CM3's SPI3 pins first (0 Ω / solder-blob move per pad). Confirmed 2026-08-11
(silkscreen + Waveshare wiki):

| back pad | set to | = phys pin | as-shipped | action |
|---|---|---|---|---|
| SCLK | SCK | 23 | D21 (40) | move |
| SDI | MOSI | 19 | D20 (38) | move |
| SDO | MISO | 21 | D19 (35) | move |
| CE_0 | CE0 | 24 | D17 (11) | move |
| CE_1 | D16 | 36 | D16 (36) | ✅ leave |
| INT0 | D22 | 15 | D22 (15) | ✅ leave |
| INT_1 | D25 | 22 | D13 (33) | move |

5 pads to move. Do **not** use the CS_1 `CE1`/pin-26 option (pin 26 = CM3 SARADC, dead).
After re-strapping, buzz-verify: SCLK→23, MOSI→19, MISO→21, CS0→24, CS1→36, INT0→15,
INT1→22 — and that 35/38/40 are now open (so DIMMER on pin 40 is clear).

## 5. Enable overlays
Overlays ship `.disabled` in `/boot/dtbo/`; enable via `rsetup` or Radxa overlay config,
then `sudo u-boot-update`. Ours (build the `.dtbo` from the `.dtso` here with
`cpp … | dtc -@`, using the base kernel's `dt-bindings` headers):
- **CAN, bench first:** `overlays/mcp2515-cm3.dtso` (single ch, 12 MHz RS485-CAN HAT).
- **CAN, production:** `overlays/mcp2515-2ch-hatplus.dtso` (can0+can1, 16 MHz 2-CH HAT+).
- **Display:** `overlays/waveshare-12in3-dsi1.dtso` (+ generated `hx8399-init-seq.dtsi`).
  Fill the board plumbing TODOs on hardware (VOP2 `dsi1` route node, DISP1 backlight/
  VCC/touch pins) — lift from the rock-3b-8inch + CM5-7inch-ts-disp1 shipped overlays.

## 6. Verify
```sh
sudo ip link set can0 up type can bitrate 500000    # + can1 for the 2-CH HAT+
candump -tz can0                                     # expect Zombie traffic <100 ms after key-on
oic --help                                           # then import zombie_can_map.json (see reference_oic_provisioning)
```

## First on-hardware unknowns (from README §Open questions)
1. MCP2515 **INT gpio** routing on the CAN board vs our overlay (`GPIO0_C3`/`GPIO3_C6`).
2. VOP2 → `dsi1` **route node** name (`vp0`/`vp1`).
3. CM4IO **DISP1 connector pins** (backlight-PWM / VCC-enable / touch INT+RST).
4. **CAN HAT+ SPI1 continuity buzz-out** — see README / HAT_CONTEXT (pins 35/38/40; DIMMER on 40).
</content>
