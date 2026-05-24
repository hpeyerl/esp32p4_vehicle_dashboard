# scripts/copy_lv_conf.py
# Pre-build script: copies lv_conf.h to the location LVGL expects
# when built as an IDF managed component.
#
# When LV_CONF_INCLUDE_SIMPLE is set, LVGL's lv_conf_internal.h does:
#   #include "lv_conf.h"
# The managed component's own directory is on its include path, so
# placing lv_conf.h at managed_components/lvgl__lvgl/lv_conf.h
# is sufficient.
#
# The old destination (.pio/libdeps/.../lv_conf.h) was for LVGL as a
# PlatformIO lib dependency — not applicable when using idf_component.yml.

Import("env")  # noqa: F821
import os, shutil

project_dir = env.subst("$PROJECT_DIR")

src = os.path.join(project_dir, "src", "lv_conf.h")
dst = os.path.join(project_dir, "managed_components", "lvgl__lvgl", "lv_conf.h")

if not os.path.exists(src):
    print(f"[copy_lv_conf] ERROR: {src} not found")
else:
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    print(f"[copy_lv_conf] Copied lv_conf.h -> {dst}")
