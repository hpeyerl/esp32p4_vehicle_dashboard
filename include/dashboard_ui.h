// =============================================================
//  dashboard_ui.h — EV Dashboard LVGL UI public API
// =============================================================
#pragma once
#include "lvgl.h"
#include "can_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DASH_SCREEN_HOME     = 0,
    DASH_SCREEN_SETTINGS = 1,
    DASH_SCREEN_STATUS   = 2,
} dash_screen_t;

// Create all LVGL widgets. Call once after lv_init() and display setup.
void dashboard_ui_create(lv_display_t *disp);

// Update all widgets from a snapshot of DashData. Call each frame.
void dashboard_ui_update(const DashData *d);

// Switch active screen. Safe to call from any task (queues the switch).
void dashboard_ui_set_screen(dash_screen_t screen);

// Get current screen
dash_screen_t dashboard_ui_get_screen(void);

#ifdef __cplusplus
}
#endif
