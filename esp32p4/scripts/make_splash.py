#!/usr/bin/env python3
"""
tools/make_splash.py  —  Convert splash PNG to LVGL-compatible formats

Generates two outputs:
  1. evj55_splash.bin   — raw RGB565 LE binary to flash into the 'splash'
                          partition (the recommended approach: 2 MB, loads fast)
  2. evj55_splash_c.h   — optional C array at 1/4 resolution (320x213) for
                          direct embed in firmware without a flash partition.
                          Use this if you don't want to manage a separate partition.

Usage:
    python3 tools/make_splash.py [input.png]
    python3 tools/make_splash.py                  # uses evj55.png by default

Output files written to current directory.

LVGL image format used: CF_TRUE_COLOR (RGB565 little-endian, no alpha)
Width × Height: 1280×800 (full) or 320×213 (thumbnail C array)
"""

import sys
import os
import struct
import numpy as np
from PIL import Image

TARGET_W  = 1280
TARGET_H  = 800
THUMB_W   = 320
THUMB_H   = 213    # maintains ~3:2 aspect with black bars

INPUT_PNG = sys.argv[1] if len(sys.argv) > 1 else "evj55.png"

def to_rgb565_array(canvas_rgb):
    """Convert PIL RGB image to numpy array of uint16 RGB565 LE values."""
    arr = np.array(canvas_rgb, dtype=np.uint16)
    r = (arr[:, :, 0] >> 3).astype(np.uint16)
    g = (arr[:, :, 1] >> 2).astype(np.uint16)
    b = (arr[:, :, 2] >> 3).astype(np.uint16)
    return ((r << 11) | (g << 5) | b).astype(np.uint16)

def fit_to_canvas(img, w, h):
    """Scale img to fit w×h on a black canvas, centered."""
    scale  = min(w / img.width, h / img.height)
    new_w  = int(img.width  * scale)
    new_h  = int(img.height * scale)
    scaled = img.resize((new_w, new_h), Image.LANCZOS)
    canvas = Image.new('RGB', (w, h), (0, 0, 0))
    canvas.paste(scaled, ((w - new_w) // 2, (h - new_h) // 2))
    return canvas

print(f"Loading {INPUT_PNG}...")
img = Image.open(INPUT_PNG).convert('RGB')
print(f"  Source: {img.width}×{img.height}")

# ── Full-resolution binary for flash partition ─────────────────────────────
full_canvas  = fit_to_canvas(img, TARGET_W, TARGET_H)
full_rgb565  = to_rgb565_array(full_canvas)

bin_path = "evj55_splash.bin"
full_rgb565.astype('<u2').tofile(bin_path)
print(f"\nWrote {bin_path}: {os.path.getsize(bin_path)/1024:.1f} KB  "
      f"({TARGET_W}×{TARGET_H} RGB565 LE)")

# ── Thumbnail C array for optional direct embed ────────────────────────────
thumb_canvas = fit_to_canvas(img, THUMB_W, THUMB_H)
thumb_rgb565 = to_rgb565_array(thumb_canvas)

c_path = "evj55_splash_c.h"
flat   = thumb_rgb565.flatten()
n      = len(flat)

with open(c_path, 'w') as f:
    f.write(f"""\
/*
 * evj55_splash_c.h  —  LVGL splash screen image (embedded C array)
 *
 * Resolution : {THUMB_W}×{THUMB_H}  RGB565 LE
 * Size       : {n*2/1024:.1f} KB
 *
 * USAGE (in splash_screen.c):
 *   #include "evj55_splash_c.h"
 *   // then call splash_show() defined below
 *
 * NOTE: This is the {THUMB_W}×{THUMB_H} thumbnail version for direct firmware embed.
 * For full 1280×800, flash evj55_splash.bin to the 'splash' partition instead
 * and use the partition-based loader in splash_screen.c.
 */

#pragma once
#include "lvgl.h"

#define SPLASH_EMBED_W  {THUMB_W}
#define SPLASH_EMBED_H  {THUMB_H}

static const uint16_t evj55_splash_data[{n}] = {{
""")
    # Write 16 values per line
    for i in range(0, n, 16):
        chunk = flat[i:i+16]
        line  = ", ".join(f"0x{v:04X}" for v in chunk)
        f.write(f"    {line},\n")

    f.write(f"""\
}};

/* Use assignment init to avoid LVGL 9 missing-field-initializer warnings */
static lv_image_dsc_t evj55_splash_img;
static inline void evj55_splash_img_init(void) {{
    evj55_splash_img.header.cf     = LV_COLOR_FORMAT_RGB565;
    evj55_splash_img.header.magic  = LV_IMAGE_HEADER_MAGIC;
    evj55_splash_img.header.w      = SPLASH_EMBED_W;
    evj55_splash_img.header.h      = SPLASH_EMBED_H;
    evj55_splash_img.header.stride = SPLASH_EMBED_W * 2;
    evj55_splash_img.data_size     = sizeof(evj55_splash_data);
    evj55_splash_img.data          = (const uint8_t *)evj55_splash_data;
}}
""")

print(f"Wrote {c_path}: {os.path.getsize(c_path)/1024:.1f} KB source  "
      f"({THUMB_W}×{THUMB_H} embedded C array)")

print("""
Next steps
──────────
Option A — Flash partition (recommended, full 1280×800):
  1. Add to partitions_16MB.csv:
       splash, data, spiffs, 0xF20000, 0x200000,
  2. Flash the binary:
       esptool.py --port /dev/ttyUSB0 write_flash 0xF20000 evj55_splash.bin
  3. In firmware, call splash_show_from_partition() from splash_screen.c

Option B — Embedded C array (320×213, no extra partition needed):
  1. Copy evj55_splash_c.h to src/
  2. In main.cpp, call splash_show_embedded() from splash_screen.c
""")
