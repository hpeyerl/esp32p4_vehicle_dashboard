#!/usr/bin/env python3
# =============================================================
#  scripts/ota_upload.py — PlatformIO custom OTA upload script
#  Uses curl for reliable streaming upload.
# =============================================================

import sys, os, subprocess
Import("env")  # noqa: F821

def ota_upload(source, target, env):
    firmware = str(source[0])
    host     = env.GetProjectOption("custom_ota_host",
                 os.environ.get("OTA_HOST", "ev-dashboard.local"))
    port     = env.GetProjectOption("custom_ota_port",
                 os.environ.get("OTA_PORT", "80"))
    url      = f"http://{host}:{port}/update"

    print(f"\n==> OTA upload: {firmware}")
    print(f"==> Target:     {url}")
    print(f"==> Size:       {os.path.getsize(firmware):,} bytes\n")

    result = subprocess.run([
        "curl", "-X", "POST", url,
        "-H", "Content-Type: application/octet-stream",
        "--data-binary", f"@{firmware}",
        "--progress-bar",
        "--max-time", "120",
    ])

    if result.returncode != 0:
        print(f"\n*** OTA upload failed (curl exit {result.returncode})", file=sys.stderr)
        env.Exit(1)
    else:
        print(f"\n==> OTA upload complete — device is rebooting")
        print(f"==> Reconnect to http://{host}/ in ~5 seconds")

env.Replace(UPLOADCMD=ota_upload)
