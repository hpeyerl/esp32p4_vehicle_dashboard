# CM3 post-flash provisioning (Ansible)

Two-part provisioning for the CM3 dashboard:

- **Static files → baked into the image offline** with losetup (see `../target/`).
- **Everything that needs the *running* box → this playbook** — the stuff you
  can't do to a loop-mounted image: `apt` packages, `pipx`, the native ARM app
  build, and stacking the DT overlays against the running kernel's headers, plus
  the systemd enablement the file overlay can't carry.

## Use

After flashing the image and first boot, on the box:

```sh
cd ~/evj55-dashboard/linux/cm3/ansible
./RUNME_FIRST.sh
sudo reboot
```

`RUNME_FIRST.sh` runs `apt-get update` (a fresh image has an empty cache),
installs `ansible-core`, then runs `provision.yml` **locally** (`-i 'localhost,'
-c local` — no control node, no ssh). The playbook is idempotent: safe to re-run
after a kernel update (re-stacks the DTB) or to heal drift.

## What the playbook does

| Step | Why it can't be baked offline |
|------|-------------------------------|
| `apt install` the package set | needs network + the ARM runtime |
| `pipx install openinverter-can-tool` (oic) | network + runtime |
| build the LVGL dashboard (`cmake`, **`-j1`** for the LVGL parallel race) | ARM native compile |
| compile + stack DT overlays onto the base DTB (`rebuild-dtb.sh`) | needs the running kernel's dtb/headers |
| enable `ssh` (retires the old broken `force-ssh` hack), `dashboard`, `ws-panel-power`, `ws-backlight`, `can-up@can0/1`; boot trims; hostname | systemd state, not files |

Display + CAN units are only *enabled* (not started) by the playbook — they come
up on the reboot, once the stacked DTB brings up the panel and `can0`/`can1`.
Package list and exact tasks: see `provision.yml`.
