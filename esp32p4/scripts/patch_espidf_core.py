#!/usr/bin/env python3
# scripts/patch_espidf_core.py
#
# Patches installed main.py and espidf.py for espressif32 platform
# to add esp32p4 to the RISC-V MCU whitelists.
#
# Run directly:   python3 scripts/patch_espidf_core.py
# Safe to run multiple times — skips already-patched files.

import os, sys

RISCV_FULL = '"esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32c61", "esp32h2", "esp32p4"'

ESPIDF_REPLACEMENTS = [
    # 1a. TOOLCHAIN_DIR — multi-line form (platform >= 6.8)
    (
        '    "toolchain-riscv32-esp"\n    if mcu in ("esp32c3", "esp32c6")',
        f'    "toolchain-riscv32-esp"\n    if mcu in ({RISCV_FULL})',
    ),
    # 1b. TOOLCHAIN_DIR — single-line form (platform < 6.8)
    (
        '"riscv32-esp" if mcu in ("esp32c3", "esp32c6")',
        f'"riscv32-esp" if mcu in ({RISCV_FULL})',
    ),
    # 2. ULP exclusion
    (
        'if mcu not in ("esp32c3", "esp32c6"):',
        f'if mcu not in ({RISCV_FULL}):',
    ),
    # 3a. riscv linker fragment — multi-line append
    (
        'if mcu in ("esp32c3", "esp32c6"):\n        result.append(\n'
        '            os.path.join(framework_components_dir, "riscv", "linker.lf")\n        )',
        f'if mcu in ({RISCV_FULL}):\n        result.append(\n'
        '            os.path.join(framework_components_dir, "riscv", "linker.lf")\n        )',
    ),
    # 3b. riscv linker fragment — single-line append
    (
        'if mcu in ("esp32c3", "esp32c6"):\n'
        '        result.append(os.path.join(framework_components_dir, "riscv", "linker.lf"))',
        f'if mcu in ({RISCV_FULL}):\n'
        '        result.append(os.path.join(framework_components_dir, "riscv", "linker.lf"))',
    ),
    # 4. bootloader offset
    (
        '"esp32c3", "esp32c6", "esp32s3"',
        '"esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32c61", "esp32h2", "esp32p4", "esp32s3"',
    ),
]

# main.py actual structure in 6.13.0:
#   toolchain_arch = "xtensa-%s" % mcu
#   filesystem = board.get("build.filesystem", "spiffs")   ← extra line
#   if mcu in ("esp32c3", "esp32c6"):
#       toolchain_arch = "riscv32-esp"
# So we match just the `if` line — no context needed, it's unique enough.
MAIN_REPLACEMENTS = [
    # 1. toolchain_arch if-block (matches regardless of what's between)
    (
        'if mcu in ("esp32c3", "esp32c6"):\n    toolchain_arch = "riscv32-esp"',
        f'if mcu in ({RISCV_FULL}):\n    toolchain_arch = "riscv32-esp"',
    ),
    # 2. GDB tool selection
    (
        'if mcu in ("esp32c3", "esp32c6")\n            else "tool-xtensa-esp-elf-gdb"',
        f'if mcu in ({RISCV_FULL})\n            else "tool-xtensa-esp-elf-gdb"',
    ),
]

def patch_file(path, replacements, sentinel='"esp32p4"'):
    with open(path, "r") as fh:
        original = fh.read()
    if sentinel in original:
        print(f"[patch_espidf] Already patched — skipping:\n             {path}")
        return True
    patched = original
    applied = []
    for old, new in replacements:
        if old in patched:
            patched = patched.replace(old, new, 1)
            applied.append(old.split("\n")[0][:60])
    if applied:
        with open(path, "w") as fh:
            fh.write(patched)
        print(f"[patch_espidf] Patched:  {path}")
        for label in applied:
            print(f"[patch_espidf]   OK: {label!r}")
        return True
    else:
        print(f"[patch_espidf] WARNING: no substitutions matched in:\n             {path}")
        return False

pio_home = os.path.expanduser("~/.platformio")
errors   = 0

for root, dirs, files in os.walk(os.path.join(pio_home, "platforms")):
    if "espressif32" not in root:
        continue
    # Only patch espressif32 — skip all other platforms silently
    if "espressif32" not in os.path.basename(os.path.dirname(root)) and \
       "espressif32" not in os.path.basename(root):
        continue
    # Skip old versions that have a different code structure
    base = os.path.basename(root)
    parent = os.path.basename(os.path.dirname(root))
    is_esp32 = "espressif32" in base or "espressif32" in parent
    if not is_esp32:
        continue

    for fname in files:
        path = os.path.join(root, fname)
        if fname == "espidf.py" and "frameworks" in root:
            if not patch_file(path, ESPIDF_REPLACEMENTS):
                errors += 1
        elif fname == "main.py" and "builder" in root and "frameworks" not in root:
            if not patch_file(path, MAIN_REPLACEMENTS):
                errors += 1

sys.exit(errors)
