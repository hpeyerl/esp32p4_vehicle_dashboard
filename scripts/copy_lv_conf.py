# scripts/copy_lv_conf.py
# Pre-build script: copies lv_conf.h to all locations LVGL searches.
#
# LVGL (LV_CONF_INCLUDE_SIMPLE) searches for "lv_conf.h" in:
#   1. The managed component directory (when built as IDF component)
#   2. Two levels above the lvgl/ folder in libdeps (PlatformIO lib build)
#
# We copy to both so it works regardless of which build path is active.

Import("env")  # noqa: F821
import os, shutil

project_dir  = env.subst("$PROJECT_DIR")
pio_env_name = env.subst("$PIOENV")

src = os.path.join(project_dir, "src", "lv_conf.h")
if not os.path.exists(src):
    print(f"[copy_lv_conf] ERROR: {src} not found")
else:
    destinations = [
        # IDF managed component path
        os.path.join(project_dir, "managed_components", "lvgl__lvgl", "lv_conf.h"),
        # PlatformIO libdeps path (../../lv_conf.h relative to lvgl/src/)
        os.path.join(project_dir, ".pio", "libdeps", pio_env_name, "lv_conf.h"),
    ]
    for dst in destinations:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        print(f"[copy_lv_conf] Copied lv_conf.h -> {dst}")
