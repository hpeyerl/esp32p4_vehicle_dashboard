// =============================================================
//  display_driver.c — Compile-time backend selector
// =============================================================

#include "display_driver.h"

#if DISPLAY_STUB
// ── Backend: headless stub ────────────────────────────────────────────────
#include "display_stub.h"

esp_err_t display_init(lv_display_t **disp_out)
{
    return display_stub_init(disp_out);
}

esp_lcd_panel_handle_t display_get_panel(void)
{
    return display_stub_get_panel();
}

#elif DISPLAY_TARGET == DISPLAY_TARGET_TAB5
// ── Backend: M5Stack Tab5 ─────────────────────────────────────────────────
#include "tab5_display.h"

esp_err_t display_init(lv_display_t **disp_out)
{
    return tab5_display_init(disp_out);
}

esp_lcd_panel_handle_t display_get_panel(void)
{
    return tab5_get_panel();
}

#elif DISPLAY_TARGET == DISPLAY_TARGET_WAVESHARE
// ── Backend: Waveshare 12.3" HX8399-C ────────────────────────────────────
#include "waveshare_display.h"

esp_err_t display_init(lv_display_t **disp_out)
{
    return ws_display_init(disp_out);
}

esp_lcd_panel_handle_t display_get_panel(void)
{
    return ws_get_panel();
}

#else
#error "Unknown DISPLAY_TARGET. See display_driver.h."
#endif
