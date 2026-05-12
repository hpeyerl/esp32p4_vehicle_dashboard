# scripts/patch_espidf_builder.py
# PlatformIO pre-build shim — called by extra_scripts in platformio.ini.
# Delegates all real work to patch_espidf_core.py so that script
# can also be run standalone with plain python3.

Import("env")  # noqa: F821

import os, subprocess, sys

core_script = os.path.join(
    env.subst("$PROJECT_DIR"), "scripts", "patch_espidf_core.py"
)

result = subprocess.run([sys.executable, core_script], capture_output=False)
if result.returncode != 0:
    print("[patch_espidf] patch_espidf_core.py exited with error")

# ── Clean stale sdkconfig ──────────────────────────────────────────────────────
project_dir    = env.subst("$PROJECT_DIR")
env_name       = env.subst("$PIOENV")
defaults_file  = os.path.join(project_dir, "sdkconfig.defaults")
generated_file = os.path.join(project_dir, f"sdkconfig.{env_name}")

def _mtime(p):
    try:    return os.path.getmtime(p)
    except: return 0

if os.path.exists(generated_file) and _mtime(defaults_file) > _mtime(generated_file):
    print(f"[patch_espidf] sdkconfig.defaults newer — removing {generated_file}")
    os.remove(generated_file)
