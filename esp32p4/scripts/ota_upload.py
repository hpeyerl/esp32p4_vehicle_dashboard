#!/usr/bin/env python3
# =============================================================
#  scripts/ota_upload.py ? PlatformIO custom OTA upload script
#  Sets UPLOADCMD to a curl shell command string.
# =============================================================

Import("env")  # noqa: F821

host = env.GetProjectOption("custom_ota_host", "ev-dashboard.local")
port = env.GetProjectOption("custom_ota_port", "80")
url  = f"http://{host}:{port}/update"

env.Replace(
    UPLOADCMD=(
        f'curl -X POST {url}'
        f' -H "Content-Type: application/octet-stream"'
        f' -H "Expect:"'
        f' --data-binary @$SOURCE'
        f' --progress-bar'
        f' --max-time 120'
        f' --connect-timeout 10'
    )
)
Import("projenv")
projenv.AlwaysBuild(projenv.Alias("upload"))
