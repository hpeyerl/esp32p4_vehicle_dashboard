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
        # wifi_default_ap.c must stay in wifi_sources (it needs esp_wifi_remote's
        # injected headers) — it's removed from esp_wifi's own SOURCES instead, below.
        marker2 = '# [patched] remove wifi_default_ap.c from esp_wifi'
        old2 = '    get_target_property(wifi_sources ${wifi} SOURCES)\n    # [patched] removed duplicate dummy_src.c registration (pioarduino fix)'
        new2 = ('    get_target_property(wifi_sources ${wifi} SOURCES)\n'
                '    # [patched] removed duplicate dummy_src.c registration (pioarduino fix)\n'
                '    # [patched] filter files that esp_wifi also compiles directly to avoid duplicate ninja targets.\n'
                '    # wifi_default_ap.c is NOT filtered here — it needs injected headers so must be compiled via\n'
                '    # wifi_sources with esp_wifi_remote\'s include path. It is removed from esp_wifi\'s SOURCES below.\n'
                '    list(FILTER wifi_sources EXCLUDE REGEX "/wifi_default\\\\.c$")\n'
                '    list(FILTER wifi_sources EXCLUDE REGEX "/wifi_netif\\\\.c$")\n'
                '    # [patched] remove wifi_default_ap.c from esp_wifi\'s own source list so only esp_wifi_remote\n'
                '    # compiles it (with injected headers). Without this it fails with WiFi header mismatch.\n'
                '    get_target_property(esp_wifi_srcs ${wifi} SOURCES)\n'
                '    list(FILTER esp_wifi_srcs EXCLUDE REGEX "/wifi_default_ap\\\\.c$")\n'
                '    set_target_properties(${wifi} PROPERTIES SOURCES "${esp_wifi_srcs}")')
        if marker2 in content:
            print("[patch_espidf] esp_wifi_remote wifi_sources filter already applied")
        elif old2 in content:
            content = content.replace(old2, new2)
            with open(wifi_remote_cmake, 'w') as f:
                f.write(content)
            print("[patch_espidf] patched esp_wifi_remote CMakeLists.txt (wifi_sources duplicate fix)")
        else:
            print("[patch_espidf] esp_wifi_remote wifi_sources patch point not found — skipping")
except Exception as e:
    print(f"[patch_espidf] esp_wifi_remote patch failed: {e}")

# ── Patch mipi_dsi_hal.c infinite FIFO-full spin-wait ─────────────────────────
# ESP32-P4 MIPI DSI: mipi_dsi_hal_host_gen_write_dcs_command() spins forever
# on `while (mipi_dsi_host_ll_gen_is_cmd_fifo_full(hal->host));` (and the
# analogous write-FIFO checks) with no timeout. Known, unresolved upstream
# bug — see espressif/esp-idf#15137 and #15358 (same hang location). On our
# hardware it hangs on the very first DBI command ever sent to the HX8399
# panel, watchdog-spinning forever. Root cause not fully understood — this
# is a mitigation, not a real fix: bound each spin to ~1s of real wall-clock
# retrying (long enough to let a genuinely transient condition clear) and
# log + proceed rather than hang forever if it truly never clears. See
# CONTEXT.md "DSI hang deep-dive 2026-07-11" for full investigation.
#
# A/B tested 2026-07-11: a boot-time xTaskCreateStaticPinnedToCore assert
# crash appeared reproducibly (5/5 resets) and looked patch-related, but
# testing WITHOUT this patch also crashed 5/5 — root cause was a stale
# generated sdkconfig left over from an ESP-IDF version-upgrade experiment
# earlier the same session, unrelated to this patch. Deleted the stale
# sdkconfig, regenerated clean, crash gone (5/5 clean boots). Patch was
# cleared and confirmed working as designed (stops the hang, one FIFO
# timeout then proceeds) — but the panel still shows nothing regardless,
# so it doesn't fix the actual problem. Disabled again 2026-07-11 so the
# ORIGINAL hang-forever behavior is active for a scope session (CLK/D0/D1
# on the P4-Nano's 15-pin connector, pins 5/6 + 8/9) — hanging forever
# gives unlimited time to get probe placement/triggering right, vs racing
# the patched version's ~1s window. Flip back to True afterward if the
# scope session doesn't change the plan. See CONTEXT.md "DSI hang
# deep-dive" / "Framework patch attempt" / "Cable swap" sections.
_MIPI_DSI_HAL_PATCH_ENABLED = False
if not _MIPI_DSI_HAL_PATCH_ENABLED:
    print("[patch_espidf] mipi_dsi_hal.c patch SKIPPED (disabled for scope session — original hang-forever behavior active, see CONTEXT.md)")
