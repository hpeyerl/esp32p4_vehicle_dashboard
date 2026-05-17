// =============================================================
//  dashboard_ui.cpp — EV Dashboard LVGL UI Implementation
//
//  Layout (landscape 1280×720, SW-rotated to portrait for Tab5):
//
//  ┌──────────────┬──┬──────────────────┬──┬──────────────┐
//  │  THERMAL     │S │                  │P │  EFFICIENCY  │
//  │  inv/mot/bat │O │    72  km/h      │W │  + FJ55 img  │
//  │  PackV/A/12V │C │    D  PRND       │R │              │
//  ├──────────────┴──┴──────────────────┴──┴──────────────┤
//  │  CAN 500kbps          M5Stack Tab5              ● CAN │
//  └──────────────────────────────────────────────────────┘
//
//  SOC bar:   cyan(≥50%) → orange(21-49%) → red(0-20%), fills bottom→top
//  Power bar: orange(+kW drive) / green(-kW regen), zero at center
// =============================================================

#include "dashboard_ui.h"
#include "can_signals.h"
#include "units.h"
#include "evj55_splash_c.h"

#include "lvgl.h"
#include <string.h>
#include <stdio.h>

// ── Colour palette ────────────────────────────────────────────────────────
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

// ── Layout constants ──────────────────────────────────────────────────────
#define LEFT_W   260    // thermal sidebar width (px)
#define RIGHT_W  240    // efficiency/image sidebar width (px)
#define BAR_W     48    // bar graph stroke width (px)
#define BAR_H    580    // bar graph height
#define BAR_TOP   50    // bar top y offset — room for labels above
#define BOT_H      0    // status bar removed

// Power bar full-scale (kW each direction)
#define PWR_FULL_SCALE  200.0f

// ── Widget handles ────────────────────────────────────────────────────────
static lv_obj_t *s_bar_soc        = NULL;
static lv_obj_t *s_lbl_soc_pct    = NULL;
static lv_obj_t *s_lbl_range      = NULL;
static lv_obj_t *s_bar_pwr        = NULL;
static lv_obj_t *s_lbl_pwr_val    = NULL;
static lv_obj_t *s_lbl_speed      = NULL;
static lv_obj_t *s_lbl_gear       = NULL;
static lv_obj_t *s_lbl_prnd[5];
static lv_obj_t *s_lbl_inv_temp   = NULL;
static lv_obj_t *s_lbl_mot_temp   = NULL;
static lv_obj_t *s_lbl_batt_temp  = NULL;
static lv_obj_t *s_lbl_pack_v     = NULL;
static lv_obj_t *s_lbl_pack_a     = NULL;
static lv_obj_t *s_lbl_aux_v      = NULL;
static lv_obj_t *s_lbl_efficiency = NULL;
static lv_obj_t *s_dot_can        = NULL;

// Cached bar positions (set at create time, used at update time)
static lv_coord_t s_soc_bar_x = 0;
static lv_coord_t s_pwr_bar_x = 0;

// ── Helpers ───────────────────────────────────────────────────────────────
static lv_obj_t *make_label(lv_obj_t *parent, const char *txt,
                              lv_color_t col, const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, col, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

static lv_obj_t *make_panel(lv_obj_t *parent,
                              lv_coord_t x, lv_coord_t y,
                              lv_coord_t w, lv_coord_t h,
                              lv_color_t bg, lv_border_side_t border_side)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, CLR_BORDER, 0);
    lv_obj_set_style_border_width(p, border_side ? 1 : 0, 0);
    lv_obj_set_style_border_side(p, border_side, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    return p;
}

