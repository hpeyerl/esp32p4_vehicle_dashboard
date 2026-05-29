// =============================================================
//  layout_waveshare.h — Dashboard geometry for Waveshare 12.3"
//  1920×720 landscape, DSI, HX8399-C
//
//  Tighter arc brackets + nav bar at bottom.
// =============================================================
#pragma once

// ── Panel widths ──────────────────────────────────────────────
#define LEFT_W      200    // left panel (5 mini meters)
#define RIGHT_W     200    // right panel (eff / trip / aux)
#define BOT_H        60    // bottom nav bar height
#define PWR_FULL    200.0f

// ── SOC / Power arc geometry ──────────────────────────────────
// Smaller radius brackets the speed tighter on a wide screen
#define ARC_R       220    // arc radius
#define ARC_W        18    // arc stroke width
#define ARC_START   120    // SOC arc start angle (LVGL degrees)
#define ARC_END     240    // SOC arc end angle
#define PWR_ARC_START 300
#define PWR_ARC_END    60

// ── Meter gauge geometry ──────────────────────────────────────
#define METER_R      52    // arc radius
#define METER_W       8    // arc stroke width

// ── Navigation bar ────────────────────────────────────────────
#define NAV_ICON_W   80
#define NAV_ICON_H   50
#define NAV_ICON_CNT  3    // Home, Settings, Status
