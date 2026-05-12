// =============================================================
//  EV Dashboard — LVGL UI Layer
//  dashboard_ui.h  +  dashboard_ui.c
//
//  Implements the Blazer-EV-style dual-gauge layout:
//    Left sidebar  : SoC bar + range + gear + CAN status
//    Center-left   : Speed arc gauge
//    Center-right  : Power arc gauge (-200 → +1000 kW)
//    Right sidebar : Thermal panel (inverter, motor, batt×2)
//                    + 12V aux + energy stats
//    Bottom bar    : Pack V, Pack A, aux V, SoC%, range
//
//  All gauge arcs drawn with LVGL arc widgets.
//  Colour coding:
//    Speed    : cyan  (#00E5FF)
//    Power+   : green → amber → red (mapped to kW level)
//    Regen−   : purple (#7C3AED)
//    Temps OK : green   warn: amber   crit: red
// =============================================================

#pragma once
#include "lvgl.h"
#include "can_parser.h"

// -------------------------------------------------------------
//  Colour palette  (LVGL lv_color_hex)
// -------------------------------------------------------------
#define CLR_BG          lv_color_hex(0x07090E)
#define CLR_PANEL       lv_color_hex(0x0B0F18)
#define CLR_BORDER      lv_color_hex(0x161C26)
#define CLR_CYAN        lv_color_hex(0x00E5FF)
#define CLR_GREEN       lv_color_hex(0x10B981)
#define CLR_AMBER       lv_color_hex(0xF59E0B)
#define CLR_RED         lv_color_hex(0xEF4444)
#define CLR_PURPLE      lv_color_hex(0x7C3AED)
#define CLR_TEXT_DIM    lv_color_hex(0x2D3A50)
#define CLR_TEXT_MID    lv_color_hex(0x4A5A70)
#define CLR_TEXT_BRIGHT lv_color_hex(0xC8D6E5)
#define CLR_WHITE       lv_color_hex(0xFFFFFF)

// -------------------------------------------------------------
//  Public API
// -------------------------------------------------------------

/**
 * @brief  Build the entire dashboard screen on the given display.
 *         Call once after lv_init() and display creation.
 */
void dashboard_ui_create(lv_display_t *disp);

/**
 * @brief  Push a fresh DashData snapshot into all LVGL widgets.
 *         Call at your target frame rate (e.g. 30 fps).
 *         Must be called from the same task as lv_timer_handler().
 */
void dashboard_ui_update(const DashData *d);

// =============================================================
//  Implementation  (inline in header for single-file simplicity;
//  move to dashboard_ui.c if preferred)
// =============================================================
#ifdef DASHBOARD_UI_IMPL

#include <stdio.h>
#include <math.h>

// -------------------------------------------------------------
//  Widget handles (file-scope)
// -------------------------------------------------------------

// Speed gauge
static lv_obj_t *arc_speed       = NULL;
static lv_obj_t *lbl_speed_val   = NULL;

// Power gauge
static lv_obj_t *arc_pwr_regen   = NULL;   // purple  (regen portion)
static lv_obj_t *arc_pwr_pos     = NULL;   // green/amber/red (motoring)
static lv_obj_t *lbl_pwr_val     = NULL;

// SoC
static lv_obj_t *bar_soc         = NULL;
static lv_obj_t *lbl_soc_val     = NULL;
static lv_obj_t *lbl_range       = NULL;

// Gear
static lv_obj_t *lbl_gear        = NULL;
static lv_obj_t *lbl_gear_btns[5];         // P R N D B

// Thermal
static lv_obj_t *lbl_inv_temp    = NULL;
static lv_obj_t *lbl_mot_temp    = NULL;
static lv_obj_t *lbl_b1_temp     = NULL;
static lv_obj_t *lbl_b2_temp     = NULL;
static lv_obj_t *bar_inv         = NULL;
static lv_obj_t *bar_mot         = NULL;
static lv_obj_t *bar_b1          = NULL;
static lv_obj_t *bar_b2          = NULL;

// Bottom bar
static lv_obj_t *lbl_pack_v      = NULL;
static lv_obj_t *lbl_pack_a      = NULL;
static lv_obj_t *lbl_aux_v       = NULL;

// CAN status dot
static lv_obj_t *dot_can         = NULL;

