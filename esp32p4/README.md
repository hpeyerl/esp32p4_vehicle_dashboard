# ESP32-P4 line — frozen archive

This directory is a **read-only snapshot** of the original ESP32-P4 / ESP-IDF
build (PlatformIO project, `sdkconfig.*`, boards, ESP-only sources). It is kept
for reference and **is not built from `main`** — the shared UI it depends on
(`dashboard_ui.cpp`, `can_parser.cpp`, fonts) now lives at the repo root under
`../src`, so `pio run` will not work here as-is.

For a buildable, self-contained ESP32-P4 tree — plus the board wiring, pin map
and OTA flashing notes — check out the **`esp32p4` branch**, which is the
canonical frozen line:

```sh
git checkout esp32p4
```

Suited to a **2-lane** DSI panel or the **M5Stack Tab5**. The active 4-lane
(Radxa CM3 / Linux) dashboard is on `main` — see the top-level [README](../README.md).
