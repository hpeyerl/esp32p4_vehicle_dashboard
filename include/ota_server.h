// =============================================================
//  ota_server.h — Web-based OTA firmware update server
//
//  Starts WiFi, mDNS, and a minimal HTTP server that accepts
//  firmware uploads and performs live OTA updates.
//
//  Usage (call once from app_main after display init):
//    ota_server_start();
//
//  Then from your lab:
//    curl -X POST http://ev-dashboard.local/update \
//         -H "Content-Type: application/octet-stream" \
//         --data-binary @.pio/build/waveshare/firmware.bin
//
//  Or use the browser UI at http://ev-dashboard.local/
//
//  The device reboots automatically after a successful flash.
//  If the new firmware crashes before calling
//  ota_server_mark_valid(), the bootloader rolls back to the
//  previous image on next boot.
// =============================================================

#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start WiFi + mDNS + HTTP OTA server.
// Non-blocking — spawns tasks internally.
// Call once from app_main.
esp_err_t ota_server_start(void);

// Call this after your app has confirmed it's working correctly.
// Marks the running OTA image as valid, preventing rollback.
// Safe to call multiple times.
// If you never call this after an OTA update, the bootloader
// rolls back to the previous image on the next reboot.
void ota_server_mark_valid(void);

#ifdef __cplusplus
}
#endif
