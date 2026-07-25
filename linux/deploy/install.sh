#!/usr/bin/env bash
#
# install.sh — convert a Raspberry Pi OS (Bookworm) Pi 5 into an EVJ55 dashboard
#              appliance: console boot (no desktop), Plymouth truck splash, the
#              dashboard auto-started on /dev/fb0, and console blanking disabled.
#
# Run ON THE PI, from the repo, as root:
#     sudo linux/deploy/install.sh          # apply
#     sudo linux/deploy/install.sh --revert # undo everything
#
# It is idempotent (safe to re-run) and backs up cmdline.txt/config.txt before
# touching them. UNTESTED on this specific Pi — review, then run. Reboot to apply.
#
set -euo pipefail

THEME=evj55
DEPLOY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$DEPLOY_DIR/../.." && pwd)"
LINUX_DIR="$REPO_ROOT/linux"
DASH_BIN="$LINUX_DIR/build/dashboard"

THEME_SRC="$DEPLOY_DIR/plymouth/$THEME"
THEME_DST="/usr/share/plymouth/themes/$THEME"
SVC_SRC="$DEPLOY_DIR/systemd/dashboard.service.in"
SVC_DST="/etc/systemd/system/dashboard.service"

# Bookworm keeps boot config in /boot/firmware; older images in /boot.
if   [ -f /boot/firmware/cmdline.txt ]; then BOOT=/boot/firmware
elif [ -f /boot/cmdline.txt ];          then BOOT=/boot
else echo "!! cannot find cmdline.txt in /boot/firmware or /boot" >&2; exit 1
fi
CMDLINE="$BOOT/cmdline.txt"
CONFIG="$BOOT/config.txt"

CMDLINE_TOKENS="quiet splash plymouth.ignore-serial-consoles logo.nologo vt.global_cursor_default=0 consoleblank=0 loglevel=3"
CONFIG_LINES=("disable_splash=1" "auto_initramfs=1")
DISABLE_SERVICES=("NetworkManager-wait-online.service" "systemd-networkd-wait-online.service" "ModemManager.service")

need_root() { [ "$(id -u)" -eq 0 ] || { echo "run as root (sudo)"; exit 1; }; }

# ---------------------------------------------------------------------------
apply() {
  need_root
  echo "== EVJ55 appliance install =="
  echo "repo:      $REPO_ROOT"
  echo "boot dir:  $BOOT"

  [ -x "$DASH_BIN" ] || { echo "!! dashboard binary not found/executable: $DASH_BIN"; echo "   build it first (see linux/README.md), then re-run."; exit 1; }

  # 1) Plymouth theme -------------------------------------------------------
  echo "-- installing Plymouth theme '$THEME'"
  install -d "$THEME_DST"
  install -m644 "$THEME_SRC/$THEME.plymouth" "$THEME_DST/"
  install -m644 "$THEME_SRC/$THEME.script"   "$THEME_DST/"
  install -m644 "$THEME_SRC/evj55_splash.png" "$THEME_DST/"
  plymouth-set-default-theme "$THEME"

  # 2) systemd service (substitute paths) -----------------------------------
  echo "-- installing dashboard.service"
  sed -e "s#@DASHBOARD_BIN@#$DASH_BIN#g" \
      -e "s#@WORKDIR@#$LINUX_DIR#g" \
      "$SVC_SRC" > "$SVC_DST"
  systemctl daemon-reload
  systemctl enable dashboard.service

  # 3) appliance boot target: console, not desktop --------------------------
  echo "-- boot to console (multi-user.target), disabling display manager"
  systemctl set-default multi-user.target
  systemctl disable lightdm.service 2>/dev/null || true

  # 4) trim slow/unneeded boot services (avahi is KEPT for pi5.local) --------
  for s in "${DISABLE_SERVICES[@]}"; do
    systemctl disable "$s" 2>/dev/null && echo "   disabled $s" || true
  done

  # 5) cmdline.txt: append missing tokens (single line) ---------------------
  echo "-- editing $CMDLINE"
  [ -f "$CMDLINE.evj55.bak" ] || cp -a "$CMDLINE" "$CMDLINE.evj55.bak"
  line="$(cat "$CMDLINE")"
  for tok in $CMDLINE_TOKENS; do
    key="${tok%%=*}"
    # replace an existing key=val, else append the token
    if grep -qw -- "$key" <<<"$line" && [[ "$tok" == *=* ]]; then
      line="$(sed -E "s#(^| )$key=[^ ]*#\1$tok#" <<<"$line")"
    elif ! grep -qw -- "$tok" <<<"$line"; then
      line="$line $tok"
    fi
  done
  echo "$line" | tr -s ' ' > "$CMDLINE"

  # 6) config.txt: append missing lines -------------------------------------
  echo "-- editing $CONFIG"
  [ -f "$CONFIG.evj55.bak" ] || cp -a "$CONFIG" "$CONFIG.evj55.bak"
  grep -q "^# EVJ55 appliance" "$CONFIG" || echo -e "\n# EVJ55 appliance" >> "$CONFIG"
  for l in "${CONFIG_LINES[@]}"; do
    key="${l%%=*}"
    if grep -qE "^\s*$key=" "$CONFIG"; then
      sed -i -E "s#^\s*$key=.*#$l#" "$CONFIG"
    else
      echo "$l" >> "$CONFIG"
    fi
  done

  # 7) rebuild initramfs so the splash shows EARLY --------------------------
  echo "-- rebuilding initramfs (early splash)"
  plymouth-set-default-theme -R "$THEME" 2>/dev/null || update-initramfs -u 2>/dev/null || \
    echo "   (could not rebuild initramfs automatically — splash may appear late)"

  echo
  echo "== done. Reboot to apply.  Revert with: sudo $0 --revert =="
  echo "   Backups: $CMDLINE.evj55.bak  $CONFIG.evj55.bak"
}

# ---------------------------------------------------------------------------
revert() {
  need_root
  echo "== EVJ55 appliance REVERT =="
  systemctl disable dashboard.service 2>/dev/null || true
  rm -f "$SVC_DST"; systemctl daemon-reload
  systemctl set-default graphical.target
  systemctl enable lightdm.service 2>/dev/null || true
  for s in "${DISABLE_SERVICES[@]}"; do systemctl enable "$s" 2>/dev/null || true; done
  plymouth-set-default-theme -R spinner 2>/dev/null || plymouth-set-default-theme spinner 2>/dev/null || true
  rm -rf "$THEME_DST"
  [ -f "$CMDLINE.evj55.bak" ] && mv "$CMDLINE.evj55.bak" "$CMDLINE" && echo "restored $CMDLINE"
  [ -f "$CONFIG.evj55.bak" ]  && mv "$CONFIG.evj55.bak"  "$CONFIG"  && echo "restored $CONFIG"
  echo "== reverted. Reboot to apply. =="
}

case "${1:-}" in
  --revert) revert ;;
  ""|--apply) apply ;;
  *) echo "usage: $0 [--apply|--revert]"; exit 1 ;;
esac
