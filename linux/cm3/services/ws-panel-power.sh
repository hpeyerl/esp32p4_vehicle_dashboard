#!/bin/sh
# Waveshare 12.3" panel MCU@0x45 power-on (rails + DSI re-init) — BACKLIGHT LEFT OFF.
# The backlight is turned on later by ws-backlight.service, once the dashboard app is
# actually rendering, so cold boot shows black -> dashboard (no boot-console flash).
# MCU regs: 0x94/0x95 = GPIO output hi/lo byte (AVDD=b0, LCD_RESET=b1, BL_EN=b2, IOVCC=b4).
B=0; A=0x45
i2cset -y $B $A 0x94 0x03; i2cset -y $B $A 0x95 0x00; sleep 0.3
i2cset -y $B $A 0x95 0x10; sleep 0.02      # IOVCC
i2cset -y $B $A 0x95 0x11; sleep 0.02      # +AVDD
i2cset -y $B $A 0x95 0x11; sleep 0.06      # LCD reset asserted (BL_EN off)
i2cset -y $B $A 0x95 0x13; sleep 0.06      # LCD reset released  (BL_EN still off)
# NOTE: deliberately NO 0x95=0x17 (BL_EN) and NO 0x96 (PWM) here -> backlight stays OFF.
if [ -e /sys/class/graphics/fb0/blank ]; then
  echo 1 > /sys/class/graphics/fb0/blank; sleep 0.2; echo 0 > /sys/class/graphics/fb0/blank
fi
