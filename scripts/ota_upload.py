#!/usr/bin/env python3
# =============================================================
#  scripts/ota_upload.py — PlatformIO custom OTA upload script
#
#  Called by PlatformIO when upload_protocol = custom
#  in platformio.ini.
#
#  Usage:
#    pio run -e waveshare -t upload          # uses OTA_HOST from ini
#    OTA_HOST=192.168.1.42 pio run -t upload # override host
#
#  Requires: requests  (pip install requests)
# =============================================================

import sys
import os
Import("env")  # noqa: F821 — PlatformIO SCons environment

def ota_upload(source, target, env):
    import urllib.request
    import urllib.error

    firmware = str(source[0])
    host     = env.GetProjectOption("custom_ota_host",
                 os.environ.get("OTA_HOST", "ev-dashboard.local"))
    port     = env.GetProjectOption("custom_ota_port",
                 os.environ.get("OTA_PORT", "80"))
    url      = f"http://{host}:{port}/update"

    print(f"\n==> OTA upload: {firmware}")
    print(f"==> Target:     {url}")

    size = os.path.getsize(firmware)
    print(f"==> Size:       {size:,} bytes")

    try:
        with open(firmware, "rb") as f:
            data = f.read()

        req = urllib.request.Request(
            url,
            data=data,
            method="POST",
            headers={"Content-Type": "application/octet-stream",
                     "Content-Length": str(size)},
        )

        # Progress reporting via a wrapper that prints dots
        print("==> Uploading ", end="", flush=True)
        chunk = 32768
        sent  = 0

        # urllib doesn't support chunked progress easily — use a file-like wrapper
        class ProgressFile:
            def __init__(self, data):
                self._data = data
                self._pos  = 0
            def read(self, n=-1):
                nonlocal sent
                chunk_data = self._data[self._pos:self._pos + (n if n > 0 else len(self._data))]
                self._pos += len(chunk_data)
                sent      += len(chunk_data)
                pct = int(sent / size * 50)
                bar = "=" * pct + ">" + " " * (50 - pct)
                print(f"\r==> [{bar}] {sent*100//size:3d}%", end="", flush=True)
                return chunk_data

        req2 = urllib.request.Request(
            url,
            data=ProgressFile(data),
            method="POST",
            headers={"Content-Type": "application/octet-stream",
                     "Content-Length": str(size)},
        )
        with urllib.request.urlopen(req2, timeout=120) as resp:
            body = resp.read().decode()
        print(f"\r==> [{'=' * 51}] 100%")
        print(f"==> Server response: {body}")
        print("==> OTA upload successful — device is rebooting")
        print(f"==> Reconnect to http://{host}/ in ~5 seconds")

    except urllib.error.HTTPError as e:
        body = e.read().decode()
        print(f"\n*** OTA upload failed: HTTP {e.code} — {body}", file=sys.stderr)
        env.Exit(1)
    except Exception as e:
        print(f"\n*** OTA upload error: {e}", file=sys.stderr)
        print("    Is the device reachable? Try:", file=sys.stderr)
        print(f"    curl http://{host}/status", file=sys.stderr)
        env.Exit(1)

env.Replace(UPLOADCMD=ota_upload)
