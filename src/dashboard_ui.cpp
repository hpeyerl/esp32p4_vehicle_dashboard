// =============================================================
//  dashboard_ui.cpp — EV Dashboard LVGL UI  v3
//
//  Layout (landscape 1280×720):
//
//  ┌──────────────┬──┬──────────────────────┬──┬────────────┐
//  │ 5× half-arc  │S │                      │P │ Efficiency │
//  │ meter gauges │O │    157  km/h          │W │ Trip kWh  │
//  │ inv/mot/bat  │C │    D  PRND            │R │ Range     │
//  │ pack V/A     │  │                      │  │ 12V aux   │
//  ├──────────────┴──┴──────────────────────┴──┴────────────┤
//  │  CAN 500kbps            ESP32-P4 | M5Stack Tab5    ●   │
//  └──────────────────────────────────────────────────────────┘
//
//  Left/right panels have large inner border-radius for bracket effect.
//  Meter gauges: half-circle arc + colored zone arcs + needle line.
//  SOC bar: vertical, fills bottom→top. cyan≥50% amber 21-49% red≤20%.
//  Power bar: vertical, zero center. orange=drive, green=regen.
// =============================================================

#include "dashboard_ui.h"
#include "can_signals.h"
#include "units.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>
#include <cstring>

LV_FONT_DECLARE(lv_font_montserrat_72)

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

// ── Layout ────────────────────────────────────────────────────────────────
#define LEFT_W    500    // left panel width
#define RIGHT_W   280    // right panel width
#define BAR_W      22    // bar width
#define BAR_H     400    // bar height
#define BAR_TOP    50    // bar top y
#define BOT_H      52    // bottom bar height
#define PWR_FULL   200.0f

// ── Meter gauge geometry ──────────────────────────────────────────────────
// Half-circle, flat side down, opens upward.
// LVGL arc: angle 0=3-o'clock, goes CW.
// For flat-bottom half circle: bg_start=180, bg_end=360 (=0).
// Needle: line from center outward at angle corresponding to value.
#define METER_R    56    // arc radius
#define METER_W    10    // arc stroke width
#define METER_GAP  90    // vertical spacing between meter centers

// ── Widget handles ────────────────────────────────────────────────────────
// Meter: arc indicator + needle line + value label
typedef struct {
    lv_obj_t *arc_green;
    lv_obj_t *arc_yellow;
    lv_obj_t *arc_red;
    lv_obj_t *needle;
    lv_obj_t *lbl_val;
} Meter;

static Meter s_inv_meter;
static Meter s_mot_meter;
static Meter s_bat_meter;
static Meter s_pv_meter;
static Meter s_pa_meter;

static lv_obj_t *s_bar_soc     = NULL;
static lv_obj_t *s_lbl_soc_pct = NULL;
static lv_obj_t *s_lbl_range   = NULL;
static lv_obj_t *s_bar_pwr     = NULL;
static lv_obj_t *s_lbl_pwr_val = NULL;
static lv_obj_t *s_lbl_speed   = NULL;
static lv_obj_t *s_lbl_gear    = NULL;
static lv_obj_t *s_lbl_prnd[5];
static lv_obj_t *s_lbl_eff     = NULL;
static lv_obj_t *s_lbl_trip    = NULL;
static lv_obj_t *s_lbl_aux_v   = NULL;
static lv_obj_t *s_dot_can     = NULL;

static lv_coord_t s_soc_bar_x  = 0;
static lv_coord_t s_pwr_bar_x  = 0;

