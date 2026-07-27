#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Herb Peyerl
# SPDX-License-Identifier: BSD-3-Clause
"""
make_pi_splash.py — render the FJ55 boot splash for the Pi 5 / Waveshare DSI panel.

The panel is native 720x1920 PORTRAIT; the dashboard app renders a 1920x720
landscape UI and rotates it 270 deg in software (see linux/README.md). Plymouth
and the console draw straight to the raw framebuffer, so to make the truck appear
UPRIGHT on the landscape-mounted panel we compose landscape, then pre-rotate 270
to match the app's convention.

Output: a 720x1920 PNG sized exactly to /dev/fb0.

Usage:
    python3 make_pi_splash.py [source.png] [out.png]
    # defaults: ~/GitHub/evj55/images/evj55.png -> plymouth/evj55/evj55_splash.png
"""
import os
import sys
from PIL import Image

FB_W, FB_H = 720, 1920        # panel native portrait framebuffer
UI_W, UI_H = 1920, 720        # landscape UI orientation the app rotates 270 into
BG        = (24, 26, 28)      # #18191c dark neutral
FILL      = 0.92             # truck occupies this fraction of the frame

HERE   = os.path.dirname(os.path.abspath(__file__))
SRC    = sys.argv[1] if len(sys.argv) > 1 else \
         os.path.expanduser("~/GitHub/evj55/images/evj55.png")
OUT    = sys.argv[2] if len(sys.argv) > 2 else \
         os.path.join(HERE, "plymouth", "evj55", "evj55_splash.png")


def main():
    truck = Image.open(SRC).convert("RGBA")

    # 1) compose the landscape frame as it should look upright on the panel
    land = Image.new("RGBA", (UI_W, UI_H), BG + (255,))
    scale = min(UI_W / truck.width, UI_H / truck.height) * FILL
    tw, th = int(truck.width * scale), int(truck.height * scale)
    tr = truck.resize((tw, th), Image.LANCZOS)
    land.alpha_composite(tr, ((UI_W - tw) // 2, (UI_H - th) // 2))

    # 2) rotate 270 (CCW) == 90 CW to match the app -> portrait fb geometry
    port = land.rotate(270, expand=True)
    assert port.size == (FB_W, FB_H), f"unexpected size {port.size}"

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    port.convert("RGB").save(OUT)
    print(f"wrote {OUT}  {port.size}  (source: {SRC})")


if __name__ == "__main__":
    main()
