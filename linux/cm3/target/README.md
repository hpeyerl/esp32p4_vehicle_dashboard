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
| `etc/initramfs-tools/scripts/init-bottom/overlayroot` | **read-only root**: RAM (tmpfs) overlay over a ro eMMC root — power-cut safe |
| `etc/initramfs-tools/modules` | pulls the `overlay` module into the initramfs + loads it early |
| `usr/local/sbin/ro-rw` / `ro-ro` | flip the eMMC root writable/read-only to persist a change |

> ⚠️ **Do NOT deploy with `tar -C / -xf -` (the example below).** A `tar` archive
> of `.` includes `./` and re-stamps the owner/mode of `/`, `/etc`, `/usr`, `/home`
> — that broke login once. Prefer per-file `install -o root -g root -m NNN <src> <dst>`,
> or `rsync -a` (which does not restamp existing parents).

## After transfer — the non-file steps (packages, build, systemd, DTB)
These aren't files (apt/pipx/native build + systemd state + DTB stacking), so they
aren't captured above. They're now the **Ansible playbook** in `../ansible/` — run
it on the box after a fresh flash:

```sh
cd ~/evj55-dashboard/linux/cm3/ansible
./RUNME_FIRST.sh     # apt update, install ansible-core, run provision.yml (-c local)
sudo reboot          # apply the stacked DTB (panel + CAN), start the dashboard
```

The playbook enables `ssh` properly (the old `force-ssh.service` hack is retired —
it's disabled + removed by the playbook), installs the package set, `pipx`-installs
`oic`, builds the app (`-j1`), and compiles + stacks `mcp+ws+wake+pcie` onto the
base DTB via `rebuild-dtb.sh`. Idempotent; re-run after any kernel update.

DT overlay **sources** live in `../overlays/` (`mcp2515-2ch-hatplus`, `waveshare-12in3-dsi1`,
`wake-button-gpio0`, `disable-pcie`). They are pre-merged into the fdtdir base DTB with
`fdtoverlay` (U-Boot's runtime `fdtoverlays` apply traps into rescue on this board).
Re-run `rebuild-dtb.sh` after any kernel update (it overwrites the base DTB).

## Read-only root (power-cut safe)

The vehicle yanks +12V unpredictably; an unclean shutdown on a read-write ext4 is
how this eMMC got corrupted (twice). `init-bottom/overlayroot` fixes this at the
source: it stacks a **tmpfs overlay** on top of a **read-only** eMMC root, so all
runtime writes land in RAM and the eMMC is only ever *read* — nothing to corrupt.

The Debian `overlayroot` package does **not** work here (its `trap … EXIT` is
invalid in this BSP's klibc initramfs → boot panic → reflash). Our hook is written
for klibc and, critically, **fails safe**: any error leaves the original read-write
root in place and boots normally (recoverable over ssh) instead of panicking.

* **Activate / after a kernel update:** files are static (baked from this tree), but
  the boot initrd must be regenerated so it contains the hook + `overlay` module:
  ```sh
  sudo update-initramfs -u -k "$(uname -r)"   # then reboot
  ```
  (The ansible playbook does this automatically as its last step.)
* **Verify after boot:** `findmnt / ` shows `overlay`; `findmnt /mnt/ro` shows the
  eMMC mounted `ro`. The live lower is at `/mnt/ro`, the RAM layer at `/mnt/rw`.
* **Persist a change** (survives reboot): `sudo ro-rw` → edit under `/mnt/ro/…` →
  `sudo ro-ro` → `sudo reboot`.
* **Escape hatch:** add `overlayroot=off` to the kernel cmdline to boot a plain
  read-write root for maintenance.
