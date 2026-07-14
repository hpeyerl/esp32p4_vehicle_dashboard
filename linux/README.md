# EV Dashboard — Pi5 / Linux port

Runs the LVGL dashboard UI natively on Linux (Raspberry Pi 5 + Waveshare
12.3" DSI panel), reusing the shared UI (`../src/dashboard_ui.cpp`) unchanged.
The ESP-IDF/FreeRTOS platform layer is replaced by a Linux one (fbdev display,
evdev touch, `SIM_DATA` loop). See the top-level TODO to make this the primary
target and drop the shims.

## Why fbdev (not DRM)

LVGL's DRM driver renders `LV_DISPLAY_RENDER_MODE_DIRECT` into the physical
framebuffer, which cannot do software rotation. The panel is native 720×1920
portrait and the UI is 1920×720 landscape, so we need rotation — LVGL's **fbdev**
driver rotates in software in its flush. Hence `/dev/fb0` + `lv_display_set_rotation`.

## Build (native on the Pi)

```sh
sudo apt install -y cmake build-essential libdrm-dev pkg-config
cd linux
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

LVGL is taken from `../managed_components/lvgl__lvgl` if present, else fetched
(v9.5.0). Config is `linux/lv_conf.h` (32-bit color, CLIB malloc, fbdev+evdev,
the montserrat fonts the UI uses).

## Run

The DSI panel is a DRM device the desktop compositor holds. Free it first:

```sh
sudo systemctl stop lightdm          # release the display
./build/dashboard                    # /dev/fb0 + auto-detected touch, rot 270
# ./build/dashboard /dev/fb0 /dev/input/event5   # explicit device args
LV_ROTATE=90 ./build/dashboard       # override rotation (0/90/180/270)
sudo systemctl start lightdm         # restore the desktop when done
```

- **Display:** fbdev `/dev/fb0`, rotated 270° to 1920×720 landscape.
- **Touch:** GT911 auto-detected by scanning `/sys/class/input/*/device/name`.
- **Data:** simulated (`sinf` sweeps) — no CAN needed for the prototype.