// ── Helpers ───────────────────────────────────────────────────────────────
static lv_obj_t *make_label(lv_obj_t *parent, const char *txt,
                              lv_color_t col, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static lv_obj_t *make_rect(lv_obj_t *parent,
                             lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, lv_coord_t h,
                             lv_color_t color, lv_coord_t radius)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_bg_color(r, color, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, radius, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

// Create one meter gauge.
// parent: container object
// cx, cy: center of the half-circle (flat side at cy, arc opens upward)
// green_end_deg, yellow_end_deg: arc zone boundaries (0-180°, left=0 right=180)
// label: short tag string
static void make_meter(Meter *m, lv_obj_t *parent,
                        lv_coord_t cx, lv_coord_t cy,
                        int16_t green_end, int16_t yellow_end,
                        const char *tag)
{
    lv_coord_t sz = METER_R * 2;
    lv_coord_t ax = cx - METER_R;
    lv_coord_t ay = cy - METER_R;

    // Background arc (full 180° sweep, flat bottom)
    // In LVGL arc: 0=right, 90=bottom, 180=left, 270=top
    // Half circle flat-bottom = bg from 180 to 360(=0) going CW
    // But we want arc that goes from LEFT to RIGHT across the top.
    // bg_start=180 (left), bg_end=360(=0) (right) → 180° CW sweep
    lv_obj_t *bg = lv_arc_create(parent);
    lv_obj_set_pos(bg, ax, ay);
    lv_obj_set_size(bg, sz, sz);
    lv_arc_set_bg_angles(bg, 180, 360);
    lv_arc_set_angles(bg, 180, 180);
    lv_obj_set_style_arc_color(bg, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(bg, CLR_BORDER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(bg, METER_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bg, METER_W, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bg, LV_OPA_TRANSP, 0);
    lv_obj_remove_style(bg, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    // Green zone arc (from 180° toward right by green_end degrees)
    int16_t g_end = 180 + green_end;
    lv_obj_t *ag = lv_arc_create(parent);
    lv_obj_set_pos(ag, ax, ay);
    lv_obj_set_size(ag, sz, sz);
    lv_arc_set_bg_angles(ag, 180, g_end);
    lv_arc_set_angles(ag, 180, g_end);
    lv_obj_set_style_arc_color(ag, CLR_GREEN, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ag, CLR_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ag, METER_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ag, METER_W, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ag, LV_OPA_TRANSP, 0);
    lv_obj_remove_style(ag, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ag, LV_OBJ_FLAG_CLICKABLE);
    m->arc_green = ag;

    // Yellow zone
    int16_t y_end = 180 + yellow_end;
    lv_obj_t *ay2 = lv_arc_create(parent);
    lv_obj_set_pos(ay2, ax, ay);
    lv_obj_set_size(ay2, sz, sz);
    lv_arc_set_bg_angles(ay2, g_end, y_end);
    lv_arc_set_angles(ay2, g_end, y_end);
    lv_obj_set_style_arc_color(ay2, CLR_AMBER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ay2, CLR_AMBER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ay2, METER_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ay2, METER_W, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ay2, LV_OPA_TRANSP, 0);
    lv_obj_remove_style(ay2, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ay2, LV_OBJ_FLAG_CLICKABLE);
    m->arc_yellow = ay2;

    // Red zone (from yellow_end to 360)
    lv_obj_t *ar = lv_arc_create(parent);
    lv_obj_set_pos(ar, ax, ay);
    lv_obj_set_size(ar, sz, sz);
    lv_arc_set_bg_angles(ar, y_end, 360);
    lv_arc_set_angles(ar, y_end, 360);
    lv_obj_set_style_arc_color(ar, CLR_RED, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ar, CLR_RED, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ar, METER_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ar, METER_W, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ar, LV_OPA_TRANSP, 0);
    lv_obj_remove_style(ar, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ar, LV_OBJ_FLAG_CLICKABLE);
    m->arc_red = ar;

    // Needle: thin line widget rotated by transform
    // We use a narrow rect rotated via style transform
    lv_obj_t *nd = lv_obj_create(parent);
    lv_obj_set_size(nd, 2, METER_R - 6);
    lv_obj_set_style_bg_color(nd, CLR_WHITE, 0);
    lv_obj_set_style_bg_opa(nd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nd, 0, 0);
    lv_obj_set_style_radius(nd, 1, 0);
    // Position at center; rotation via transform_angle
    lv_obj_set_pos(nd, cx - 1, cy - METER_R + 6);
    lv_obj_set_style_transform_pivot_x(nd, 1, 0);
    lv_obj_set_style_transform_pivot_y(nd, METER_R - 6, 0);
    lv_obj_set_style_transform_angle(nd, 0, 0);
    lv_obj_clear_flag(nd, LV_OBJ_FLAG_SCROLLABLE);
    m->needle = nd;

    // Center dot
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_pos(dot, cx - 3, cy - 3);
    lv_obj_set_style_bg_color(dot, CLR_WHITE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    // Tag label
    lv_obj_t *tl = make_label(parent, tag, CLR_TEXT_DIM, &lv_font_montserrat_10);
    lv_obj_set_pos(tl, cx - 14, cy + 4);

    // Value label
    m->lbl_val = make_label(parent, "--", CLR_GREEN, &lv_font_montserrat_14);
    lv_obj_set_pos(m->lbl_val, cx - 20, cy + 16);
}

// Update needle angle for a meter.
// pct: 0.0 (left/min) to 1.0 (right/max)
// Needle at pct=0 points left (270° in LVGL = pointing up when flat-bottom arc)
// Actually: flat-bottom arc left edge = 180°, right edge = 360°
// pct=0 → angle 270° (pointing straight up-left)
// pct=0.5 → angle 270° (pointing straight up)  
// Wait — let's think in terms of the needle rotation:
// Arc goes from 180° (left) to 360°(right) = 180° sweep
// pct=0 → needle points at 180° = left side = angle from vertical = -90°
// pct=1 → needle points at 360° = right side = angle from vertical = +90°
// pct=0.5 → needle points at 270° = straight up = angle 0°
// transform_angle in LVGL is in tenths of degrees, 0=pointing up
// At pct: needle_angle = (pct - 0.5) * 180 degrees
static void update_needle(Meter *m, float pct)
{
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
    int16_t angle_deg = (int16_t)((pct - 0.5f) * 180.0f);
    lv_obj_set_style_transform_angle(m->needle, angle_deg * 10, 0);
    lv_obj_invalidate(m->needle);
}

// ── dashboard_ui_create ───────────────────────────────────────────────────
void dashboard_ui_create(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    const lv_coord_t W      = LCD_H_RES;
    const lv_coord_t H      = LCD_V_RES;
    const lv_coord_t MAIN_H = H - BOT_H;

    s_soc_bar_x = LEFT_W;
    s_pwr_bar_x = W - RIGHT_W - BAR_W;
    const lv_coord_t CTR_X  = s_soc_bar_x + BAR_W + 10;
    const lv_coord_t CTR_W  = s_pwr_bar_x - CTR_X;

    // ── Left panel — bracket shape via radius on inner-top and inner-bottom corners
    lv_obj_t *left = lv_obj_create(scr);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_size(left, LEFT_W, MAIN_H);
    lv_obj_set_style_bg_color(left, CLR_PANEL, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(left, CLR_BORDER, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(left, 0, 0);
    // Bracket curve: large radius on right side
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(left, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // Meters are children of left panel — coords relative to left panel
    lv_coord_t meter_cx = LEFT_W / 2;
    lv_coord_t meter_spacing = (MAIN_H - 20) / 5;
    lv_coord_t cy0 = meter_spacing / 2;

    make_meter(&s_inv_meter, left, meter_cx, cy0 + 0*meter_spacing,
               126, 153, "INV");
    make_meter(&s_mot_meter, left, meter_cx, cy0 + 1*meter_spacing,
               126, 153, "MOT");
    make_meter(&s_bat_meter, left, meter_cx, cy0 + 2*meter_spacing,
               126, 153, "BATT");
    make_meter(&s_pv_meter,  left, meter_cx, cy0 + 3*meter_spacing,
               180, 180, "PACK V");
    make_meter(&s_pa_meter,  left, meter_cx, cy0 + 4*meter_spacing,
               90, 90, "PACK A");

    // Override pack V arc to cyan
    lv_obj_set_style_arc_color(s_pv_meter.arc_green, CLR_CYAN, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_pv_meter.arc_green, CLR_CYAN, LV_PART_INDICATOR);

    // Pack A: green left half, orange right half (regen / drive)
    lv_obj_set_style_arc_color(s_pa_meter.arc_green, CLR_GREEN, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_pa_meter.arc_green, CLR_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_pa_meter.arc_yellow, CLR_ORANGE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_pa_meter.arc_yellow, CLR_ORANGE, LV_PART_INDICATOR);

    // ── SOC bar ───────────────────────────────────────────────────────────
    // Track
    lv_obj_t *soc_track = make_rect(scr, s_soc_bar_x, BAR_TOP,
                                     BAR_W, BAR_H, CLR_BORDER, 3);
    (void)soc_track;
    // Fill
    s_bar_soc = make_rect(scr, s_soc_bar_x, BAR_TOP + BAR_H,
                           BAR_W, 0, CLR_CYAN, 3);

    lv_obj_t *soc_top = make_label(scr, "100%", CLR_TEXT_DIM,
                                    &lv_font_montserrat_10);
    lv_obj_align(soc_top, LV_ALIGN_TOP_LEFT, s_soc_bar_x - 2, BAR_TOP - 14);

    s_lbl_soc_pct = make_label(scr, "0%", CLR_CYAN, &lv_font_montserrat_14);
    lv_obj_align(s_lbl_soc_pct, LV_ALIGN_TOP_LEFT,
                 s_soc_bar_x - 2, BAR_TOP + BAR_H + 4);

    s_lbl_range = make_label(scr, "--", CLR_TEXT_BRIGHT, &lv_font_montserrat_10);
    lv_obj_align(s_lbl_range, LV_ALIGN_TOP_LEFT,
                 s_soc_bar_x - 2, BAR_TOP - 26);

    // ── Center ────────────────────────────────────────────────────────────
    lv_obj_t *ctr = lv_obj_create(scr);
    lv_obj_set_pos(ctr, CTR_X, 0);
    lv_obj_set_size(ctr, CTR_W, MAIN_H);
    lv_obj_set_style_bg_color(ctr, CLR_BG, 0);
    lv_obj_set_style_border_width(ctr, 0, 0);
    lv_obj_set_style_radius(ctr, 0, 0);
    lv_obj_clear_flag(ctr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *spd_lbl = make_label(ctr, "SPEED", CLR_TEXT_DIM,
                                    &lv_font_montserrat_14);
    lv_obj_align(spd_lbl, LV_ALIGN_TOP_MID, 0, 18);

    s_lbl_speed = make_label(ctr, "0", CLR_WHITE, &lv_font_montserrat_48);
    lv_obj_align(s_lbl_speed, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *unit = make_label(ctr, UNITS_SPEED_LABEL, CLR_TEXT_MID,
                                 &lv_font_montserrat_18);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *gdiv = lv_obj_create(ctr);
    lv_obj_set_size(gdiv, CTR_W - 40, 1);
    lv_obj_set_style_bg_color(gdiv, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(gdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gdiv, 0, 0);
    lv_obj_align(gdiv, LV_ALIGN_CENTER, 0, 60);

    s_lbl_gear = make_label(ctr, "P", CLR_CYAN, &lv_font_montserrat_48);
    lv_obj_align(s_lbl_gear, LV_ALIGN_BOTTOM_MID, 0, -88);

    const char *gnames[] = {"P","R","N","D","B"};
    for (int i = 0; i < 5; i++) {
        s_lbl_prnd[i] = make_label(ctr, gnames[i], CLR_TEXT_DIM,
                                    &lv_font_montserrat_18);
        lv_obj_align(s_lbl_prnd[i], LV_ALIGN_BOTTOM_MID,
                     -96 + i * 48, -46);
    }
    lv_obj_set_style_text_color(s_lbl_prnd[0], CLR_CYAN, 0);

    // ── Power bar ─────────────────────────────────────────────────────────
    make_rect(scr, s_pwr_bar_x, BAR_TOP, BAR_W, BAR_H, CLR_BORDER, 3);
    s_bar_pwr = make_rect(scr, s_pwr_bar_x, BAR_TOP + BAR_H/2,
                           BAR_W, 0, CLR_ORANGE, 3);

    // Zero line
    lv_obj_t *zero = make_rect(scr, s_pwr_bar_x - 4, BAR_TOP + BAR_H/2,
                                BAR_W + 4, 2, CLR_TEXT_MID, 0);
    (void)zero;

    lv_obj_t *kw_top = make_label(scr, "+200", CLR_AMBER,
                                   &lv_font_montserrat_10);
    lv_obj_align(kw_top, LV_ALIGN_TOP_LEFT,
                 s_pwr_bar_x, BAR_TOP - 14);

    lv_obj_t *kw_hdr = make_label(scr, "kW", CLR_TEXT_DIM,
                                   &lv_font_montserrat_10);
    lv_obj_align(kw_hdr, LV_ALIGN_TOP_LEFT, s_pwr_bar_x + 2, BAR_TOP - 26);

    s_lbl_pwr_val = make_label(scr, "+0", CLR_ORANGE, &lv_font_montserrat_14);
    lv_obj_align(s_lbl_pwr_val, LV_ALIGN_TOP_LEFT,
                 s_pwr_bar_x, BAR_TOP + BAR_H + 4);

    // ── Right panel ───────────────────────────────────────────────────────
    lv_obj_t *right = lv_obj_create(scr);
    lv_obj_set_pos(right, W - RIGHT_W, 0);
    lv_obj_set_size(right, RIGHT_W, MAIN_H);
    lv_obj_set_style_bg_color(right, CLR_PANEL, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(right, CLR_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_border_side(right, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_set_style_pad_left(right, 14, 0);
    lv_obj_set_style_pad_top(right, 14, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    struct { lv_obj_t **lbl; const char *tag; const char *init; } rows[] = {
        {&s_lbl_eff,  "EFFICIENCY\n" UNITS_EFF_LABEL, "--"},
        {&s_lbl_trip, "TRIP kWh",   "--"},
        {NULL,        "EST RANGE",  "--"},
        {&s_lbl_aux_v,"12V AUX",    "0.0 V"},
    };
    // We reuse s_lbl_range for range — just add 12V aux here
    // Actually range is shown below SOC bar — add separate range label here too
    static lv_obj_t *s_lbl_range2 = NULL;  // second instance for right panel

    lv_obj_t *r_eff_tag = make_label(right, "EFFICIENCY", CLR_TEXT_DIM,
                                      &lv_font_montserrat_10);
    lv_obj_align(r_eff_tag, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *r_eff_unit = make_label(right, UNITS_EFF_LABEL, CLR_TEXT_DIM,
                                       &lv_font_montserrat_10);
    lv_obj_align(r_eff_unit, LV_ALIGN_TOP_LEFT, 0, 14);
    s_lbl_eff = make_label(right, "--", CLR_TEXT_BRIGHT,
                            &lv_font_montserrat_48);
    lv_obj_align(s_lbl_eff, LV_ALIGN_TOP_LEFT, 0, 30);

    lv_obj_t *d1 = lv_obj_create(right);
    lv_obj_set_size(d1, RIGHT_W - 14, 1);
    lv_obj_set_style_bg_color(d1, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(d1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d1, 0, 0);
    lv_obj_align(d1, LV_ALIGN_TOP_LEFT, 0, 100);

    make_label(right, "TRIP kWh", CLR_TEXT_DIM, &lv_font_montserrat_10);
    lv_obj_t *trip_tag = lv_obj_get_child(right, lv_obj_get_child_cnt(right)-1);
    lv_obj_align(trip_tag, LV_ALIGN_TOP_LEFT, 0, 112);
    s_lbl_trip = make_label(right, "--", CLR_TEXT_BRIGHT,
                             &lv_font_montserrat_48);
    lv_obj_align(s_lbl_trip, LV_ALIGN_TOP_LEFT, 0, 126);

    lv_obj_t *d2 = lv_obj_create(right);
    lv_obj_set_size(d2, RIGHT_W - 14, 1);
    lv_obj_set_style_bg_color(d2, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(d2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d2, 0, 0);
    lv_obj_align(d2, LV_ALIGN_TOP_LEFT, 0, 200);

    make_label(right, "12V AUX", CLR_TEXT_DIM, &lv_font_montserrat_10);
    lv_obj_t *aux_tag = lv_obj_get_child(right, lv_obj_get_child_cnt(right)-1);
    lv_obj_align(aux_tag, LV_ALIGN_TOP_LEFT, 0, 212);
    s_lbl_aux_v = make_label(right, "0.0 V", CLR_TEXT_BRIGHT,
                              &lv_font_montserrat_48);
    lv_obj_align(s_lbl_aux_v, LV_ALIGN_TOP_LEFT, 0, 226);

    // ── Bottom bar ────────────────────────────────────────────────────────
    lv_obj_t *bot = lv_obj_create(scr);
    lv_obj_set_pos(bot, 0, MAIN_H);
    lv_obj_set_size(bot, W, BOT_H);
    lv_obj_set_style_bg_color(bot, lv_color_hex(0x050709), 0);
    lv_obj_set_style_border_color(bot, CLR_BORDER, 0);
    lv_obj_set_style_border_width(bot, 1, 0);
    lv_obj_set_style_border_side(bot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(bot, 0, 0);
    lv_obj_clear_flag(bot, LV_OBJ_FLAG_SCROLLABLE);

    make_label(bot, "CAN 500 kbps", CLR_TEXT_DIM, &lv_font_montserrat_10);
    lv_obj_t *can_lbl = lv_obj_get_child(bot, 0);
    lv_obj_align(can_lbl, LV_ALIGN_LEFT_MID, 24, 0);

    make_label(bot, "ESP32-P4  |  M5Stack Tab5", CLR_TEXT_DIM,
               &lv_font_montserrat_10);
    lv_obj_t *hw_lbl = lv_obj_get_child(bot, 1);
    lv_obj_align(hw_lbl, LV_ALIGN_CENTER, 0, 0);

    s_dot_can = lv_obj_create(bot);
    lv_obj_set_size(s_dot_can, 10, 10);
    lv_obj_set_style_radius(s_dot_can, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot_can, CLR_GREEN, 0);
    lv_obj_set_style_bg_opa(s_dot_can, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dot_can, 0, 0);
    lv_obj_align(s_dot_can, LV_ALIGN_RIGHT_MID, -16, 0);
}

// ── dashboard_ui_update ───────────────────────────────────────────────────
void dashboard_ui_update(const DashData *d)
{
    char buf[32];

    // ── Meter gauge update helper ─────────────────────────────────────────
    // Updates needle position and value label; only calls set_style on color change
    auto uptemp = [](Meter *m, float v, float vmin, float vmax,
                     float warn, float crit, lv_color_t *last_col) {
        float pct = (v - vmin) / (vmax - vmin);
        update_needle(m, pct);
        char b[12];
        snprintf(b, sizeof(b), "%.0f C", v);
        lv_label_set_text(m->lbl_val, b);
        lv_color_t nc = v >= crit ? CLR_RED : v >= warn ? CLR_AMBER : CLR_GREEN;
        if (memcmp(&nc, last_col, sizeof(lv_color_t)) != 0) {
            lv_obj_set_style_text_color(m->lbl_val, nc, 0);
            *last_col = nc;
        }
    };

    static lv_color_t lc_inv = {0}, lc_mot = {0}, lc_bat = {0};
    static lv_color_t lc_pv  = {0}, lc_pa  = {0};

    // Temp meters: scale 0-150°C
    uptemp(&s_inv_meter, d->inverter_temp_c, 0, 150,
           WARN_INV_TEMP_C, CRIT_INV_TEMP_C, &lc_inv);
    uptemp(&s_mot_meter, d->motor_temp_c,    0, 150,
           WARN_MOTOR_TEMP_C, CRIT_MOTOR_TEMP_C, &lc_mot);
    uptemp(&s_bat_meter, d->batt_temp_c,     0,  60,
           WARN_BATT_TEMP_C, CRIT_BATT_TEMP_C, &lc_bat);

    // Pack voltage meter: scale 300-420V
    {
        float pct = (d->pack_volts - 300.0f) / 120.0f;
        update_needle(&s_pv_meter, pct);
        snprintf(buf, sizeof(buf), "%.0f V", d->pack_volts);
        lv_label_set_text(s_pv_meter.lbl_val, buf);
        lv_color_t nc = CLR_CYAN;
        if (memcmp(&nc, &lc_pv, sizeof(lv_color_t)) != 0) {
            lv_obj_set_style_text_color(s_pv_meter.lbl_val, CLR_CYAN, 0);
            lc_pv = nc;
        }
    }

    // Pack amps meter: scale -500 to +500A, zero at center
    {
        float pct = (d->pack_amps + 500.0f) / 1000.0f;
        update_needle(&s_pa_meter, pct);
        snprintf(buf, sizeof(buf), "%.0f A", d->pack_amps);
        lv_label_set_text(s_pa_meter.lbl_val, buf);
        lv_color_t nc = d->pack_amps < 0 ? CLR_GREEN : CLR_ORANGE;
        if (memcmp(&nc, &lc_pa, sizeof(lv_color_t)) != 0) {
            lv_obj_set_style_text_color(s_pa_meter.lbl_val, nc, 0);
            lc_pa = nc;
        }
    }

    // ── SOC bar ───────────────────────────────────────────────────────────
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

    // ── Speed ─────────────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "%d", (int)SPEED_TO_DISPLAY(d->speed));
    lv_label_set_text(s_lbl_speed, buf);

    // ── Gear ──────────────────────────────────────────────────────────────
    static int last_gear = -1;
    const char *gnames[] = {"P","R","N","D","B"};
    int g = (d->gear >= 0 && d->gear < 5) ? d->gear : 0;
    lv_label_set_text(s_lbl_gear, gnames[g]);
    if (g != last_gear) {
        for (int i = 0; i < 5; i++)
            lv_obj_set_style_text_color(s_lbl_prnd[i],
                i == g ? CLR_CYAN : CLR_TEXT_DIM, 0);
        last_gear = g;
    }

    // ── Power bar ─────────────────────────────────────────────────────────
    static lv_color_t last_pwr_col = {0};
    float kw      = d->power_kw;
    float kw_frac = kw / PWR_FULL;
    lv_coord_t half   = BAR_H / 2;
    lv_coord_t zero_y = BAR_TOP + half;
    lv_color_t pwr_col = kw >= 0.0f ? CLR_ORANGE : CLR_GREEN;
    if (kw >= 0.0f) {
        lv_coord_t fh = (lv_coord_t)(kw_frac * half);
        if (fh > half) fh = half;
        lv_obj_set_pos(s_bar_pwr, s_pwr_bar_x, zero_y - fh);
        lv_obj_set_size(s_bar_pwr, BAR_W, fh);
    } else {
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

    // ── Right panel ───────────────────────────────────────────────────────
    if (d->speed > 2.0f && d->power_kw > 5.0f)
        snprintf(buf, sizeof(buf), "%.1f", EFF_TO_DISPLAY(d->speed / d->power_kw));
    else
        snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(s_lbl_eff, buf);

    lv_label_set_text(s_lbl_trip, "--");  // TODO: trip accumulator

    snprintf(buf, sizeof(buf), "%.1f V", d->aux_volts);
    lv_label_set_text(s_lbl_aux_v, buf);

    // ── Invalidate ────────────────────────────────────────────────────────
    lv_obj_invalidate(lv_screen_active());
}
