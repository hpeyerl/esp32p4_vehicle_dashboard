# scripts/clean_sdkconfig.py
# PlatformIO pre-build script.
# Deletes the generated sdkconfig.<env> file if sdkconfig.defaults
# has been modified more recently, preventing stale Kconfig state
# from breaking the build after sdkconfig.defaults changes.
#
# Also fires on `pio run --target clean`.

import os
import glob

Import("env")  # noqa: F821 — PlatformIO injects this

project_dir = env.subst("$PROJECT_DIR")
env_name    = env.subst("$PIOENV")

defaults_file  = os.path.join(project_dir, "sdkconfig.defaults")
generated_file = os.path.join(project_dir, f"sdkconfig.{env_name}")

def _mtime(path):
    try:
        return os.path.getmtime(path)
    except OSError:
        return 0

if os.path.exists(generated_file):
    if _mtime(defaults_file) > _mtime(generated_file):
        print(f"[clean_sdkconfig] sdkconfig.defaults is newer — removing {generated_file}")
        os.remove(generated_file)
    else:
        print(f"[clean_sdkconfig] sdkconfig up to date, skipping removal.")
else:
    print(f"[clean_sdkconfig] No existing {generated_file}, nothing to remove.")
