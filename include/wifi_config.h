// =============================================================
//  wifi_config.h — WiFi + OTA server configuration
//
//  STA credentials come from secrets.h (gitignored).
//  If secrets.h is absent or doesn't define the credentials,
//  the device falls back to AP mode automatically.
// =============================================================

#pragma once

// Pull in secrets.h if it exists — gitignored, never committed.
// Create include/secrets.h with your actual credentials.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

// ── Station mode credentials ──────────────────────────────────
// Defined in secrets.h. If absent, STA connect will fail and
// the device falls back to AP mode (see ota_server.c).
#ifndef OTA_STA_SSID
  #define OTA_STA_SSID      ""
#endif
#ifndef OTA_STA_PASSWORD
  #define OTA_STA_PASSWORD  ""
#endif

// ── Access Point fallback ─────────────────────────────────────
#define OTA_AP_SSID       "ev-dashboard"
#define OTA_AP_PASSWORD   "dashboard1"
#define OTA_AP_CHANNEL    6
#define OTA_AP_MAX_CONN   2

// ── HTTP server ───────────────────────────────────────────────
#define OTA_HTTP_PORT     80

// ── mDNS hostname ─────────────────────────────────────────────
#define OTA_MDNS_HOSTNAME "ev-dashboard"