// -------------------------------------------------------------
//  Helper: map kW to arc colour
// -------------------------------------------------------------
static lv_color_t pwr_color(float kw)
{
    if (kw < 0)         return CLR_PURPLE;
    if (kw > 800.0f)    return CLR_RED;
    if (kw > 500.0f)    return CLR_AMBER;
    return CLR_GREEN;
}

// Helper: temp colour
static lv_color_t temp_color(float val, float warn, float crit)
{
    if (val >= crit) return CLR_RED;
    if (val >= warn) return CLR_AMBER;
    return CLR_GREEN;
}

// Helper: create a styled label
static lv_obj_t *make_label(lv_obj_t *parent, const char *txt,
                             lv_color_t col, lv_coord_t font_size)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, col, 0);
    // Font size selection — adapt to your LVGL font set
    // Assumes fonts: lv_font_montserrat_10/14/18/24/40/56
    const lv_font_t *font = &lv_font_montserrat_14;
    if      (font_size <= 10) font = &lv_font_montserrat_10;
    else if (font_size <= 14) font = &lv_font_montserrat_14;
    else if (font_size <= 18) font = &lv_font_montserrat_18;
    else if (font_size <= 24) font = &lv_font_montserrat_24;
    else if (font_size <= 40) font = &lv_font_montserrat_40;
    else                      font = &lv_font_montserrat_48;
    lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

// Helper: styled arc gauge
static lv_obj_t *make_arc(lv_obj_t *parent,
                           lv_coord_t size,
                           int16_t start_angle, int16_t end_angle,
                           lv_color_t color, lv_coord_t width)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_bg_angles(arc, start_angle, end_angle);
    lv_arc_set_angles(arc, start_angle, start_angle);  // empty initially
    lv_obj_set_style_arc_color(arc, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);   // hide knob
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

// Helper: styled progress bar
static lv_obj_t *make_bar(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_color_t col)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_style_bg_color(bar, CLR_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, col, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    return bar;
}

