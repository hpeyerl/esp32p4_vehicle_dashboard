// =============================================================
//  EV Dashboard — LVGL UI Layer  v7
//  dashboard_ui.h
//
//  Layout (landscape 1280×720, SW-rotated to portrait for Tab5):
//
//  ┌──────────────┬──┬──────────────────┬──┬──────────────┐
//  │  THERMAL     │S │                  │P │  EFFICIENCY  │
//  │  inv/mot/bat │O │    72  MPH       │W │  + FJ55 img  │
//  │  PackV/A/12V │C │    D  PRND       │R │              │
//  ├──────────────┴──┴──────────────────┴──┴──────────────┤
//  │  CAN 500kbps          M5Stack Tab5              • CAN │
//  └──────────────────────────────────────────────────────┘
// =============================================================

#pragma once
#include "lvgl.h"
#include "can_parser.h"
#include "evj55_splash_c.h"
#include "units.h"

// Colours
#define CLR_BG          lv_color_hex(0x07090E)
#define CLR_PANEL       lv_color_hex(0x0B0F18)
#define CLR_BORDER      lv_color_hex(0x161C26)
#define CLR_CYAN        lv_color_hex(0x00E5FF)
#define CLR_GREEN       lv_color_hex(0x10B981)
#define CLR_AMBER       lv_color_hex(0xF59E0B)
#define CLR_RED         lv_color_hex(0xEF4444)
#define CLR_TEXT_DIM    lv_color_hex(0x2D3A50)
#define CLR_TEXT_MID    lv_color_hex(0x4A5A70)
#define CLR_TEXT_BRIGHT lv_color_hex(0xC8D6E5)
#define CLR_WHITE       lv_color_hex(0xFFFFFF)
#define CLR_ORANGE      lv_color_hex(0xF59E0B)

// Layout constants (landscape 1280×720)
#define LEFT_W    260     // thermal sidebar width
#define RIGHT_W   240     // efficiency/image sidebar width
#define BAR_W      24     // bar graph width
#define BAR_H     400     // bar graph height
#define BAR_TOP    40     // bar top y (relative to screen)
#define BOT_H      52     // bottom bar height

void dashboard_ui_create(lv_display_t *disp);
void dashboard_ui_update(const DashData *d);

// =============================================================
#ifdef DASHBOARD_UI_IMPL
#include <stdio.h>

// Widget handles
static lv_obj_t *bar_soc         = NULL;   // SOC filled portion
static lv_obj_t *lbl_soc_pct     = NULL;
static lv_obj_t *lbl_range       = NULL;
static lv_obj_t *bar_pwr         = NULL;   // power filled portion
static lv_obj_t *lbl_pwr_val     = NULL;
static lv_obj_t *lbl_speed_val   = NULL;
static lv_obj_t *lbl_gear        = NULL;
static lv_obj_t *lbl_gear_btns[5];
static lv_obj_t *lbl_inv_temp    = NULL;
static lv_obj_t *lbl_mot_temp    = NULL;
static lv_obj_t *lbl_batt_temp   = NULL;
static lv_obj_t *lbl_pack_v      = NULL;
static lv_obj_t *lbl_pack_a      = NULL;
static lv_obj_t *lbl_aux_v       = NULL;
static lv_obj_t *lbl_efficiency  = NULL;
static lv_obj_t *lbl_trip_kwh    = NULL;
static lv_obj_t *dot_can         = NULL;

// Bar graph max power (kW) — full scale each direction
#define PWR_FULL_SCALE  200.0f

