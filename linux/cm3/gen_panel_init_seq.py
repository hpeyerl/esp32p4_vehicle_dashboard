#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Herb Peyerl
# SPDX-License-Identifier: BSD-3-Clause
"""
gen_panel_init_seq.py — translate our proven HX8399 init array into a Rockchip
`panel-init-sequence` device-tree byte blob (for a `simple-panel-dsi` node).

Source of truth: src/waveshare_display.c :: s_hx8399_init_cmds[], which was itself
lifted verbatim from the working Raspberry Pi driver
(raspberrypi/linux drivers/gpu/drm/panel/panel-waveshare-dsi-v2.c ::
ws_panel_12_3_a_4lane_init). We parse the C entries and emit DT bytes so there is
NO error-prone hand transcription — regenerate any time the C init changes.

Rockchip DT command format, per command:
    <mipi_type> <delay_ms> <payload_len> <cmd> [params...]
  mipi_type = 0x05 (DCS short write, 0 params)
            = 0x15 (DCS short write, 1 param)
            = 0x39 (DCS long write,  >=2 params)
  payload_len = 1 (cmd) + n_params

Only the core init (through SLPOUT 0x11 / DISPON 0x29) is emitted; the P4-only
2-lane SETMIPI block (#ifdef SETMIPI_2LANE) is intentionally skipped — the CM3
drives the panel at its native 4 lanes on the CM4IO DISP1 connector.

Usage:  python3 gen_panel_init_seq.py [path/to/waveshare_display.c]
"""
import os
import re
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else \
    os.path.join(os.path.dirname(__file__), "..", "..", "src", "waveshare_display.c")

ENTRY = re.compile(
    r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*"
    r"(?:NULL|\(uint8_t\[\]\)\s*\{([^}]*)\})\s*,\s*"
    r"(\d+)\s*,\s*(\d+)\s*\}"
)


def main():
    text = open(SRC).read()
    start = text.index("s_hx8399_init_cmds[] = {")
    body = text[start:]

    rows = []
    for line in body.splitlines():
        if "#ifdef SETMIPI" in line:      # stop before the P4-only 2-lane block
            break
        m = ENTRY.search(line)
        if not m:
            continue
        cmd = int(m.group(1), 16)
        params = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(2) or "")]
        delay = int(m.group(4))
        n = len(params)
        mtype = 0x05 if n == 0 else (0x15 if n == 1 else 0x39)
        payload = [cmd] + params
        rows.append(([mtype, delay, len(payload)] + payload, cmd, delay))
        if cmd == 0x29:                   # DISPON = last command we want
            break

    print(f"/* generated from {os.path.relpath(SRC)} :: s_hx8399_init_cmds */")
    print("panel-init-sequence = [")
    for entry, cmd, delay in rows:
        hexs = " ".join(f"{b:02x}" for b in entry)
        note = {0x01: "SWRESET", 0x11: "SLPOUT", 0x29: "DISPON"}.get(cmd, f"MFG 0x{cmd:02x}")
        d = f" +{delay}ms" if delay else ""
        print(f"\t{hexs}\t/* {note}{d} */")
    print("];")
    print(f"\n/* {len(rows)} commands */", file=sys.stderr)


if __name__ == "__main__":
    main()
