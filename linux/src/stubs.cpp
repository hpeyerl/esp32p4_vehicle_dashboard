// SPDX-FileCopyrightText: 2026 Herb Peyerl
// SPDX-License-Identifier: BSD-3-Clause

// =============================================================
//  stubs.cpp — Linux implementations of the few external symbols
//  the portable dashboard UI references.
//
//  Compiled as C++ so each symbol gets the linkage its header
//  dictates: gear_shifter.h and wifi_manager.h are extern "C"
//  (→ C linkage), while can_parser.h is not (→ C++ linkage for
//  can_signal_stale, matching dashboard_ui.cpp).
//
//  On the ESP32 these are backed by WiFi and a CAN gear-shift TX
//  task. For the Pi prototype they're inert: connectivity reports
//  "not connected", a gear request is just remembered, and every
//  CAN signal is treated as live so the sim data isn't flagged stale.
// =============================================================
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "gear_shifter.h"   // extern "C"  (clean, no ESP deps)
#include "wifi_manager.h"   // extern "C"  (compat shim)

// NOTE: g_dash and can_signal_stale now come from the real can_parser.cpp,
// which is compiled into the build (it also provides parse_can_frame for the
// SocketCAN path). Don't redefine them here.

// --- wifi_manager: report the REAL link state on Linux ---
// A wifi iface exposes /sys/class/net/<if>/wireless/; operstate "up" = associated
// with carrier. Scans all ifaces so it works for wlan0 / wlp* alike. No deps.
bool wifi_manager_is_connected(void)
{
    DIR *d = opendir("/sys/class/net");
    if (!d) return false;
    bool up = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && !up) {
        if (e->d_name[0] == '.') continue;
        char path[300];
        struct stat st;
        snprintf(path, sizeof path, "/sys/class/net/%s/wireless", e->d_name);
        if (stat(path, &st) != 0) continue;          // not a wireless iface
        snprintf(path, sizeof path, "/sys/class/net/%s/operstate", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char s[16] = {0};
        if (fgets(s, sizeof s, f) && strncmp(s, "up", 2) == 0) up = true;
        fclose(f);
    }
    closedir(d);
    return up;
}

// --- gear_shifter (no CAN on Linux; just track the request) ---
static volatile int8_t s_gear = -1;   // 0=P 1=R 2=N 3=D, -1 = none
void   gear_shifter_start(void)          { /* no-op on Linux */ }
void   gear_shifter_request(int8_t gear) { s_gear = gear; }
int8_t gear_shifter_current(void)        { return s_gear; }