static lv_color_t temp_color(float v, float warn, float crit)
{
    if (v >= crit) return CLR_RED;
    if (v >= warn) return CLR_AMBER;
    return CLR_GREEN;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt,
                              lv_color_t col, const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, col, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

// Create a thin vertical bar (track + fill as two overlaid rects)
// Returns the fill rect handle. Track is created as sibling.
static lv_obj_t *make_bar_track(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                  lv_coord_t w, lv_coord_t h)
{
    // Track (background)
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_size(track, w, h);
    lv_obj_set_style_bg_color(track, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_radius(track, 3, 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    // Fill (sits on top, height adjusted dynamically)
    lv_obj_t *fill = lv_obj_create(parent);
    lv_obj_set_pos(fill, x, y + h);  // starts empty (zero height at bottom)
    lv_obj_set_size(fill, w, 0);
    lv_obj_set_style_bg_color(fill, CLR_CYAN, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    return fill;
}

// Update a bottom-anchored bar: pct 0.0-1.0, bar track at (x,y) height h
static void set_bar(lv_obj_t *fill, float pct,
                     lv_coord_t track_x, lv_coord_t track_y,
                     lv_coord_t track_h, lv_coord_t bar_w,
                     lv_color_t color)
{
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
    lv_coord_t fill_h = (lv_coord_t)(pct * track_h);
    lv_obj_set_pos(fill, track_x, track_y + track_h - fill_h);
    lv_obj_set_size(fill, bar_w, fill_h);
    lv_obj_set_style_bg_color(fill, color, 0);
}

void dashboard_ui_create(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    const lv_coord_t W      = LCD_H_RES;   // 1280
    const lv_coord_t H      = LCD_V_RES;   // 720
    const lv_coord_t MAIN_H = H - BOT_H;   // 668

    // Bar positions — hugging the speed number
    const lv_coord_t SOC_BAR_X  = LEFT_W + 8;
    const lv_coord_t PWR_BAR_X  = W - RIGHT_W - BAR_W - 8;
    const lv_coord_t CTR_X      = LEFT_W + BAR_W + 16;
    const lv_coord_t CTR_W      = PWR_BAR_X - CTR_X;

    // ===========================================================
    //  LEFT SIDEBAR — Thermals
    // ===========================================================
    lv_obj_t *left = lv_obj_create(scr);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_size(left, LEFT_W, MAIN_H);
    lv_obj_set_style_bg_color(left, CLR_PANEL, 0);
    lv_obj_set_style_border_color(left, CLR_BORDER, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_pad_left(left, 16, 0);
    lv_obj_set_style_pad_top(left, 16, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    struct {
        lv_obj_t **lbl;
        const char *tag;
        float warn, crit;
    } temps[] = {
        {&lbl_inv_temp,  "INVERTER", WARN_INV_TEMP_C,   CRIT_INV_TEMP_C},
        {&lbl_mot_temp,  "MOTOR",    WARN_MOTOR_TEMP_C, CRIT_MOTOR_TEMP_C},
        {&lbl_batt_temp, "BATTERY",  WARN_BATT_TEMP_C,  CRIT_BATT_TEMP_C},
    };
    for (int i = 0; i < 3; i++) {
        lv_coord_t y = (lv_coord_t)(i * 130);
        lv_obj_t *tag = make_label(left, temps[i].tag, CLR_TEXT_MID,
                                    &lv_font_montserrat_10);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 0, y);
        *temps[i].lbl = make_label(left, "0°C", CLR_GREEN,
                                    &lv_font_montserrat_40);
        lv_obj_align(*temps[i].lbl, LV_ALIGN_TOP_LEFT, 0, y + 16);
    }

    lv_obj_t *div = lv_obj_create(left);
    lv_obj_set_size(div, LEFT_W - 16, 1);
    lv_obj_set_style_bg_color(div, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_align(div, LV_ALIGN_TOP_LEFT, 0, 395);

    struct { lv_obj_t **lbl; const char *tag; const char *init; } elec[] = {
        {&lbl_pack_v, "PACK VOLTAGE", "0.0 V"},
        {&lbl_pack_a, "PACK AMPS",    "0 A"},
        {&lbl_aux_v,  "12V AUX",      "0.0 V"},
    };
    for (int i = 0; i < 3; i++) {
        lv_coord_t y = (lv_coord_t)(410 + i * 80);
        lv_obj_t *tag = make_label(left, elec[i].tag, CLR_TEXT_MID,
                                    &lv_font_montserrat_10);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 0, y);
        *elec[i].lbl = make_label(left, elec[i].init, CLR_TEXT_BRIGHT,
                                   &lv_font_montserrat_24);
        lv_obj_align(*elec[i].lbl, LV_ALIGN_TOP_LEFT, 0, y + 14);
    }

    // ===========================================================
    //  SOC BAR
    // ===========================================================
    // Track
    lv_obj_t *soc_track = lv_obj_create(scr);
    lv_obj_set_pos(soc_track, SOC_BAR_X, BAR_TOP);
    lv_obj_set_size(soc_track, BAR_W, BAR_H);
    lv_obj_set_style_bg_color(soc_track, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(soc_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(soc_track, 0, 0);
    lv_obj_set_style_radius(soc_track, 3, 0);
    lv_obj_clear_flag(soc_track, LV_OBJ_FLAG_SCROLLABLE);

    // Fill (dynamic)
    bar_soc = lv_obj_create(scr);
    lv_obj_set_pos(bar_soc, SOC_BAR_X, BAR_TOP + BAR_H);
    lv_obj_set_size(bar_soc, BAR_W, 0);
    lv_obj_set_style_bg_color(bar_soc, CLR_CYAN, 0);
    lv_obj_set_style_bg_opa(bar_soc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_soc, 0, 0);
    lv_obj_set_style_radius(bar_soc, 3, 0);
    lv_obj_clear_flag(bar_soc, LV_OBJ_FLAG_SCROLLABLE);

    // Tick marks and labels left of SOC bar
    const char *soc_ticks[] = {"100", "75", "50", "25", "0"};
    for (int i = 0; i < 5; i++) {
        lv_coord_t ty = (lv_coord_t)(BAR_TOP + i * (BAR_H / 4));
        lv_obj_t *tick = lv_obj_create(scr);
        lv_obj_set_pos(tick, SOC_BAR_X - 6, ty);
        lv_obj_set_size(tick, 5, 1);
        lv_obj_set_style_bg_color(tick, CLR_TEXT_DIM, 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tick, 0, 0);
        lv_obj_t *tl = make_label(scr, soc_ticks[i], CLR_TEXT_DIM,
                                   &lv_font_montserrat_10);
        lv_obj_align(tl, LV_ALIGN_TOP_RIGHT, -(LEFT_W + BAR_W + 14), ty - 4);
    }

    // Range above, SOC% below
    lbl_range = make_label(scr, "0 mi", CLR_TEXT_BRIGHT, &lv_font_montserrat_14);
    lv_obj_align(lbl_range, LV_ALIGN_TOP_LEFT, SOC_BAR_X - 10, BAR_TOP - 22);
    lbl_soc_pct = make_label(scr, "0%", CLR_CYAN, &lv_font_montserrat_18);
    lv_obj_align(lbl_soc_pct, LV_ALIGN_TOP_LEFT,
                 SOC_BAR_X - 10, BAR_TOP + BAR_H + 6);

    // ===========================================================
    //  SPEED CENTER
    // ===========================================================
    lv_obj_t *ctr = lv_obj_create(scr);
    lv_obj_set_pos(ctr, CTR_X, 0);
    lv_obj_set_size(ctr, CTR_W, MAIN_H);
    lv_obj_set_style_bg_color(ctr, CLR_BG, 0);
    lv_obj_set_style_border_width(ctr, 0, 0);
    lv_obj_set_style_radius(ctr, 0, 0);
    lv_obj_clear_flag(ctr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *spd_lbl = make_label(ctr, "SPEED", CLR_TEXT_DIM,
                                    &lv_font_montserrat_14);
    lv_obj_align(spd_lbl, LV_ALIGN_TOP_MID, 0, 20);

    lbl_speed_val = make_label(ctr, "0", CLR_WHITE, &lv_font_montserrat_48);
    lv_obj_align(lbl_speed_val, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *mph = make_label(ctr, UNITS_SPEED_LABEL, CLR_TEXT_MID, &lv_font_montserrat_18);
    lv_obj_align(mph, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *gdiv = lv_obj_create(ctr);
    lv_obj_set_size(gdiv, CTR_W - 60, 1);
    lv_obj_set_style_bg_color(gdiv, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(gdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gdiv, 0, 0);
    lv_obj_align(gdiv, LV_ALIGN_CENTER, 0, 65);

    lbl_gear = make_label(ctr, "P", CLR_CYAN, &lv_font_montserrat_48);
    lv_obj_align(lbl_gear, LV_ALIGN_BOTTOM_MID, 0, -90);

    const char *gnames[] = {"P","R","N","D","B"};
    for (int i = 0; i < 5; i++) {
        lbl_gear_btns[i] = make_label(ctr, gnames[i], CLR_TEXT_DIM,
                                       &lv_font_montserrat_18);
        lv_obj_align(lbl_gear_btns[i], LV_ALIGN_BOTTOM_MID,
                     -96 + i * 48, -48);
    }
    lv_obj_set_style_text_color(lbl_gear_btns[0], CLR_CYAN, 0);

    // ===========================================================
    //  POWER BAR
    // ===========================================================
    lv_obj_t *pwr_track = lv_obj_create(scr);
    lv_obj_set_pos(pwr_track, PWR_BAR_X, BAR_TOP);
    lv_obj_set_size(pwr_track, BAR_W, BAR_H);
    lv_obj_set_style_bg_color(pwr_track, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(pwr_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pwr_track, 0, 0);
    lv_obj_set_style_radius(pwr_track, 3, 0);
    lv_obj_clear_flag(pwr_track, LV_OBJ_FLAG_SCROLLABLE);

    bar_pwr = lv_obj_create(scr);
    lv_obj_set_pos(bar_pwr, PWR_BAR_X, BAR_TOP + BAR_H / 2);
    lv_obj_set_size(bar_pwr, BAR_W, 0);
    lv_obj_set_style_bg_color(bar_pwr, CLR_ORANGE, 0);
    lv_obj_set_style_bg_opa(bar_pwr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_pwr, 0, 0);
    lv_obj_set_style_radius(bar_pwr, 3, 0);
    lv_obj_clear_flag(bar_pwr, LV_OBJ_FLAG_SCROLLABLE);

    // Zero line
    lv_obj_t *zero_line = lv_obj_create(scr);
    lv_obj_set_pos(zero_line, PWR_BAR_X - 6, BAR_TOP + BAR_H / 2);
    lv_obj_set_size(zero_line, BAR_W + 6, 2);
    lv_obj_set_style_bg_color(zero_line, CLR_TEXT_MID, 0);
    lv_obj_set_style_bg_opa(zero_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(zero_line, 0, 0);
    lv_obj_clear_flag(zero_line, LV_OBJ_FLAG_SCROLLABLE);

    // Tick marks and labels right of power bar
    const char *pwr_ticks[] = {"+200", "+100", "0", "-100", "-200"};
    lv_color_t pwr_tick_cols[] = {CLR_AMBER, CLR_AMBER, CLR_TEXT_MID,
                                   CLR_GREEN, CLR_GREEN};
    for (int i = 0; i < 5; i++) {
        lv_coord_t ty = (lv_coord_t)(BAR_TOP + i * (BAR_H / 4));
        lv_obj_t *tl = make_label(scr, pwr_ticks[i], pwr_tick_cols[i],
                                   &lv_font_montserrat_10);
        lv_obj_align(tl, LV_ALIGN_TOP_LEFT,
                     PWR_BAR_X + BAR_W + 4, ty - 4);
    }

    // kW label above, value below
    make_label(scr, "kW", CLR_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_t *kw_lbl = lv_obj_get_child(scr, lv_obj_get_child_cnt(scr) - 1);
    lv_obj_align(kw_lbl, LV_ALIGN_TOP_LEFT, PWR_BAR_X + 2, BAR_TOP - 22);

    lbl_pwr_val = make_label(scr, "+0", CLR_ORANGE, &lv_font_montserrat_18);
    lv_obj_align(lbl_pwr_val, LV_ALIGN_TOP_LEFT,
                 PWR_BAR_X - 4, BAR_TOP + BAR_H + 6);

    // ===========================================================
    //  RIGHT SIDEBAR — Efficiency + FJ55
    // ===========================================================
    lv_obj_t *right = lv_obj_create(scr);
    lv_obj_set_pos(right, W - RIGHT_W, 0);
    lv_obj_set_size(right, RIGHT_W, MAIN_H);
    lv_obj_set_style_bg_color(right, CLR_PANEL, 0);
    lv_obj_set_style_border_color(right, CLR_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_border_side(right, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_pad_left(right, 16, 0);
    lv_obj_set_style_pad_top(right, 16, 0);
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    make_label(right, "EFFICIENCY", CLR_TEXT_DIM, &lv_font_montserrat_10);
    lv_obj_t *eff_title = lv_obj_get_child(right, 0);
    lv_obj_align(eff_title, LV_ALIGN_TOP_LEFT, 0, 0);

    make_label(right, UNITS_EFF_LABEL, CLR_TEXT_MID, &lv_font_montserrat_10);
    lv_obj_t *eff_sub = lv_obj_get_child(right, 1);
    lv_obj_align(eff_sub, LV_ALIGN_TOP_LEFT, 0, 20);

    lbl_efficiency = make_label(right, "--", CLR_TEXT_BRIGHT,
                                 &lv_font_montserrat_40);
    lv_obj_align(lbl_efficiency, LV_ALIGN_TOP_LEFT, 0, 36);

    make_label(right, "TRIP kWh", CLR_TEXT_MID, &lv_font_montserrat_10);
    lv_obj_t *trip_sub = lv_obj_get_child(right, 3);
    lv_obj_align(trip_sub, LV_ALIGN_TOP_LEFT, 0, 110);

    lbl_trip_kwh = make_label(right, "--", CLR_TEXT_BRIGHT,
                               &lv_font_montserrat_40);
    lv_obj_align(lbl_trip_kwh, LV_ALIGN_TOP_LEFT, 0, 126);

    // Divider
    lv_obj_t *rdiv = lv_obj_create(right);
    lv_obj_set_size(rdiv, RIGHT_W - 16, 1);
    lv_obj_set_style_bg_color(rdiv, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(rdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rdiv, 0, 0);
    lv_obj_align(rdiv, LV_ALIGN_TOP_LEFT, 0, 200);

    // FJ55 image
    evj55_splash_img_init();
    lv_obj_t *img = lv_image_create(right);
    lv_image_set_src(img, &evj55_splash_img);
    // Scale to fit RIGHT_W: 256 * RIGHT_W / SPLASH_EMBED_W
    lv_image_set_scale(img, (uint32_t)(256 * (RIGHT_W - 16)) / SPLASH_EMBED_W);
    lv_obj_align(img, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_obj_t *veh = make_label(right, "FJ55 EV", CLR_TEXT_DIM,
                                &lv_font_montserrat_14);
    lv_obj_align(veh, LV_ALIGN_BOTTOM_MID, 0, -12);

    // ===========================================================
    //  BOTTOM BAR
    // ===========================================================
    lv_obj_t *bot = lv_obj_create(scr);
    lv_obj_set_pos(bot, 0, MAIN_H);
    lv_obj_set_size(bot, W, BOT_H);
    lv_obj_set_style_bg_color(bot, lv_color_hex(0x050709), 0);
    lv_obj_set_style_border_color(bot, CLR_BORDER, 0);
    lv_obj_set_style_border_width(bot, 1, 0);
    lv_obj_set_style_border_side(bot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(bot, 0, 0);
    lv_obj_clear_flag(bot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *can_lbl = make_label(bot, "CAN 500 kbps", CLR_TEXT_DIM,
                                    &lv_font_montserrat_10);
    lv_obj_align(can_lbl, LV_ALIGN_LEFT_MID, 24, 0);

    lv_obj_t *hw_lbl = make_label(bot, "ESP32-P4  |  M5Stack Tab5",
                                   CLR_TEXT_DIM, &lv_font_montserrat_10);
    lv_obj_align(hw_lbl, LV_ALIGN_CENTER, 0, 0);

    dot_can = lv_obj_create(bot);
    lv_obj_set_size(dot_can, 10, 10);
    lv_obj_set_style_radius(dot_can, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_can, CLR_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(dot_can, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_can, 0, 0);
    lv_obj_align(dot_can, LV_ALIGN_RIGHT_MID, -16, 0);
}

// =============================================================
//  dashboard_ui_update
// =============================================================
void dashboard_ui_update(const DashData *d)
{
    char buf[32];

    // SOC bar — fills from bottom
    float soc_pct = d->soc_pct / 100.0f;
    lv_color_t soc_col = soc_pct < 0.15f ? CLR_RED :
                         soc_pct < 0.30f ? CLR_AMBER : CLR_CYAN;
    set_bar(bar_soc, soc_pct,
            LEFT_W + 8, BAR_TOP, BAR_H, BAR_W, soc_col);
    snprintf(buf, sizeof(buf), "%d%%", (int)d->soc_pct);
    lv_label_set_text(lbl_soc_pct, buf);
    lv_obj_set_style_text_color(lbl_soc_pct, soc_col, 0);
    snprintf(buf, sizeof(buf), "%.0f %s", DIST_TO_DISPLAY(d->range_dist), UNITS_DIST_LABEL);
    lv_label_set_text(lbl_range, buf);

    // Speed
    snprintf(buf, sizeof(buf), "%d", (int)SPEED_TO_DISPLAY(d->speed));
    lv_label_set_text(lbl_speed_val, buf);

    // Gear
    const char *gnames[] = {"P","R","N","D","B"};
    int g = d->gear < 5 ? d->gear : 0;
    lv_label_set_text(lbl_gear, gnames[g]);
    for (int i = 0; i < 5; i++)
        lv_obj_set_style_text_color(lbl_gear_btns[i],
            i == g ? CLR_CYAN : CLR_TEXT_DIM, 0);

    // Power bar — zero is center, drive fills up (orange), regen fills down (green)
    float kw     = d->power_kw;
    float kw_pct = kw / PWR_FULL_SCALE;  // -1.0 .. +1.0
    lv_coord_t half = BAR_H / 2;
    lv_coord_t zero_y = BAR_TOP + half;
    lv_color_t pwr_col;

    if (kw >= 0) {
        // Drive: fill upward from zero
        pwr_col = kw > PWR_FULL_SCALE * 0.75f ? CLR_RED :
                  kw > PWR_FULL_SCALE * 0.50f ? CLR_AMBER : CLR_ORANGE;
        lv_coord_t fill_h = (lv_coord_t)(kw_pct * half);
        if (fill_h > half) fill_h = half;
        lv_obj_set_pos(bar_pwr,  (int)(LCD_H_RES - RIGHT_W - BAR_W - 8), zero_y - fill_h);
        lv_obj_set_size(bar_pwr, BAR_W, fill_h);
    } else {
        // Regen: fill downward from zero
        pwr_col = CLR_GREEN;
        lv_coord_t fill_h = (lv_coord_t)(-kw_pct * half);
        if (fill_h > half) fill_h = half;
        lv_obj_set_pos(bar_pwr,  (int)(LCD_H_RES - RIGHT_W - BAR_W - 8), zero_y);
        lv_obj_set_size(bar_pwr, BAR_W, fill_h);
    }
    lv_obj_set_style_bg_color(bar_pwr, pwr_col, 0);

    snprintf(buf, sizeof(buf), "%+d", (int)kw);
    lv_label_set_text(lbl_pwr_val, buf);
    lv_obj_set_style_text_color(lbl_pwr_val, pwr_col, 0);

    // Thermal
    auto uptemp = [](lv_obj_t *lbl, float v, float warn, float crit) {
        char b[12];
        snprintf(b, sizeof(b), "%.0f°C", v);
        lv_label_set_text(lbl, b);
        lv_obj_set_style_text_color(lbl,
            v >= crit ? CLR_RED : v >= warn ? CLR_AMBER : CLR_GREEN, 0);
    };
    uptemp(lbl_inv_temp,  d->inverter_temp_c, WARN_INV_TEMP_C,   CRIT_INV_TEMP_C);
    uptemp(lbl_mot_temp,  d->motor_temp_c,    WARN_MOTOR_TEMP_C, CRIT_MOTOR_TEMP_C);
    uptemp(lbl_batt_temp, d->batt_temp_c,     WARN_BATT_TEMP_C,  CRIT_BATT_TEMP_C);

    snprintf(buf, sizeof(buf), "%.1f V", d->pack_volts);
    lv_label_set_text(lbl_pack_v, buf);
    snprintf(buf, sizeof(buf), "%.0f A", d->pack_amps);
    lv_label_set_text(lbl_pack_a, buf);
    snprintf(buf, sizeof(buf), "%.1f V", d->aux_volts);
    lv_label_set_text(lbl_aux_v, buf);

    // Efficiency (placeholder — needs trip energy tracking)
    if (d->speed > 2.0f && d->power_kw > 0) {
        // Very rough instantaneous efficiency
        float eff = EFF_TO_DISPLAY(d->speed / d->power_kw);
        snprintf(buf, sizeof(buf), "%.1f", eff);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(lbl_efficiency, buf);
    lv_label_set_text(lbl_trip_kwh, "--");  // TODO: trip energy accumulator

    // CAN dot
    lv_obj_set_style_bg_color(dot_can, CLR_GREEN, 0);
}

#endif // DASHBOARD_UI_IMPL
