#!/bin/sh
# Turn the panel backlight ON once the dashboard is up (its HTTP API responds), so the
# user sees black -> dashboard instead of the boot console. i2c-0 / MCU@0x45 is
# independent of the DSI framebuffer, so this works fine while the app owns /dev/fb0.
for i in $(seq 1 150); do
  curl -sf --max-time 1 http://localhost:8080/api/status >/dev/null 2>&1 && break
  sleep 0.1
done
sleep 0.3                        # let the first frame paint
i2cset -y 0 0x45 0x95 0x17       # +BL_EN (keeps AVDD/IOVCC/reset-released bits)
i2cset -y 0 0x45 0x96 0xff       # backlight PWM full
exit 0