try:
  if _MIPI_DSI_HAL_PATCH_ENABLED:
    dsi_hal = os.path.join(
        os.path.expanduser("~/.platformio"), "packages", "framework-espidf",
        "components", "hal", "mipi_dsi_hal.c"
    )
    if os.path.exists(dsi_hal):
        with open(dsi_hal, 'r') as f:
            content = f.read()
        marker = "// [patched 2026-07-11] bounded fifo wait (vehicle-dashboard, esp-idf#15137/#15358)"
        if marker in content:
            print("[patch_espidf] mipi_dsi_hal.c already patched (bounded FIFO wait)")
        else:
            # 1. Add esp_rom_delay_us availability.
            old_inc = '#include "soc/mipi_dsi_periph.h"'
            new_inc = old_inc + '\n#include "esp_rom_sys.h"  ' + marker
            if old_inc not in content:
                raise RuntimeError("include anchor not found")
            content = content.replace(old_inc, new_inc, 1)

            # 2. Bound the write-FIFO spin (3 identical occurrences).
            old_write = "while (mipi_dsi_host_ll_gen_is_write_fifo_full(hal->host));"
            new_write = (
                "for (int _dsi_wfifo_i = 0; mipi_dsi_host_ll_gen_is_write_fifo_full(hal->host); _dsi_wfifo_i++) "
                "{ if (_dsi_wfifo_i >= 100000) { HAL_LOGW(\"dsi_hal\", \"write FIFO never cleared after ~1s "
                "(patched, see esp-idf#15137)\"); break; } esp_rom_delay_us(10); }"
            )
            write_count = content.count(old_write)
            if write_count == 0:
                raise RuntimeError("write-FIFO spin pattern not found")
            content = content.replace(old_write, new_write)

            # 3. Bound the cmd-FIFO spin (1 occurrence) — this is the one
            #    confirmed hanging on our hardware.
            old_cmd = "while (mipi_dsi_host_ll_gen_is_cmd_fifo_full(hal->host));"
            new_cmd = (
                "for (int _dsi_cfifo_i = 0; mipi_dsi_host_ll_gen_is_cmd_fifo_full(hal->host); _dsi_cfifo_i++) "
                "{ if (_dsi_cfifo_i >= 100000) { HAL_LOGW(\"dsi_hal\", \"cmd FIFO never cleared after ~1s "
                "(patched, see esp-idf#15137) - sending header anyway\"); break; } esp_rom_delay_us(10); }"
            )
            cmd_count = content.count(old_cmd)
            if cmd_count == 0:
                raise RuntimeError("cmd-FIFO spin pattern not found")
            content = content.replace(old_cmd, new_cmd)

            with open(dsi_hal, 'w') as f:
                f.write(content)
            print(f"[patch_espidf] patched mipi_dsi_hal.c — bounded {write_count} write-FIFO "
                  f"+ {cmd_count} cmd-FIFO spin(s) to ~1s max instead of forever")
    else:
        print(f"[patch_espidf] mipi_dsi_hal.c not found at expected path — skipping (not an error, may not be installed yet)")
except Exception as e:
    print(f"[patch_espidf] mipi_dsi_hal.c patch failed: {e}")

# ── Patch mspi_timing_tuning_configs.h: reduce PSRAM delayline tuning repeat count ──
# 2026-07-13: PSRAM 200MHz DQS/delayline tuning genuinely SUCCEEDS every time
# (confirmed via serial log: "tuning success, best phase id is 0" then
# "tuning success, best delayline id is 16") but takes ~11 seconds — the
# delayline sweep tests 31 candidates x MSPI_TIMING_DELAYLINE_TEST_NUMS (100)
# repeats each. Right after tuning completes, boot crashes with
# "assert failed: xTaskCreateStaticPinnedToCore ... xPortcheckValidStackMem"
# during ESP-Hosted's early static task creation, then SW-resets and repeats
# the whole ~11s cycle forever — LOOKS like an infinite tuning hang from the
# console but is actually a crash-reboot loop. User's hypothesis: the long
# tuning duration races against something else's timing (not necessarily a
# hardware WDT — the reset is a software assert, not a WDT reset — but some
# other timing-sensitive init step). Testing whether cutting the repeat
# count (still a REAL sweep, not a hardcoded value — same self-calibrating
# code path, just faster) makes the race disappear. Chosen value (8, down
# from 100) still gives real statistical confidence per candidate while
# cutting tuning time roughly 12x (~11s -> under 1s).
try:
    mspi_cfg = os.path.join(
        os.path.expanduser("~/.platformio"), "packages", "framework-espidf",
        "components", "esp_hw_support", "port", "esp32p4",
        "mspi_timing_tuning_configs.h"
    )
    if os.path.exists(mspi_cfg):
        with open(mspi_cfg, 'r') as f:
            content = f.read()
        marker = "// [patched 2026-07-13] reduced delayline tuning repeat count (vehicle-dashboard)"
        if marker in content:
            print("[patch_espidf] mspi_timing_tuning_configs.h already patched (reduced repeat count)")
        else:
            old = "#define MSPI_TIMING_DELAYLINE_TEST_NUMS     100"
            new = "#define MSPI_TIMING_DELAYLINE_TEST_NUMS     8  " + marker
            if old not in content:
                raise RuntimeError("MSPI_TIMING_DELAYLINE_TEST_NUMS anchor not found")
            content = content.replace(old, new, 1)
            with open(mspi_cfg, 'w') as f:
                f.write(content)
            print("[patch_espidf] patched mspi_timing_tuning_configs.h — delayline tuning repeat count 100 -> 8")
    else:
        print("[patch_espidf] mspi_timing_tuning_configs.h not found at expected path — skipping (not an error, may not be installed yet)")
