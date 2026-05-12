# scripts/copy_lv_conf.py
# Pre-build script: copies lv_conf.h from src/ to the location
# LVGL expects when LV_CONF_INCLUDE_SIMPLE is NOT sufficient —
# i.e. two directories above lvgl/src/, which resolves to
# .pio/libdeps/<env>/lv_conf.h
#
# LVGL's lv_conf_internal.h tries these paths in order:
#   1. "lv_conf.h"          — on the include path (LV_CONF_INCLUDE_SIMPLE)
#   2. "../../lv_conf.h"    — next to the lvgl/ folder in libdeps
# We satisfy both.

Import("env")  # noqa: F821
import os, shutil

project_dir  = env.subst("$PROJECT_DIR")
pio_env_name = env.subst("$PIOENV")

src  = os.path.join(project_dir, "src", "lv_conf.h")
dst  = os.path.join(project_dir, ".pio", "libdeps", pio_env_name, "lv_conf.h")

if not os.path.exists(src):
    print(f"[copy_lv_conf] ERROR: {src} not found")
else:
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    print(f"[copy_lv_conf] Copied lv_conf.h → {dst}")
