# CM3 target filesystem overlay

Files here mirror the real paths on the CM3 (`ev-dashboard`, Debian bookworm BSP).
The tree is rooted at what becomes `/`, so it transfers with a single rsync/tar:

```sh
# from repo root, onto the target root:
rsync -a --rsync-path="sudo rsync" linux/cm3/target/ ev-dashboard.local:/
# or:  tar -C linux/cm3/target -cf - . | ssh ev-dashboard.local 'sudo tar -C / -xf -'
```

## What's here
| Path | Purpose |
|------|---------|
| `usr/local/sbin/ws-panel-power.sh` | cold-boot panel MCU@0x45 power + DSI re-init (backlight left OFF) |
| `usr/local/sbin/ws-backlight-on.sh` | backlight ON once the dashboard renders (black→dashboard) |
| `etc/systemd/system/ws-backlight.service` | runs ws-backlight-on after dashboard.service |
| `etc/systemd/system/dashboard.service` | the LVGL dashboard on `/dev/fb0` |
| `etc/systemd/system/can-up@.service` | bring up canN @500k (enable can-up@can0, @can1) |
| `etc/systemd/system/force-ssh.service` | keep SSH on (Radxa rsetup disables it) |
| `etc/NetworkManager/conf.d/99-unmanage-can.conf` | NM ignores can* |
| `lib/systemd/system-sleep/brcmfmac-str` | STR: rmmod/modprobe wifi (SDIO resume bug) |
| `lib/systemd/system-sleep/panel-backlight-str` | STR: backlight off asleep, relight on resume |
| `home/hpeyerl/cm3-dtbo/rebuild-dtb.sh` | stack the DT overlays into the base DTB |

## After transfer — the non-file steps (systemctl state + DTB build)
These aren't files, so they aren't captured above (future Ansible territory —
see the `project_cm3_provisioning` memory):

```sh
# services
sudo systemctl daemon-reload
sudo systemctl enable --now dashboard.service ws-backlight.service \
     can-up@can0.service can-up@can1.service
# boot-time trims
sudo systemctl mask systemd-boot-update.service
sudo systemctl disable NetworkManager-wait-online.service
# device tree: compile ../overlays/*.dtso -> ~/cm3-dtbo/*.dtbo, then:
~/cm3-dtbo/rebuild-dtb.sh        # stacks mcp+ws+wake+pcie onto the base DTB; reboot to apply
```

DT overlay **sources** live in `../overlays/` (`mcp2515-2ch-hatplus`, `waveshare-12in3-dsi1`,
`wake-button-gpio0`, `disable-pcie`). They are pre-merged into the fdtdir base DTB with
`fdtoverlay` (U-Boot's runtime `fdtoverlays` apply traps into rescue on this board).
Re-run `rebuild-dtb.sh` after any kernel update (it overwrites the base DTB).