except Exception as e:
    print(f"[patch_espidf] mspi_timing_tuning_configs.h patch failed: {e}")

# ── Patch esp_memory_utils.c: teach esp_ptr_byte_accessible() about TCM ──────
# 2026-07-13: root cause of the 200MHz-PSRAM boot crash above (NOT the
# tuning duration — reducing MSPI_TIMING_DELAYLINE_TEST_NUMS above didn't
# help). The crash is `assert failed: xTaskCreateStaticPinnedToCore
# freertos_tasks_c_additions.h:299 (xPortcheckValidStackMem(puxStackBuffer))`
# at FreeRTOS scheduler start. Traced (with temporary instrumentation,
# since removed) to: the second core's idle task stack, allocated via a
# generic pvPortMalloc(), lands in TCM (Tightly-Coupled Memory) — only at
# 200MHz, because the different PSRAM init path leaves the heap allocator
# in a different state by that point (not a hard capacity limit, an
# allocation-order difference; latent at 20MHz where a different pool
# gets picked instead). Our config has CONFIG_FREERTOS_TASK_CREATE_
# ALLOW_EXT_MEM=y, so xPortcheckValidStackMem() compiles down to just
# `return esp_ptr_byte_accessible(ptr);` — but THAT function's address-
# range check never included TCM either (confirmed: neither it nor
# esp_ptr_internal() call the existing esp_ptr_in_tcm() helper, despite
# TCM being registered in the heap allocator's own capability table with
# MALLOC_CAP_INTERNAL — components/heap/port/esp32p4/memory_layout.c).
# TCM genuinely is valid, always-accessible on-chip memory; this is a
# real ESP-IDF gap for this chip, not a workaround. Fixing
# esp_ptr_byte_accessible() (the function actually in the active check
# path) — NOT esp_ptr_internal() (tried first, turned out to be dead
# code under our ALLOW_EXT_MEM config, left unpatched to keep this
# minimal). Verified: 5/5 clean boots at 200MHz with this patch, 0/5
# without it.
try:
    mem_utils = os.path.join(
        os.path.expanduser("~/.platformio"), "packages", "framework-espidf",
        "components", "esp_hw_support", "esp_memory_utils.c"
    )
    if os.path.exists(mem_utils):
        with open(mem_utils, 'r') as f:
            content = f.read()
        marker = "// [patched 2026-07-13] esp_ptr_byte_accessible TCM fix (vehicle-dashboard)"
        if marker in content:
            print("[patch_espidf] esp_memory_utils.c already patched (TCM byte-accessible fix)")
        else:
            old = "    r = (ip >= SOC_BYTE_ACCESSIBLE_LOW && ip < SOC_BYTE_ACCESSIBLE_HIGH);"
            new = (old + "\n"
                   "    " + marker + "\n"
                   "#if SOC_MEM_TCM_SUPPORTED\n"
                   "    r |= esp_ptr_in_tcm(p);\n"
                   "#endif")
            if old not in content:
                raise RuntimeError("esp_ptr_byte_accessible anchor not found")
            content = content.replace(old, new, 1)
            with open(mem_utils, 'w') as f:
                f.write(content)
            print("[patch_espidf] patched esp_memory_utils.c — esp_ptr_byte_accessible() now recognizes TCM")
    else:
        print("[patch_espidf] esp_memory_utils.c not found at expected path — skipping (not an error, may not be installed yet)")
except Exception as e:
    print(f"[patch_espidf] esp_memory_utils.c patch failed: {e}")
