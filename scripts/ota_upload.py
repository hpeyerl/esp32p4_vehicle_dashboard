#!/usr/bin/env python3
# =============================================================
#  scripts/ota_upload.py — PlatformIO custom OTA upload script
#
#  Called by PlatformIO when upload_protocol = custom.
#  Used by non-_usb environments for WiFi OTA flashing.
#
#  Usage:
#    pio run -e waveshare -t upload          # uses OTA_HOST from ini
#    OTA_HOST=192.168.1.42 pio run -t upload # override host
# =============================================================

import sys, os
Import("env")  # noqa: F821

def ota_upload(source, target, env):
    import urllib.request, urllib.error

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

        class ProgressFile:
            def __init__(self, data):
                self._data = data
                self._pos  = 0
            def read(self, n=-1):
                chunk = self._data[self._pos:self._pos + (n if n > 0 else len(self._data))]
                self._pos += len(chunk)
                sent = self._pos
                pct = int(sent / size * 50)
                bar = "=" * pct + ">" + " " * (50 - pct)
                print(f"\r==> [{bar}] {sent*100//size:3d}%", end="", flush=True)
                return chunk

        req = urllib.request.Request(
            url, data=ProgressFile(data), method="POST",
            headers={"Content-Type": "application/octet-stream",
                     "Content-Length": str(size)},
        )
        with urllib.request.urlopen(req, timeout=120) as resp:
            body = resp.read().decode()
        print(f"\r==> [{'=' * 51}] 100%")
        print(f"==> Server response: {body}")
        print("==> OTA upload successful — device is rebooting")
        print(f"==> Reconnect to http://{host}/ in ~5 seconds")

    except urllib.error.HTTPError as e:
        print(f"\n*** OTA upload failed: HTTP {e.code} — {e.read().decode()}", file=sys.stderr)
        env.Exit(1)
    except Exception as e:
        print(f"\n*** OTA upload error: {e}", file=sys.stderr)
        print(f"    curl http://{host}/status", file=sys.stderr)
        env.Exit(1)

env.Replace(UPLOADCMD=ota_upload)
