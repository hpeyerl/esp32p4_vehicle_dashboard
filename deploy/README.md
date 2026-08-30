# EVJ55 dashboard — Pi 5 appliance deploy

Turns a Raspberry Pi OS (Bookworm) Pi 5 from "boots to desktop, then you stop
lightdm and launch the dashboard by hand" into an **appliance**: console boot, a
Plymouth truck splash from early boot, the dashboard auto-started on `/dev/fb0`,
and console blanking disabled.

Everything here is **prep** — it gets applied on the Pi, which was unreachable when
it was written, so it is **untested on the actual unit**. Review before running.

## Contents

| File | Purpose |
|------|---------|
| `make_pi_splash.py` | Regenerate `evj55_splash.png` (720×1920, pre-rotated 270°) from `~/GitHub/evj55/images/evj55.png` |
| `plymouth/evj55/` | Plymouth theme (`.plymouth`, `.script`, `evj55_splash.png`) |
| `systemd/dashboard.service.in` | Service template; `install.sh` fills in the paths |
| `boot/cmdline.additions.txt` | Kernel cmdline tokens (incl. `consoleblank=0`) |
| `boot/config.additions.txt` | `config.txt` additions (`disable_splash`, `auto_initramfs`) |
| `install.sh` | Applies (and `--revert`s) all of the above, idempotently |

## Apply (on the Pi)

```sh
# 1. build the dashboard first (see ../README.md)
cd ~/GitHub/esp32p4_vehicle_dashboard/linux
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4

# 2. apply the appliance conversion
sudo ./deploy/install.sh
sudo reboot
```

Undo everything (restores desktop boot + the stock splash, from the backups it made):

```sh
sudo ./deploy/install.sh --revert && sudo reboot
```

## What it changes

- **Boot target** → `multi-user.target` (console, no X); `lightdm` disabled.
- **dashboard.service** enabled, runs `build/dashboard /dev/fb0` as root, `Restart=always`,
  ordered `After=plymouth-quit-wait.service` so it doesn't fight Plymouth for the fb.
- **Plymouth theme** `evj55` set default; initramfs rebuilt so the truck shows early.
- **cmdline.txt**: `quiet splash … consoleblank=0 loglevel=3` (the `consoleblank=0`
  is the fix for the panel going dark after idle).
- **config.txt**: `disable_splash=1`, `auto_initramfs=1`.
- **Disabled** the wait-online services + ModemManager (boot-time sinks).
  **`avahi` is kept** — that's what serves `pi5.local`.

## Notes / gotchas

- **Splash rotation is a guess.** The PNG is rotated 270° to match the app's
  software rotation. If the truck appears upside-down on the panel, edit
  `make_pi_splash.py` (`land.rotate(270 → 90)`), re-run it, and re-run `install.sh`.
- **Seamless handoff** (no black frame between splash and dashboard) was *not* wired
  up — this is the plain "Plymouth boot splash" option. There may be a brief blank
  at the Plymouth→dashboard transition. If it bugs you, we can hold Plymouth until
  the app's first frame.
- **Power first.** Before trusting any boot-time numbers, check `vcgencmd get_throttled`
  — a Pi 5 on a 5 V/3 A buck can undervolt (see the dark-screen investigation). A
  fast boot into a brownout is not a win.
- **De-privileging** the service: create a user in the `video` + `input` groups and
  set `User=` in `dashboard.service.in` instead of `root`.
