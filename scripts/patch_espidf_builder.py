# scripts/patch_espidf_builder.py
# PlatformIO pre-build shim — called by extra_scripts in platformio.ini.

Import("env")  # noqa: F821

import os, subprocess, sys

core_script = os.path.join(
    env.subst("$PROJECT_DIR"), "scripts", "patch_espidf_core.py"
)

result = subprocess.run([sys.executable, core_script], capture_output=False)
if result.returncode != 0:
    print("[patch_espidf] patch_espidf_core.py exited with error")

# ── Clean stale sdkconfig ─────────────────────────────────────────────────────
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

# ── Patch _get_board_flash_mode ───────────────────────────────────────────────
try:
    board_flash_mode = env.BoardConfig().get("build.flash_mode", "dio")
    if board_flash_mode in ("qio", "qout"):
        env.Replace(BOARD_FLASH_MODE=board_flash_mode)
        env["__get_board_flash_mode"] = lambda e: board_flash_mode
        print(f"[patch_espidf] flash mode forced to '{board_flash_mode}' (overriding platform DIO downgrade)")
    else:
        print(f"[patch_espidf] flash mode: '{board_flash_mode}' (no patch needed)")
except Exception as e:
    print(f"[patch_espidf] flash mode patch failed: {e}")

# ── Patch esp_wifi_remote dummy_src.c double-registration ────────────────────
# When neither ESP_WIFI_ENABLED nor ESP_HOST_WIFI_ENABLED is set,
# esp_wifi_remote CMakeLists.txt calls set_target_properties() to replace
# esp_wifi's sources with dummy_src.c. This causes pioarduino's Ninja
# generator to see two different compile commands for the same .o file.
# Fix: remove that set_target_properties call from the component file
# before CMake runs.
try:
    wifi_remote_cmake = os.path.join(
        project_dir, "managed_components",
        "espressif__esp_wifi_remote", "CMakeLists.txt"
    )
    if os.path.exists(wifi_remote_cmake):
        with open(wifi_remote_cmake, 'r') as f:
            content = f.read()
        old = '    set_target_properties(${wifi} PROPERTIES SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/dummy_src.c")'
        new = '    # [patched] removed duplicate dummy_src.c registration (pioarduino fix)'
        if old in content:
            content = content.replace(old, new)
            with open(wifi_remote_cmake, 'w') as f:
                f.write(content)
            print("[patch_espidf] patched esp_wifi_remote CMakeLists.txt (dummy_src.c fix)")
        else:
            print("[patch_espidf] esp_wifi_remote already patched or not found")

        # ── Patch wifi_sources duplicate ninja target conflict ─────────────────
        # get_target_property(wifi_sources) includes wifi_default.c, wifi_netif.c,
        # wifi_default_ap.c which are already compiled by esp_wifi. When added again
        # via target_sources(), Ninja sees two compile actions for the same .o file.
        old2 = '    get_target_property(wifi_sources ${wifi} SOURCES)\n    # [patched] removed duplicate dummy_src.c registration (pioarduino fix)'
        new2 = ('    get_target_property(wifi_sources ${wifi} SOURCES)\n'
                '    # [patched] removed duplicate dummy_src.c registration (pioarduino fix)\n'
                '    # [patched] filter files already compiled by esp_wifi to avoid duplicate ninja targets\n'
                '    list(FILTER wifi_sources EXCLUDE REGEX "wifi_default[^/]*\\\\.c$")\n'
                '    list(FILTER wifi_sources EXCLUDE REGEX "wifi_netif\\\\.c$")')
        if old2 in content:
            content = content.replace(old2, new2)
            with open(wifi_remote_cmake, 'w') as f:
                f.write(content)
            print("[patch_espidf] patched esp_wifi_remote CMakeLists.txt (wifi_sources duplicate fix)")
        elif 'list(FILTER wifi_sources' in content:
            print("[patch_espidf] esp_wifi_remote wifi_sources filter already applied")
except Exception as e:
    print(f"[patch_espidf] esp_wifi_remote patch failed: {e}")