// -------------------------------------------------------------
//  dashboard_ui_create
// -------------------------------------------------------------
void dashboard_ui_create(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // --- Layout constants for 1280×800 ---
    const lv_coord_t W = 1280, H = 800;
    const lv_coord_t SIDEBAR_W = 220;
    const lv_coord_t BOTTOM_H  = 60;
    const lv_coord_t CTR_W     = (W - 2*SIDEBAR_W) / 2;   // 420px each
    const lv_coord_t CTR_H     = H - BOTTOM_H;             // 740px

    // ===========================================================
    //  LEFT SIDEBAR
    // ===========================================================
    lv_obj_t *left = lv_obj_create(scr);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_size(left, SIDEBAR_W, CTR_H);
    lv_obj_set_style_bg_color(left, CLR_PANEL, 0);
    lv_obj_set_style_border_color(left, CLR_BORDER, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_pad_all(left, 16, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    // SoC label
    lv_obj_t *soc_title = make_label(left, "STATE OF CHARGE", CLR_TEXT_DIM, 10);
    lv_obj_align(soc_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_soc_val = make_label(left, "78%", CLR_WHITE, 40);
    lv_obj_align(lbl_soc_val, LV_ALIGN_TOP_LEFT, 0, 18);

    bar_soc = make_bar(left, SIDEBAR_W - 32, 8, CLR_GREEN);
    lv_bar_set_value(bar_soc, 78, LV_ANIM_OFF);
    lv_obj_align(bar_soc, LV_ALIGN_TOP_LEFT, 0, 72);

    // Range
    lv_obj_t *rng_title = make_label(left, "EST. RANGE", CLR_TEXT_DIM, 10);
    lv_obj_align(rng_title, LV_ALIGN_TOP_LEFT, 0, 90);
    lbl_range = make_label(left, "195 mi", CLR_TEXT_BRIGHT, 24);
    lv_obj_align(lbl_range, LV_ALIGN_TOP_LEFT, 0, 108);

    // Gear big display
    lv_obj_t *gear_title = make_label(left, "GEAR", CLR_TEXT_DIM, 10);
    lv_obj_align(gear_title, LV_ALIGN_TOP_LEFT, 0, 155);
    lbl_gear = make_label(left, "D", CLR_CYAN, 56);
    lv_obj_align(lbl_gear, LV_ALIGN_TOP_LEFT, 0, 170);

    // PRNDL strip
    const char *gnames[] = {"P","R","N","D","B"};
    for (int i = 0; i < 5; i++) {
        lbl_gear_btns[i] = make_label(left, gnames[i], CLR_TEXT_DIM, 14);
        lv_obj_align(lbl_gear_btns[i], LV_ALIGN_TOP_LEFT, i*32, 238);
    }
    // Default D active
    lv_obj_set_style_text_color(lbl_gear_btns[3], CLR_CYAN, 0);

    // CAN status
    dot_can = lv_obj_create(left);
    lv_obj_set_size(dot_can, 8, 8);
    lv_obj_set_style_radius(dot_can, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_can, CLR_GREEN, 0);
    lv_obj_set_style_border_width(dot_can, 0, 0);
    lv_obj_align(dot_can, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    lv_obj_t *can_lbl = make_label(left, "CAN · 500 kbps", CLR_TEXT_DIM, 10);
    lv_obj_align(can_lbl, LV_ALIGN_BOTTOM_LEFT, 14, -2);

    // ===========================================================
    //  CENTER-LEFT: SPEED GAUGE
    // ===========================================================
    lv_obj_t *ctr_l = lv_obj_create(scr);
    lv_obj_set_pos(ctr_l, SIDEBAR_W, 0);
    lv_obj_set_size(ctr_l, CTR_W, CTR_H);
    lv_obj_set_style_bg_color(ctr_l, CLR_BG, 0);
    lv_obj_set_style_border_color(ctr_l, CLR_BORDER, 0);
    lv_obj_set_style_border_width(ctr_l, 1, 0);
    lv_obj_set_style_radius(ctr_l, 0, 0);
    lv_obj_clear_flag(ctr_l, LV_OBJ_FLAG_SCROLLABLE);

    // Arc gauge: 220° sweep, start=130° (7 o'clock), end=410° (5 o'clock)
    // LVGL angles: 0=right, 90=bottom, 180=left, 270=top
    // Our 220° arc: start at 135°, end at 355°
    arc_speed = make_arc(ctr_l, 360, 135, 355, CLR_CYAN, 10);
    lv_obj_align(arc_speed, LV_ALIGN_CENTER, 0, -20);

    // Background track (full arc, dimmer)
    lv_obj_set_style_arc_color(arc_speed, lv_color_hex(0x0F1520), LV_PART_MAIN);

    lv_obj_t *spd_unit = make_label(ctr_l, "MPH", CLR_TEXT_DIM, 14);
    lv_obj_align(spd_unit, LV_ALIGN_CENTER, 0, 100);

    lbl_speed_val = make_label(ctr_l, "0", CLR_WHITE, 56);
    lv_obj_align(lbl_speed_val, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *spd_title = make_label(ctr_l, "SPEED", CLR_TEXT_DIM, 10);
    lv_obj_align(spd_title, LV_ALIGN_BOTTOM_MID, 0, -8);

    // ===========================================================
    //  CENTER-RIGHT: POWER GAUGE
    // ===========================================================
    lv_obj_t *ctr_r = lv_obj_create(scr);
    lv_obj_set_pos(ctr_r, SIDEBAR_W + CTR_W, 0);
    lv_obj_set_size(ctr_r, CTR_W, CTR_H);
    lv_obj_set_style_bg_color(ctr_r, CLR_BG, 0);
    lv_obj_set_style_border_color(ctr_r, CLR_BORDER, 0);
    lv_obj_set_style_border_width(ctr_r, 1, 0);
    lv_obj_set_style_radius(ctr_r, 0, 0);
    lv_obj_clear_flag(ctr_r, LV_OBJ_FLAG_SCROLLABLE);

    // Power gauge shares the same arc sweep angles as speed.
    // Zero kW sits at the arc start + 16.67% of sweep = 135 + 36.7 ≈ 172°
    // We use two overlapping arcs:
    //   arc_pwr_regen : from 135° → 172°  (regen zone, purple, fills leftward from 172)
    //   arc_pwr_pos   : from 172° → 355°  (motoring zone, green/amber/red)

    // Track (full arc, dimmer)
    lv_obj_t *arc_pwr_track = make_arc(ctr_r, 360, 135, 355, CLR_GREEN, 10);
    lv_obj_align(arc_pwr_track, LV_ALIGN_CENTER, 0, -20);
    lv_arc_set_angles(arc_pwr_track, 135, 355);  // show full dim track
    lv_obj_set_style_arc_color(arc_pwr_track, lv_color_hex(0x0F1520), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_pwr_track, lv_color_hex(0x0F1520), LV_PART_INDICATOR);

    arc_pwr_regen = make_arc(ctr_r, 360, 135, 172, CLR_PURPLE, 10);
    lv_obj_align(arc_pwr_regen, LV_ALIGN_CENTER, 0, -20);

    arc_pwr_pos = make_arc(ctr_r, 360, 172, 172, CLR_GREEN, 10);
    lv_obj_align(arc_pwr_pos, LV_ALIGN_CENTER, 0, -20);

    lbl_pwr_val = make_label(ctr_r, "0", CLR_GREEN, 56);
    lv_obj_align(lbl_pwr_val, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *pwr_unit = make_label(ctr_r, "kW", CLR_TEXT_DIM, 14);
    lv_obj_align(pwr_unit, LV_ALIGN_CENTER, 0, 100);

    lv_obj_t *regen_lbl = make_label(ctr_r, "REGEN", lv_color_hex(0x7C3AED), 10);
    lv_obj_align(regen_lbl, LV_ALIGN_BOTTOM_LEFT, 20, -8);

    lv_obj_t *pwr_lbl2 = make_label(ctr_r, "POWER", CLR_GREEN, 10);
    lv_obj_align(pwr_lbl2, LV_ALIGN_BOTTOM_RIGHT, -20, -8);

    // ===========================================================
    //  RIGHT SIDEBAR — Thermal
    // ===========================================================
    lv_obj_t *right = lv_obj_create(scr);
    lv_obj_set_pos(right, W - SIDEBAR_W, 0);
    lv_obj_set_size(right, SIDEBAR_W, CTR_H);
    lv_obj_set_style_bg_color(right, CLR_PANEL, 0);
    lv_obj_set_style_border_color(right, CLR_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_pad_all(right, 14, 0);
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *therm_title = make_label(right, "THERMAL", CLR_TEXT_DIM, 10);
    lv_obj_align(therm_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Inverter temp
    lv_obj_t *inv_lbl = make_label(right, "INVERTER", CLR_TEXT_DIM, 10);
    lv_obj_align(inv_lbl, LV_ALIGN_TOP_LEFT, 0, 22);
    lbl_inv_temp = make_label(right, "42°C", CLR_GREEN, 18);
    lv_obj_align(lbl_inv_temp, LV_ALIGN_TOP_RIGHT, 0, 18);
    bar_inv = make_bar(right, SIDEBAR_W-28, 3, CLR_GREEN);
    lv_bar_set_value(bar_inv, 35, LV_ANIM_OFF);
    lv_obj_align(bar_inv, LV_ALIGN_TOP_LEFT, 0, 44);

    // Motor temp
    lv_obj_t *mot_lbl = make_label(right, "MOTOR", CLR_TEXT_DIM, 10);
    lv_obj_align(mot_lbl, LV_ALIGN_TOP_LEFT, 0, 60);
    lbl_mot_temp = make_label(right, "68°C", CLR_GREEN, 18);
    lv_obj_align(lbl_mot_temp, LV_ALIGN_TOP_RIGHT, 0, 56);
    bar_mot = make_bar(right, SIDEBAR_W-28, 3, CLR_GREEN);
    lv_bar_set_value(bar_mot, 45, LV_ANIM_OFF);
    lv_obj_align(bar_mot, LV_ALIGN_TOP_LEFT, 0, 82);

    // Battery temp (both packs from same signal for now)
    lv_obj_t *b1_lbl = make_label(right, "BATT 1", CLR_TEXT_DIM, 10);
    lv_obj_align(b1_lbl, LV_ALIGN_TOP_LEFT, 0, 98);
    lbl_b1_temp = make_label(right, "31°C", CLR_GREEN, 18);
    lv_obj_align(lbl_b1_temp, LV_ALIGN_TOP_RIGHT, 0, 94);
    bar_b1 = make_bar(right, SIDEBAR_W-28, 3, CLR_GREEN);
    lv_bar_set_value(bar_b1, 20, LV_ANIM_OFF);
    lv_obj_align(bar_b1, LV_ALIGN_TOP_LEFT, 0, 120);

    lv_obj_t *b2_lbl = make_label(right, "BATT 2", CLR_TEXT_DIM, 10);
    lv_obj_align(b2_lbl, LV_ALIGN_TOP_LEFT, 0, 136);
    lbl_b2_temp = make_label(right, "31°C", CLR_GREEN, 18);
    lv_obj_align(lbl_b2_temp, LV_ALIGN_TOP_RIGHT, 0, 132);
    bar_b2 = make_bar(right, SIDEBAR_W-28, 3, CLR_GREEN);
    lv_bar_set_value(bar_b2, 20, LV_ANIM_OFF);
    lv_obj_align(bar_b2, LV_ALIGN_TOP_LEFT, 0, 158);

    // 12V Aux
    lv_obj_t *aux_lbl = make_label(right, "12V AUX", CLR_TEXT_DIM, 10);
    lv_obj_align(aux_lbl, LV_ALIGN_TOP_LEFT, 0, 185);
    lbl_aux_v = make_label(right, "13.8 V", CLR_TEXT_BRIGHT, 18);
    lv_obj_align(lbl_aux_v, LV_ALIGN_TOP_RIGHT, 0, 181);

    // Pack voltage / amps
    lv_obj_t *pv_lbl = make_label(right, "PACK V", CLR_TEXT_DIM, 10);
    lv_obj_align(pv_lbl, LV_ALIGN_TOP_LEFT, 0, 220);
    lbl_pack_v = make_label(right, "396.0 V", CLR_TEXT_BRIGHT, 18);
    lv_obj_align(lbl_pack_v, LV_ALIGN_TOP_RIGHT, 0, 216);

    lv_obj_t *pa_lbl = make_label(right, "PACK A", CLR_TEXT_DIM, 10);
    lv_obj_align(pa_lbl, LV_ALIGN_TOP_LEFT, 0, 252);
    lbl_pack_a = make_label(right, "0 A", CLR_TEXT_BRIGHT, 18);
    lv_obj_align(lbl_pack_a, LV_ALIGN_TOP_RIGHT, 0, 248);

    // ===========================================================
    //  BOTTOM BAR
    // ===========================================================
    lv_obj_t *bot = lv_obj_create(scr);
    lv_obj_set_pos(bot, 0, CTR_H);
    lv_obj_set_size(bot, W, BOTTOM_H);
    lv_obj_set_style_bg_color(bot, lv_color_hex(0x050709), 0);
    lv_obj_set_style_border_color(bot, CLR_BORDER, 0);
    lv_obj_set_style_border_width(bot, 1, 0);
    lv_obj_set_style_border_side(bot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(bot, 0, 0);
    lv_obj_clear_flag(bot, LV_OBJ_FLAG_SCROLLABLE);

    // Bottom labels: evenly spaced
    struct { lv_obj_t **lbl; const char *name; const char *init; } bot_items[] = {
        {NULL,        "ODOMETER",  "12,847 mi"},
        {&lbl_pack_v, "PACK V",    "396.0 V"},
        {&lbl_pack_a, "PACK A",    "0 A"},
        {&lbl_aux_v,  "12V AUX",   "13.8 V"},
        {NULL,        "ESP32-P4",  "Waveshare 10.1\" DSI"},
    };
    int n = sizeof(bot_items)/sizeof(bot_items[0]);
    for (int i = 0; i < n; i++) {
        lv_coord_t x = (lv_coord_t)(W * (i*2+1) / (n*2));
        lv_obj_t *t = make_label(bot, bot_items[i].name, CLR_TEXT_DIM, 10);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, x - W/2 - 30, -10);
        lv_obj_t *v = make_label(bot, bot_items[i].init, CLR_TEXT_MID, 14);
        lv_obj_align(v, LV_ALIGN_LEFT_MID, x - W/2 - 30, 8);
        if (bot_items[i].lbl) *bot_items[i].lbl = v;
    }
}

// -------------------------------------------------------------
//  dashboard_ui_update — called at 30 fps from ui_task
// -------------------------------------------------------------
void dashboard_ui_update(const DashData *d)
{
    char buf[32];

    // --- Speed arc ---
    // Map 0–180 mph → 0–100 (arc range 135°–355° = 220°)
    int16_t spd_end = (int16_t)(135 + (d->speed_mph / 180.0f) * 220.0f);
    if (spd_end > 355) spd_end = 355;
    lv_arc_set_angles(arc_speed, 135, spd_end);
    snprintf(buf, sizeof(buf), "%d", (int)d->speed_mph);
    lv_label_set_text(lbl_speed_val, buf);

    // --- Power arc ---
    // Map -200..+1000 kW → arc 135°..355° (zero at 172°)
    float pct = (d->power_kw - (-200.0f)) / 1200.0f;
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
    int16_t pwr_deg = (int16_t)(135.0f + pct * 220.0f);
    const int16_t ZERO_DEG = 172;  // 135 + (200/1200)*220

    if (d->power_kw < 0) {
        lv_arc_set_angles(arc_pwr_regen, pwr_deg, ZERO_DEG);
        lv_arc_set_angles(arc_pwr_pos,   ZERO_DEG, ZERO_DEG);
    } else {
        lv_arc_set_angles(arc_pwr_regen, ZERO_DEG, ZERO_DEG);
        lv_arc_set_angles(arc_pwr_pos,   ZERO_DEG, pwr_deg);
        lv_color_t pc = pwr_color(d->power_kw);
        lv_obj_set_style_arc_color(arc_pwr_pos, pc, LV_PART_INDICATOR);
    }
    snprintf(buf, sizeof(buf), "%+d", (int)d->power_kw);
    lv_label_set_text(lbl_pwr_val, buf);
    lv_obj_set_style_text_color(lbl_pwr_val, pwr_color(d->power_kw), 0);

    // --- SoC ---
    lv_bar_set_value(bar_soc, (int32_t)d->soc_pct, LV_ANIM_OFF);
    lv_color_t soc_col = d->soc_pct < 20 ? CLR_RED : d->soc_pct < 40 ? CLR_AMBER : CLR_GREEN;
    lv_obj_set_style_bg_color(bar_soc, soc_col, LV_PART_INDICATOR);
    snprintf(buf, sizeof(buf), "%d%%", (int)d->soc_pct);
    lv_label_set_text(lbl_soc_val, buf);

    // --- Range ---
    snprintf(buf, sizeof(buf), "%.0f mi", d->range_mi);
    lv_label_set_text(lbl_range, buf);

    // --- Gear ---
    const char *gnames[] = {"P","R","N","D","B"};
    lv_label_set_text(lbl_gear, gnames[d->gear < 5 ? d->gear : 0]);
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_text_color(lbl_gear_btns[i],
            i == d->gear ? CLR_CYAN : CLR_TEXT_DIM, 0);
    }

    // --- Thermal ---
    auto update_temp = [](lv_obj_t *lbl, lv_obj_t *bar, float val,
                          float warn, float crit, float max_scale) {
        char b[16];
        snprintf(b, sizeof(b), "%.0f°C", val);
        lv_label_set_text(lbl, b);
        lv_color_t c = temp_color(val, warn, crit);
        lv_obj_set_style_text_color(lbl, c, 0);
        lv_obj_set_style_bg_color(bar, c, LV_PART_INDICATOR);
        int32_t pct = (int32_t)(val / max_scale * 100.0f);
        if (pct > 100) pct = 100;
        lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    };
    update_temp(lbl_inv_temp, bar_inv, d->inverter_temp_c, WARN_INV_TEMP_C, CRIT_INV_TEMP_C, 120.0f);
    update_temp(lbl_mot_temp, bar_mot, d->motor_temp_c,    WARN_MOTOR_TEMP_C, CRIT_MOTOR_TEMP_C, 150.0f);
    update_temp(lbl_b1_temp,  bar_b1,  d->batt_temp_c,    WARN_BATT_TEMP_C, CRIT_BATT_TEMP_C, 60.0f);
    update_temp(lbl_b2_temp,  bar_b2,  d->batt_temp_c,    WARN_BATT_TEMP_C, CRIT_BATT_TEMP_C, 60.0f);

    // --- Electrical ---
    snprintf(buf, sizeof(buf), "%.1f V", d->pack_volts);
    lv_label_set_text(lbl_pack_v, buf);
    snprintf(buf, sizeof(buf), "%.0f A", d->pack_amps);
    lv_label_set_text(lbl_pack_a, buf);
    snprintf(buf, sizeof(buf), "%.1f V", d->aux_volts);
    lv_label_set_text(lbl_aux_v, buf);
}

#endif  // DASHBOARD_UI_IMPL
