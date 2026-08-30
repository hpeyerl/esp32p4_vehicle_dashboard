# scripts/remove_arm_asm.py
# Pre-build script: removes ARM-specific ASM source files from LVGL
# libdeps so PlatformIO's file scanner doesn't try to assemble them
# on a RISC-V target.
#
# These directories contain Helium/NEON assembly that only works on
# ARM Cortex-M with MVE or ARM NEON — not on RISC-V (ESP32-P4).
# The lv_conf.h #if guards prevent the C code from *calling* these
# functions, but PlatformIO assembles every .S file it finds regardless.

Import("env")  # noqa: F821
import os, shutil

project_dir  = env.subst("$PROJECT_DIR")
env_name     = env.subst("$PIOENV")
lvgl_dir     = os.path.join(project_dir, ".pio", "libdeps", env_name, "lvgl")

ARM_ASM_DIRS = [
    os.path.join(lvgl_dir, "src", "draw", "sw", "blend", "helium"),
    os.path.join(lvgl_dir, "src", "draw", "sw", "blend", "neon"),
    os.path.join(lvgl_dir, "src", "draw", "convert", "helium"),
]

for d in ARM_ASM_DIRS:
    if os.path.isdir(d):
        shutil.rmtree(d)
        print(f"[remove_arm_asm] Removed: {d}")
    else:
        print(f"[remove_arm_asm] Already absent: {d}")