static lv_obj_t *make_bar_track(lv_obj_t *parent,
                                  lv_coord_t x, lv_coord_t y,
                                  lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_set_pos(t, x, y);
    lv_obj_set_size(t, w, h);
    lv_obj_set_style_bg_color(t, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_radius(t, 3, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    // Fill rect (zero size initially, updated each frame)
    lv_obj_t *f = lv_obj_create(parent);
    lv_obj_set_pos(f, x, y + h);
    lv_obj_set_size(f, w, 0);
    lv_obj_set_style_bg_color(f, CLR_CYAN, 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(f, 0, 0);
    lv_obj_set_style_radius(f, 3, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    return f;  // caller keeps fill handle
}

static lv_obj_t *make_divider(lv_obj_t *parent, lv_coord_t w)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, w, 1);
    lv_obj_set_style_bg_color(d, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    return d;
}

// ── dashboard_ui_create ───────────────────────────────────────────────────
void dashboard_ui_create(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    // Allow children to render outside panel bounds (labels near edges)
    lv_obj_add_flag(scr, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    const lv_coord_t W      = LCD_H_RES;
    const lv_coord_t H      = LCD_V_RES;
    const lv_coord_t MAIN_H = H;   // full height, no status bar

    s_soc_bar_x = LEFT_W + 40;
    s_pwr_bar_x = W - RIGHT_W - BAR_W - 40;
    const lv_coord_t CTR_X = s_soc_bar_x + BAR_W + 12;
    const lv_coord_t CTR_W = s_pwr_bar_x - CTR_X;

    // ── Left sidebar ─────────────────────────────────────────────────────
    lv_obj_t *left = make_panel(scr, 0, 0, LEFT_W, MAIN_H,
                                 CLR_PANEL, LV_BORDER_SIDE_RIGHT);
    lv_obj_set_style_pad_left(left, 16, 0);
    lv_obj_set_style_pad_top(left, 16, 0);

    struct { lv_obj_t **lbl; const char *tag; } temps[] = {
        {&s_lbl_inv_temp,  "INVERTER"},
        {&s_lbl_mot_temp,  "MOTOR"},
        {&s_lbl_batt_temp, "BATTERY"},
    };
    for (int i = 0; i < 3; i++) {
        lv_coord_t y = (lv_coord_t)(i * 170);
        lv_obj_t *tag = make_label(left, temps[i].tag, CLR_TEXT_MID,
                                    &lv_font_montserrat_10);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 0, y);
        *temps[i].lbl = make_label(left, "0°C", CLR_GREEN,
                                    &lv_font_montserrat_40);
        lv_obj_align(*temps[i].lbl, LV_ALIGN_TOP_LEFT, 0, y + 16);
    }

    lv_obj_t *ld = make_divider(left, LEFT_W - 16);
    lv_obj_align(ld, LV_ALIGN_TOP_LEFT, 0, 520);

    struct { lv_obj_t **lbl; const char *tag; } elec[] = {
        {&s_lbl_pack_v, "PACK VOLTAGE"},
        {&s_lbl_pack_a, "PACK AMPS"},
        {&s_lbl_aux_v,  "12V AUX"},
    };
    for (int i = 0; i < 3; i++) {
        lv_coord_t y = (lv_coord_t)(530 + i * 60);
        lv_obj_t *tag = make_label(left, elec[i].tag, CLR_TEXT_MID,
                                    &lv_font_montserrat_10);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 0, y);
        *elec[i].lbl = make_label(left, "—", CLR_TEXT_BRIGHT,
                                   &lv_font_montserrat_24);
        lv_obj_align(*elec[i].lbl, LV_ALIGN_TOP_LEFT, 0, y + 14);
    }

    // ── SOC bar ───────────────────────────────────────────────────────────
    s_bar_soc = make_bar_track(scr, s_soc_bar_x, BAR_TOP, BAR_W, BAR_H);

    const char *soc_ticks[] = {"100", "75", "50", "25", "0"};
    for (int i = 0; i < 5; i++) {
        lv_coord_t ty = (lv_coord_t)(BAR_TOP + i * (BAR_H / 4));
        lv_obj_t *tl = make_label(scr, soc_ticks[i], CLR_TEXT_DIM,
                                   &lv_font_montserrat_10);
        lv_obj_align(tl, LV_ALIGN_TOP_RIGHT,
                     -(W - s_soc_bar_x + 4), ty - 4);
    }

    s_lbl_range = make_label(scr, "— " UNITS_DIST_LABEL, CLR_TEXT_BRIGHT,
                              &lv_font_montserrat_14);
    lv_obj_align(s_lbl_range, LV_ALIGN_TOP_LEFT,
                 s_soc_bar_x - 10, BAR_TOP - 22);

    s_lbl_soc_pct = make_label(scr, "0%", CLR_CYAN, &lv_font_montserrat_18);
    lv_obj_align(s_lbl_soc_pct, LV_ALIGN_TOP_LEFT,
                 s_soc_bar_x - 10, BAR_TOP + BAR_H + 6);

    // ── Center ────────────────────────────────────────────────────────────
    lv_obj_t *ctr = make_panel(scr, CTR_X, 0, CTR_W, MAIN_H,
                                CLR_BG, LV_BORDER_SIDE_NONE);

    lv_obj_t *spd_lbl = make_label(ctr, "SPEED", CLR_TEXT_DIM,
                                    &lv_font_montserrat_14);
    lv_obj_align(spd_lbl, LV_ALIGN_TOP_MID, 0, 20);

    s_lbl_speed = make_label(ctr, "0", CLR_WHITE, &lv_font_montserrat_48);
    lv_obj_align(s_lbl_speed, LV_ALIGN_CENTER, 0, -80);

    lv_obj_t *unit_lbl = make_label(ctr, UNITS_SPEED_LABEL, CLR_TEXT_MID,
                                     &lv_font_montserrat_18);
    lv_obj_align(unit_lbl, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *gdiv = make_divider(ctr, CTR_W - 60);
    lv_obj_align(gdiv, LV_ALIGN_CENTER, 0, 120);

    s_lbl_gear = make_label(ctr, "P", CLR_CYAN, &lv_font_montserrat_48);
    lv_obj_align(s_lbl_gear, LV_ALIGN_BOTTOM_MID, 0, -72);

    const char *gnames[] = {"P", "R", "N", "D", "B"};
    for (int i = 0; i < 5; i++) {
        s_lbl_prnd[i] = make_label(ctr, gnames[i], CLR_TEXT_DIM,
                                    &lv_font_montserrat_18);
        lv_obj_align(s_lbl_prnd[i], LV_ALIGN_BOTTOM_MID,
                     -96 + i * 48, -36);
    }
    lv_obj_set_style_text_color(s_lbl_prnd[0], CLR_CYAN, 0); // P default

    // ── Power bar ─────────────────────────────────────────────────────────
    s_bar_pwr = make_bar_track(scr, s_pwr_bar_x, BAR_TOP, BAR_W, BAR_H);

    // Zero centre line
    lv_obj_t *zero_line = lv_obj_create(scr);
    lv_obj_set_pos(zero_line, s_pwr_bar_x - 6, BAR_TOP + BAR_H / 2);
    lv_obj_set_size(zero_line, BAR_W + 6, 2);
    lv_obj_set_style_bg_color(zero_line, CLR_TEXT_MID, 0);
    lv_obj_set_style_bg_opa(zero_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(zero_line, 0, 0);
    lv_obj_clear_flag(zero_line, LV_OBJ_FLAG_SCROLLABLE);

    // Power bar tick labels removed — insufficient space
    lv_obj_t *kw_lbl = make_label(scr, "kW", CLR_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_align(kw_lbl, LV_ALIGN_TOP_LEFT, s_pwr_bar_x + 2, BAR_TOP - 22);

    s_lbl_pwr_val = make_label(scr, "+0", CLR_ORANGE, &lv_font_montserrat_18);
    lv_obj_align(s_lbl_pwr_val, LV_ALIGN_TOP_LEFT,
                 s_pwr_bar_x - 4, BAR_TOP + BAR_H + 6);

    // ── Right sidebar ─────────────────────────────────────────────────────
    lv_obj_t *right = make_panel(scr, W - RIGHT_W, 0, RIGHT_W, MAIN_H,
                                  CLR_PANEL, LV_BORDER_SIDE_LEFT);
    lv_obj_set_style_pad_left(right, 16, 0);
    lv_obj_set_style_pad_top(right, 16, 0);

    make_label(right, "EFFICIENCY", CLR_TEXT_DIM, &lv_font_montserrat_10);
    lv_obj_t *eff_t = lv_obj_get_child(right, 0);
    lv_obj_align(eff_t, LV_ALIGN_TOP_LEFT, 0, 0);

    make_label(right, UNITS_EFF_LABEL, CLR_TEXT_MID, &lv_font_montserrat_10);
    lv_obj_t *eff_u = lv_obj_get_child(right, 1);
    lv_obj_align(eff_u, LV_ALIGN_TOP_LEFT, 0, 18);

    s_lbl_efficiency = make_label(right, "—", CLR_TEXT_BRIGHT,
                                   &lv_font_montserrat_40);
    lv_obj_align(s_lbl_efficiency, LV_ALIGN_TOP_LEFT, 0, 34);

    lv_obj_t *rdiv = make_divider(right, RIGHT_W - 16);
    lv_obj_align(rdiv, LV_ALIGN_TOP_LEFT, 0, 110);

    // FJ55 image
    evj55_splash_img_init();
    lv_obj_t *img = lv_image_create(right);
    lv_image_set_src(img, &evj55_splash_img);
    lv_image_set_scale(img, (uint32_t)(256 * (RIGHT_W - 16)) / SPLASH_EMBED_W);
    lv_obj_align(img, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_t *veh = make_label(right, "FJ55 EV", CLR_TEXT_DIM,
                                &lv_font_montserrat_14);
    lv_obj_align(veh, LV_ALIGN_BOTTOM_MID, 0, -10);

    // ── CAN status dot — top-right corner ────────────────────────────────
    s_dot_can = lv_obj_create(scr);
    lv_obj_set_size(s_dot_can, 12, 12);
    lv_obj_set_style_radius(s_dot_can, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot_can, CLR_GREEN, 0);
    lv_obj_set_style_bg_opa(s_dot_can, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dot_can, 0, 0);
    lv_obj_align(s_dot_can, LV_ALIGN_TOP_RIGHT, -12, 12);
}

// ── dashboard_ui_update ───────────────────────────────────────────────────
// lv_label_set_text / lv_obj_set_pos / lv_obj_set_size always called —
// marks widgets dirty so LVGL re-renders every frame.
// lv_obj_set_style_* cached — each call allocates on the LVGL heap.
void dashboard_ui_update(const DashData *d)
{
    char buf[32];

    // -- SOC bar --
    static lv_color_t last_soc_col = {0};
    float soc_f = d->soc_pct / 100.0f;
    lv_color_t soc_col = soc_f >= 0.50f ? CLR_CYAN :
                         soc_f >= 0.21f ? CLR_AMBER : CLR_RED;
    lv_coord_t soc_fill = (lv_coord_t)(soc_f * BAR_H);
    if (soc_fill > BAR_H) soc_fill = BAR_H;
    lv_obj_set_pos(s_bar_soc, s_soc_bar_x, BAR_TOP + BAR_H - soc_fill);
    lv_obj_set_size(s_bar_soc, BAR_W, soc_fill);
    if (memcmp(&soc_col, &last_soc_col, sizeof(lv_color_t)) != 0) {
        lv_obj_set_style_bg_color(s_bar_soc, soc_col, 0);
        lv_obj_set_style_text_color(s_lbl_soc_pct, soc_col, 0);
        last_soc_col = soc_col;
    }
    snprintf(buf, sizeof(buf), "%d%%", (int)d->soc_pct);
    lv_label_set_text(s_lbl_soc_pct, buf);
    snprintf(buf, sizeof(buf), "%.0f %s",
             DIST_TO_DISPLAY(d->range_dist), UNITS_DIST_LABEL);
    lv_label_set_text(s_lbl_range, buf);

    // -- Speed --
    snprintf(buf, sizeof(buf), "%d", (int)SPEED_TO_DISPLAY(d->speed));
    lv_label_set_text(s_lbl_speed, buf);

    // -- Gear / PRND --
    static int last_gear = -1;
    const char *gnames[] = {"P", "R", "N", "D", "B"};
    int g = (d->gear >= 0 && d->gear < 5) ? d->gear : 0;
    lv_label_set_text(s_lbl_gear, gnames[g]);
    if (g != last_gear) {
        for (int i = 0; i < 5; i++)
            lv_obj_set_style_text_color(s_lbl_prnd[i],
                i == g ? CLR_CYAN : CLR_TEXT_DIM, 0);
        last_gear = g;
    }

    // -- Power bar --
    static lv_color_t last_pwr_col = {0};
    float kw      = d->power_kw;
    float kw_frac = kw / PWR_FULL_SCALE;
    lv_coord_t half   = BAR_H / 2;
    lv_coord_t zero_y = BAR_TOP + half;
    lv_color_t pwr_col;
    if (kw >= 0.0f) {
        pwr_col = CLR_ORANGE;
        lv_coord_t fh = (lv_coord_t)(kw_frac * half);
        if (fh > half) fh = half;
        lv_obj_set_pos(s_bar_pwr, s_pwr_bar_x, zero_y - fh);
        lv_obj_set_size(s_bar_pwr, BAR_W, fh);
    } else {
        pwr_col = CLR_GREEN;
        lv_coord_t fh = (lv_coord_t)(-kw_frac * half);
        if (fh > half) fh = half;
        lv_obj_set_pos(s_bar_pwr, s_pwr_bar_x, zero_y);
        lv_obj_set_size(s_bar_pwr, BAR_W, fh);
    }
    if (memcmp(&pwr_col, &last_pwr_col, sizeof(lv_color_t)) != 0) {
        lv_obj_set_style_bg_color(s_bar_pwr, pwr_col, 0);
        lv_obj_set_style_text_color(s_lbl_pwr_val, pwr_col, 0);
        last_pwr_col = pwr_col;
    }
    snprintf(buf, sizeof(buf), "%+d", (int)kw);
    lv_label_set_text(s_lbl_pwr_val, buf);

    // -- Thermals --
    static lv_color_t last_inv_col = {0}, last_mot_col = {0}, last_batt_col = {0};
    auto uptemp = [](lv_obj_t *lbl, float v, float warn, float crit,
                     lv_color_t *last_col) {
        char b[12];
        snprintf(b, sizeof(b), "%.0f C", v);
        lv_label_set_text(lbl, b);
        lv_color_t nc = v >= crit ? CLR_RED : v >= warn ? CLR_AMBER : CLR_GREEN;
        if (memcmp(&nc, last_col, sizeof(lv_color_t)) != 0) {
            lv_obj_set_style_text_color(lbl, nc, 0);
            *last_col = nc;
        }
    };
    uptemp(s_lbl_inv_temp,  d->inverter_temp_c, WARN_INV_TEMP_C,   CRIT_INV_TEMP_C,  &last_inv_col);
    uptemp(s_lbl_mot_temp,  d->motor_temp_c,    WARN_MOTOR_TEMP_C, CRIT_MOTOR_TEMP_C, &last_mot_col);
    uptemp(s_lbl_batt_temp, d->batt_temp_c,     WARN_BATT_TEMP_C,  CRIT_BATT_TEMP_C,  &last_batt_col);

    // -- Electrical --
    snprintf(buf, sizeof(buf), "%.1f V", d->pack_volts);
    lv_label_set_text(s_lbl_pack_v, buf);
    snprintf(buf, sizeof(buf), "%.0f A", d->pack_amps);
    lv_label_set_text(s_lbl_pack_a, buf);
    snprintf(buf, sizeof(buf), "%.1f V", d->aux_volts);
    lv_label_set_text(s_lbl_aux_v, buf);

    // -- Efficiency --
    if (d->speed > 2.0f && d->power_kw > 5.0f) {
        snprintf(buf, sizeof(buf), "%.1f", EFF_TO_DISPLAY(d->speed / d->power_kw));
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_lbl_efficiency, buf);

    // Explicitly invalidate screen to ensure LVGL schedules a redraw.
    // lv_obj_set_pos/size should mark dirty automatically but this is
    // a safety net for LVGL 9 RENDER_MODE_FULL behaviour.
    lv_obj_invalidate(lv_screen_active());
}
