#!/bin/sh
# Best-effort Wi-Fi self-heal for the flaky brcmfmac SDIO wifi on the headless,
# in-dash CM3 (no serial/ethernet reachable once installed). Runs periodically via
# wifi-watchdog.timer. If wlan0 is missing or not connected: unblock rfkill, reload
# the driver if the interface vanished, and (re)bring up the NM profile.
PROFILE="${WIFI_PROFILE:-DebtRidge}"

# Already connected? nothing to do.
if nmcli -t -f DEVICE,STATE device status 2>/dev/null | grep -q "^wlan0:connected"; then
    exit 0
fi

logger -t wifi-watchdog "wlan0 not connected — attempting recovery"
# NM silently ignores the PSK if the profile is group/world-readable (secrets leak
# guard). An unclean power-off can mangle the mode -> no secret -> no association.
# Re-assert 0600 root:root before trying to bring the connection up.
chmod 600 /etc/NetworkManager/system-connections/* 2>/dev/null
chown root:root /etc/NetworkManager/system-connections/* 2>/dev/null
rfkill unblock wifi 2>/dev/null
nmcli radio wifi on 2>/dev/null

# Interface gone entirely = brcmfmac failed to init (intermittent SDIO). Reload it.
if ! ip link show wlan0 >/dev/null 2>&1; then
    logger -t wifi-watchdog "wlan0 absent — reloading brcmfmac"
    modprobe -r brcmfmac 2>/dev/null
    sleep 1
    modprobe brcmfmac 2>/dev/null
    sleep 3
fi

nmcli connection up "$PROFILE" 2>/dev/null || nmcli device connect wlan0 2>/dev/null
exit 0
