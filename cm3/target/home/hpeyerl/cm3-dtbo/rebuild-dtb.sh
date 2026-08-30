#!/bin/sh
# Re-stack CAN + DSI + wake overlays onto the pristine base DTB and install.
# Run after any kernel/dtb update (which overwrites the fdtdir base and drops overlays).
set -e
BASE=/usr/lib/linux-image-$(uname -r)/rockchip/rk3566-radxa-cm3-rpi-cm4-io.dtb
D=~/cm3-dtbo
[ -f "$BASE.orig" ] || sudo cp "$BASE" "$BASE.orig"   # first run: snapshot pristine
fdtoverlay -i "$BASE.orig" -o /tmp/_stacked.dtb "$D/mcp.dtbo" "$D/ws.dtbo" "$D/wake.dtbo" "$D/pcie.dtbo"
# sanity: all three present before install
n_can=$(dtc -I dtb -O dts /tmp/_stacked.dtb 2>/dev/null | grep -c microchip,mcp2515)
n_panel=$(dtc -I dtb -O dts /tmp/_stacked.dtb 2>/dev/null | grep -c simple-panel-dsi)
n_wake=$(dtc -I dtb -O dts /tmp/_stacked.dtb 2>/dev/null | grep -c wakeup-source)
echo "merged: can=$n_can panel=$n_panel wake=$n_wake"
[ "$n_can" -ge 2 ] && [ "$n_panel" -ge 1 ] && [ "$n_wake" -ge 1 ] || { echo "MERGE INCOMPLETE - not installing"; exit 1; }
sudo cp /tmp/_stacked.dtb "$BASE"
echo "installed stacked DTB -> $BASE"
