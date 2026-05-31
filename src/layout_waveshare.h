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
// 160° sweep, ARC_R=320 tuned for 1280×720 MJPEG preview.
// Inner edge of each bracket lands ~176px from speed center (x≈640).
// Arc height ≈ 630px — nearly full screen height.
// Revisit ARC_R for 1920×720 when real display arrives.
#define ARC_R       320    // arc radius (affects size/height only)
#define ARC_INSET   700    // distance from panel edge to arc center (affects position)
#define ARC_W        50    // arc stroke width
#define ARC_START   100    // SOC arc: 100°→260° = 160° CW sweep
#define ARC_END     260
#define PWR_ARC_START 280  // PWR arc: 280°→80° = 160° CW sweep (symmetric)
#define PWR_ARC_END    80

// ── Meter gauge geometry ──────────────────────────────────────
#define METER_R      52    // arc radius
#define METER_W       8    // arc stroke width

// ── Navigation bar ────────────────────────────────────────────
#define NAV_ICON_W   80
#define NAV_ICON_H   50
#define NAV_ICON_CNT  3    // Home, Settings, Status
