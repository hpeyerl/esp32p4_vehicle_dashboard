#!/usr/bin/env python3
# scripts/verify_espidf.py
# Run directly: python3 scripts/verify_espidf.py
#
# Shows the exact current state of every MCU-selection block in
# the installed espidf.py so you can confirm it's sane before
# continuing to debug the compiler selection issue.

import os

pio_home = os.path.expanduser("~/.platformio")

targets = []
for root, dirs, files in os.walk(pio_home):
    for f in files:
        if f == "espidf.py" and "frameworks" in root:
            targets.append(os.path.join(root, f))

targets.sort()

KEYWORDS = [
    "TOOLCHAIN_DIR",
    "toolchain-riscv32-esp",
    "mcu in (",
    "mcu not in (",
    "mcu == ",
    "riscv",
    "linker.lf",
    "esp32p4",
    "esp32c6",
    "esp32c3",
    "BOOTLOADER_OFFSET",
    "bootloader_offset",
]

for path in targets:
    # Only show espressif32@6.13.0 and the unversioned (symlinked) one
    if "6.13.0" not in path and "espressif32/builder" not in path:
        continue

    print(f"\n{'='*72}")
    print(f"FILE: {path}")
    print(f"{'='*72}")

    with open(path) as fh:
        lines = fh.readlines()

    last_printed = -1
    for i, line in enumerate(lines):
        if any(k in line for k in KEYWORDS):
            # Print a context window of 3 lines before and after
            start = max(0, i - 2)
            end   = min(len(lines), i + 3)
            if start > last_printed + 1:
                print(f"  ...")
            for j in range(max(last_printed + 1, start), end):
                marker = ">>>" if j == i else "   "
                print(f"  {marker} {j+1:4d}| {lines[j]}", end="")
            last_printed = end - 1
    print()
