// =============================================================
//  wifi_config.h — WiFi credentials for OTA server
//
//  !! ADD THIS FILE TO .gitignore !!
//
//  Two modes (set OTA_WIFI_MODE in platformio.ini):
//    OTA_WIFI_MODE_STA  — joins your existing network (recommended
//                         for workshop use; device gets DHCP address
//                         printed to serial on boot)
//    OTA_WIFI_MODE_AP   — device creates its own AP; useful when no
//                         infrastructure WiFi is available in the car
// =============================================================

#pragma once

// ── Station mode (join existing network) ─────────────────────
#define OTA_STA_SSID      "your_network_ssid"
#define OTA_STA_PASSWORD  "your_network_password"

// ── Access Point mode (device creates AP) ────────────────────
#define OTA_AP_SSID       "ev-dashboard"
#define OTA_AP_PASSWORD   "dashboard1"   // min 8 chars; set "" for open AP
#define OTA_AP_CHANNEL    6
#define OTA_AP_MAX_CONN   2

// ── OTA HTTP server port ──────────────────────────────────────
#define OTA_HTTP_PORT     80

// ── mDNS hostname — access via http://ev-dashboard.local ─────
#define OTA_MDNS_HOSTNAME "ev-dashboard"
