// =============================================================
//  dashboard_ui.h — EV Dashboard LVGL UI public API
// =============================================================
#pragma once
#include "lvgl.h"
#include "can_parser.h"

// Create all LVGL widgets. Call once after lv_init() and display setup.
void dashboard_ui_create(lv_display_t *disp);

// Update all widgets from a snapshot of DashData. Call each frame.
void dashboard_ui_update(const DashData *d);
