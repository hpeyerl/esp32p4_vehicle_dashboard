#!/bin/bash
# =============================================================
#  gen_font.sh — Generate a custom Montserrat font for LVGL 9
#
#  Usage:
#    ./scripts/gen_font.sh 72
#    ./scripts/gen_font.sh 110
#
#  Prerequisites:
#    npm install -g lv_font_conv
#    sudo apt install fonts-montserrat   (or locate font manually)
#
#  What this script does:
#    1. Runs lv_font_conv with correct flags for LVGL 9
#    2. Fixes the include path (lvgl/lvgl.h → lvgl.h)
#    3. Sets bitmap_format=0 (PLAIN — lv_font_conv wrongly sets 1)
#    4. Adds .release_glyph = NULL (missing from lv_font_conv output)
#
#  The --range covers all characters needed by the dashboard:
#    0x20-0x2B  space through +
#    0x30-0x39  0-9
#    0x2D       - (minus)
#    0x6B       k
#    0x4B       K
#    0x57       W
#    0x68       h
#    0x43       C  (for temperatures if used at this size)
#    0x56       V  (for voltages)
#    0x41       A  (for amps)
#
#  To use in dashboard_ui.cpp:
#    LV_FONT_DECLARE(lv_font_montserrat_<SIZE>);
#    ... &lv_font_montserrat_<SIZE> ...
#
#  Also add to include/lv_conf.h:
#    #define LV_FONT_MONTSERRAT_<SIZE>  1
# =============================================================

set -e

SIZE="${1:-72}"
FONT="/usr/share/fonts/truetype/montserrat/Montserrat-VariableFont_wght.ttf"
OUT="src/lv_font_montserrat_${SIZE}.c"

if [ ! -f "$FONT" ]; then
    echo "Font not found at $FONT"
    echo "Try: sudo apt install fonts-montserrat"
    echo "Or set FONT variable to the correct path"
    exit 1
fi

if ! command -v lv_font_conv &>/dev/null; then
    echo "lv_font_conv not found. Install with: npm install -g lv_font_conv"
    exit 1
fi

echo "Generating ${SIZE}pt Montserrat → ${OUT}"

lv_font_conv \
  --no-compress \
  --no-prefilter \
  --bpp 4 \
  --size "$SIZE" \
  --font "$FONT" \
  --range 0x20-0x2B,0x2D,0x30-0x39,0x41,0x43,0x56,0x57,0x68,0x6B \
  --format lvgl \
  -o "$OUT"

echo "Applying LVGL 9 fixes..."

# Fix 1: Replace conditional include with simple lvgl.h
python3 - "$OUT" << 'PYEOF'
import sys
with open(sys.argv[1], 'r') as f:
    c = f.read()
c = c.replace(
    '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif',
    '#include "lvgl.h"'
)
with open(sys.argv[1], 'w') as f:
    f.write(c)
print("  [1/3] Fixed include path")
PYEOF

# Fix 2: bitmap_format=1 → bitmap_format=0 (PLAIN, uncompressed)
sed -i 's/\.bitmap_format = 1,/.bitmap_format = 0,/' "$OUT"
echo "  [2/3] Fixed bitmap_format (1→0, PLAIN)"

# Fix 3: Add .release_glyph = NULL after .get_glyph_bitmap
# This field exists in LVGL 9 lv_font_t but lv_font_conv doesn't emit it,
# causing garbage rendering as the function pointer gets random memory.
sed -i 's/    \.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,/    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n    .release_glyph = NULL,/' "$OUT"
echo "  [3/3] Added .release_glyph = NULL"

# Verify
BITMAP_FORMAT=$(grep "bitmap_format" "$OUT" | head -1)
RELEASE_GLYPH=$(grep "release_glyph" "$OUT" | head -1)
INCLUDE=$(grep "^#include" "$OUT" | head -1)

echo ""
echo "Verification:"
echo "  include:        $INCLUDE"
echo "  bitmap_format:  $BITMAP_FORMAT"
echo "  release_glyph:  $RELEASE_GLYPH"
echo ""
echo "Done: $OUT"
echo ""
echo "Next steps:"
echo "  1. Add to include/lv_conf.h:  #define LV_FONT_MONTSERRAT_${SIZE}  1"
echo "  2. In dashboard_ui.cpp:        LV_FONT_DECLARE(lv_font_montserrat_${SIZE});"
echo "  3. Use as:                     &lv_font_montserrat_${SIZE}"
echo "  4. pio run (clean build recommended: rm -rf .pio/build)"
