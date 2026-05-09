// ui_tank_overview.c — Single-tank detail view: large fill bar + metric tile list.
#include "ui_tank_overview.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_detail.h"
#include "data.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "tank_ovw";

#define OVW_FILL_W  200
#define OVW_PAD     SZ_GAP
#define OVW_FILL_H  (DETAIL_H - OVW_PAD * 2 - 8 - 48)
#define OVW_RIGHT_W (CONTENT_W - OVW_PAD * 2 - OVW_FILL_W - SZ_GAP)
#define TILE_H       48

static lv_obj_t *s_screen          = NULL;
static lv_obj_t *s_name_lbl        = NULL;
static lv_obj_t *s_badge_lbl       = NULL;
static lv_obj_t *s_fill_bar        = NULL;
static lv_obj_t *s_fill_outer      = NULL;
static lv_obj_t *s_fill_pct_lbl    = NULL;
static lv_obj_t *s_tile_rows[MAX_TANK_METRICS]  = {NULL};
static lv_obj_t *s_tile_dots[MAX_TANK_METRICS]  = {NULL};
static lv_obj_t *s_tile_vals[MAX_TANK_METRICS]  = {NULL};

static int s_tank_idx = 0;

// ── Fill/Drain control panel ────────────────────────────────────────────────
static lv_obj_t   *s_ctrl_panel   = NULL;
static lv_obj_t   *s_target_lbl   = NULL;
static lv_obj_t   *s_slider       = NULL;
static lv_obj_t   *s_action_btn   = NULL;
static lv_obj_t   *s_action_lbl   = NULL;
static lv_obj_t   *s_target_line  = NULL;
static float       s_target_pct   = 0.0f;
static bool        s_animating    = false;
static lv_timer_t *s_anim_timer   = NULL;

static const char *ovw_status_text(float pct) {
    if (pct < 15.0f) return "Critical";
    if (pct < 40.0f) return "Low";
    return "OK";
}

static lv_color_t ovw_status_color(float pct) {
    if (pct < 15.0f) return COL_ERR;
    if (pct < 40.0f) return COL_WARN;
    return COL_OK;
}

static void fill_tap_cb(lv_event_t *e) {
    (void)e;
    ui_detail_open_tank_level(s_tank_idx);
}

static void tile_tap_cb(lv_event_t *e) {
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    ui_detail_open_tank_metric(s_tank_idx, k);
}

static void ovw_refresh_fill(void) {
    if (!s_fill_bar || !s_fill_outer || !s_fill_pct_lbl) return;
    Tank *t = &g_tanks[s_tank_idx];
    float pct = tank_level(t);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    int fill_h = (int)((OVW_FILL_H - 2) * pct / 100.0f);
    if (fill_h < 0) fill_h = 0;
    lv_color_t fc = lv_color_hex(tank_fill_color(pct));
    lv_obj_set_height(s_fill_bar, fill_h);
    lv_obj_set_pos(s_fill_bar, 1, OVW_FILL_H - 2 - fill_h + 1);
    lv_obj_set_style_bg_color(s_fill_bar, fc, 0);

    lv_obj_t *shine = lv_obj_get_child(s_fill_bar, 0);
    if (shine) {
        int shine_h = fill_h > 12 ? fill_h - 6 : fill_h;
        lv_obj_set_size(shine, (OVW_FILL_W - 2) / 3, shine_h);
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)pct);
    lv_label_set_text(s_fill_pct_lbl, buf);

    lv_label_set_text(s_badge_lbl, ovw_status_text(pct));
    lv_obj_set_style_text_color(s_badge_lbl, ovw_status_color(pct), 0);
}

static void ovw_show_target_line(float target_pct) {
    if (!s_target_line) return;
    int ty = (int)((OVW_FILL_H - 2) * (100.0f - target_pct) / 100.0f);
    lv_obj_set_pos(s_target_line, 0, ty);
    lv_obj_clear_flag(s_target_line, LV_OBJ_FLAG_HIDDEN);
}

static void ovw_hide_target_line(void) {
    if (s_target_line) lv_obj_add_flag(s_target_line, LV_OBJ_FLAG_HIDDEN);
}

static void update_action_btn(void) {
    if (!s_action_btn || !s_action_lbl || !s_target_lbl) return;
    Tank *t = &g_tanks[s_tank_idx];
    float cur = tank_level(t);

    char lbl_buf[24];
    snprintf(lbl_buf, sizeof(lbl_buf), "Target: %d%%", (int)s_target_pct);
    lv_label_set_text(s_target_lbl, lbl_buf);

    if (s_animating) {
        lv_obj_set_style_bg_color(s_action_btn, COL_ERR, 0);
        lv_label_set_text(s_action_lbl, "Stop");
        lv_obj_set_style_text_color(s_action_lbl, COL_BASE, 0);
        lv_obj_add_flag(s_action_btn, LV_OBJ_FLAG_CLICKABLE);
    } else if ((int)s_target_pct == (int)cur) {
        lv_obj_set_style_bg_color(s_action_btn, COL_BASE3, 0);
        char buf[20];
        snprintf(buf, sizeof(buf), "At %d%%", (int)cur);
        lv_label_set_text(s_action_lbl, buf);
        lv_obj_set_style_text_color(s_action_lbl, COL_TX3, 0);
        lv_obj_clear_flag(s_action_btn, LV_OBJ_FLAG_CLICKABLE);
    } else if (s_target_pct > cur) {
        lv_obj_set_style_bg_color(s_action_btn, COL_HI, 0);
        char buf[24];
        snprintf(buf, sizeof(buf), "Fill to %d%%", (int)s_target_pct);
        lv_label_set_text(s_action_lbl, buf);
        lv_obj_set_style_text_color(s_action_lbl, COL_BASE, 0);
        lv_obj_add_flag(s_action_btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_set_style_bg_color(s_action_btn, COL_ERR, 0);
        char buf[24];
        snprintf(buf, sizeof(buf), "Drain to %d%%", (int)s_target_pct);
        lv_label_set_text(s_action_lbl, buf);
        lv_obj_set_style_text_color(s_action_lbl, COL_BASE, 0);
        lv_obj_add_flag(s_action_btn, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void slider_cb(lv_event_t *e) {
    (void)e;
    if (s_animating) return;
    s_target_pct = (float)lv_slider_get_value(s_slider);
    Tank *t = &g_tanks[s_tank_idx];
    float cur = tank_level(t);
    lv_color_t ind_col = (s_target_pct > cur) ? COL_HI : COL_ERR;
    lv_obj_set_style_bg_color(s_slider, ind_col, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(s_slider, ind_col, LV_PART_KNOB);
    ovw_show_target_line(s_target_pct);
    update_action_btn();
}

static void action_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_animating) {
        lv_timer_pause(s_anim_timer);
        s_animating = false;
        lv_obj_add_flag(s_fill_outer, LV_OBJ_FLAG_CLICKABLE);
        ovw_hide_target_line();
        update_action_btn();
    } else {
        Tank *t = &g_tanks[s_tank_idx];
        float cur = tank_level(t);
        if ((int)s_target_pct == (int)cur) return;
        s_animating = true;
        lv_obj_clear_flag(s_fill_outer, LV_OBJ_FLAG_CLICKABLE);
        ovw_show_target_line(s_target_pct);
        lv_timer_resume(s_anim_timer);
        update_action_btn();
    }
}

static void anim_timer_cb(lv_timer_t *tmr) {
    (void)tmr;
    if (!s_animating) return;

    Tank *tk = &g_tanks[s_tank_idx];
    float cur = tank_level(tk);
    bool filling = (s_target_pct > cur);
    float next = cur + (filling ? 1.2f : -1.2f);

    if (filling  && next >= s_target_pct - 0.6f) next = s_target_pct;
    if (!filling && next <= s_target_pct + 0.6f) next = s_target_pct;
    if (next < 0.0f)   next = 0.0f;
    if (next > 100.0f) next = 100.0f;

    tk->metrics[0].value = next;
    ovw_refresh_fill();

    bool done = (filling && next >= s_target_pct) ||
                (!filling && next <= s_target_pct);
    if (done) {
        lv_timer_pause(s_anim_timer);
        s_animating = false;
        lv_obj_add_flag(s_fill_outer, LV_OBJ_FLAG_CLICKABLE);
        ovw_hide_target_line();
        update_action_btn();
    }
}

void ui_tank_overview_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, CONTENT_W, DETAIL_H);
    lv_obj_set_pos(s_screen, 0, SZ_CRUMB);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, OVW_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, 8, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_screens[VIEW_TANK_OVW] = s_screen;

    // ── Status header row ──────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(s_screen);
    lv_obj_set_size(hdr, CONTENT_W - OVW_PAD * 2, 48);
    lv_obj_set_style_bg_color(hdr, COL_BASE, 0);
    lv_obj_set_style_border_color(hdr, COL_BD, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_radius(hdr, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(hdr, 14, 0);
    lv_obj_set_style_pad_ver(hdr, 0, 0);
    lv_obj_set_style_shadow_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_name_lbl = lv_label_create(hdr);
    lv_label_set_text(s_name_lbl, "—");
    lv_obj_set_style_text_font(s_name_lbl, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(s_name_lbl, COL_TX, 0);

    // Badge pill on the right
    lv_obj_t *pill = lv_obj_create(hdr);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, COL_BASE2, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, COL_BD_STR, 0);
    lv_obj_set_style_radius(pill, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_ver(pill, 6, 0);
    lv_obj_set_style_pad_hor(pill, 12, 0);
    lv_obj_set_style_shadow_width(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    s_badge_lbl = lv_label_create(pill);
    lv_label_set_text(s_badge_lbl, "OK");
    lv_obj_set_style_text_font(s_badge_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(s_badge_lbl, COL_OK, 0);
    lv_obj_center(s_badge_lbl);

    // ── Two-column body row ─────────────────────────────────────────────────────
    lv_obj_t *body_row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(body_row);
    lv_obj_set_size(body_row, CONTENT_W - OVW_PAD * 2, OVW_FILL_H);
    lv_obj_set_flex_flow(body_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(body_row, SZ_GAP, 0);
    lv_obj_clear_flag(body_row, LV_OBJ_FLAG_SCROLLABLE);

    s_fill_outer = lv_obj_create(body_row);
    lv_obj_set_size(s_fill_outer, OVW_FILL_W, OVW_FILL_H);
    lv_obj_set_style_bg_color(s_fill_outer, COL_BASE2, 0);
    lv_obj_set_style_border_color(s_fill_outer, COL_BD_STR, 0);
    lv_obj_set_style_border_width(s_fill_outer, 1, 0);
    lv_obj_set_style_radius(s_fill_outer, SZ_RADIUS_SM, 0);
    lv_obj_set_style_clip_corner(s_fill_outer, true, 0);
    lv_obj_set_style_pad_all(s_fill_outer, 0, 0);
    lv_obj_clear_flag(s_fill_outer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_fill_outer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_fill_outer, fill_tap_cb, LV_EVENT_SHORT_CLICKED, NULL);

    // Fill bar (initially 0 height — activate() sets it)
    s_fill_bar = lv_obj_create(s_fill_outer);
    lv_obj_set_size(s_fill_bar, OVW_FILL_W - 2, 0);
    lv_obj_set_pos(s_fill_bar, 1, OVW_FILL_H - 1);
    lv_obj_set_style_bg_color(s_fill_bar, COL_HI, 0);
    lv_obj_set_style_bg_opa(s_fill_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_fill_bar, 0, 0);
    lv_obj_set_style_radius(s_fill_bar, 0, 0);
    lv_obj_set_style_pad_all(s_fill_bar, 0, 0);
    lv_obj_set_style_outline_width(s_fill_bar, 0, 0);
    lv_obj_set_style_shadow_width(s_fill_bar, 0, 0);
    lv_obj_clear_flag(s_fill_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Shine strip
    lv_obj_t *shine = lv_obj_create(s_fill_bar);
    lv_obj_set_size(shine, (OVW_FILL_W - 2) / 3, 0);
    lv_obj_set_pos(shine, 3, 0);
    lv_obj_set_style_bg_color(shine, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(shine, LV_OPA_20, 0);
    lv_obj_set_style_border_width(shine, 0, 0);
    lv_obj_set_style_radius(shine, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_all(shine, 0, 0);
    lv_obj_set_style_outline_width(shine, 0, 0);
    lv_obj_set_style_shadow_width(shine, 0, 0);
    lv_obj_clear_flag(shine, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Tick marks at 25%, 50%, 75%
    for (int p = 25; p <= 75; p += 25) {
        int ty = (int)((OVW_FILL_H - 2) * (100 - p) / 100);
        lv_obj_t *tick = lv_obj_create(s_fill_outer);
        lv_obj_set_size(tick, OVW_FILL_W / 4, 1);
        lv_obj_set_pos(tick, OVW_FILL_W - OVW_FILL_W / 4 - 2, ty);
        lv_obj_set_style_bg_color(tick, lv_color_hex(0xC0C8D8), 0);
        lv_obj_set_style_border_width(tick, 0, 0);
        lv_obj_set_style_radius(tick, 0, 0);
        lv_obj_set_style_pad_all(tick, 0, 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE);
    }

    // Target level indicator line (hidden by default)
    s_target_line = lv_obj_create(s_fill_outer);
    lv_obj_set_size(s_target_line, OVW_FILL_W - 2, 2);
    lv_obj_set_pos(s_target_line, 0, 0);
    lv_obj_set_style_bg_color(s_target_line, lv_color_hex(0xFFFF80), 0);
    lv_obj_set_style_bg_opa(s_target_line, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_target_line, 0, 0);
    lv_obj_set_style_radius(s_target_line, 0, 0);
    lv_obj_set_style_pad_all(s_target_line, 0, 0);
    lv_obj_add_flag(s_target_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_target_line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Pct label centered in fill bar container
    s_fill_pct_lbl = lv_label_create(s_fill_outer);
    lv_label_set_text(s_fill_pct_lbl, "—");
    lv_obj_set_style_text_font(s_fill_pct_lbl, FONT_MONO_MD, 0);
    lv_obj_set_style_text_color(s_fill_pct_lbl, COL_BASE, 0);
    lv_obj_center(s_fill_pct_lbl);

    // ── Metric tile list (all metrics, create MAX_TANK_METRICS rows) ───────────
    for (int k = 0; k < MAX_TANK_METRICS; k++) {
        lv_obj_t *tile = lv_obj_create(s_screen);
        lv_obj_set_size(tile, CONTENT_W - OVW_PAD * 2, TILE_H);
        lv_obj_set_style_bg_color(tile, COL_BASE, 0);
        lv_obj_set_style_border_color(tile, COL_BD, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_radius(tile, SZ_RADIUS_SM, 0);
        lv_obj_set_style_pad_hor(tile, 12, 0);
        lv_obj_set_style_pad_ver(tile, 0, 0);
        lv_obj_set_style_shadow_width(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(tile, 8, 0);
        lv_obj_add_event_cb(tile, tile_tap_cb, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)k);

        // Colored status dot
        lv_obj_t *dot = lv_obj_create(tile);
        lv_obj_set_size(dot, SZ_DOT, SZ_DOT);
        lv_obj_set_style_bg_color(dot, COL_HI, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, SZ_DOT / 2, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_tile_dots[k] = dot;

        // Metric label (takes remaining space)
        lv_obj_t *key = lv_label_create(tile);
        lv_label_set_text(key, "—");
        lv_obj_set_style_text_font(key, FONT_SANS_XXS, 0);
        lv_obj_set_style_text_color(key, COL_TX2, 0);
        lv_obj_set_flex_grow(key, 1);

        // Value + unit (right-aligned)
        lv_obj_t *val = lv_label_create(tile);
        lv_label_set_text(val, "—");
        lv_obj_set_style_text_font(val, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(val, COL_HI, 0);
        s_tile_vals[k] = val;

        s_tile_rows[k] = tile;
        // hidden until activate() sets metric_count
        lv_obj_add_flag(tile, LV_OBJ_FLAG_HIDDEN);
    }

    // ── Fill/Drain control panel ─────────────────────────────────────────────
    s_ctrl_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_ctrl_panel, CONTENT_W - OVW_PAD * 2, 96);
    lv_obj_set_style_bg_color(s_ctrl_panel, COL_BASE, 0);
    lv_obj_set_style_border_color(s_ctrl_panel, COL_BD, 0);
    lv_obj_set_style_border_width(s_ctrl_panel, 1, 0);
    lv_obj_set_style_radius(s_ctrl_panel, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_all(s_ctrl_panel, 6, 0);
    lv_obj_set_style_pad_gap(s_ctrl_panel, 4, 0);
    lv_obj_set_style_shadow_width(s_ctrl_panel, 0, 0);
    lv_obj_clear_flag(s_ctrl_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_ctrl_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ctrl_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_target_lbl = lv_label_create(s_ctrl_panel);
    lv_label_set_text(s_target_lbl, "Target: —");
    lv_obj_set_style_text_font(s_target_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(s_target_lbl, COL_TX2, 0);

    s_slider = lv_slider_create(s_ctrl_panel);
    lv_obj_set_size(s_slider, CONTENT_W - OVW_PAD * 2 - 12, 28);
    lv_slider_set_range(s_slider, 0, 100);
    lv_slider_set_value(s_slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider, COL_BASE3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, COL_HI, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, COL_BASE, LV_PART_KNOB);
    lv_obj_set_style_border_color(s_slider, COL_HI, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_slider, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_action_btn = lv_obj_create(s_ctrl_panel);
    lv_obj_set_size(s_action_btn, CONTENT_W - OVW_PAD * 2 - 12, 36);
    lv_obj_set_style_bg_color(s_action_btn, COL_BASE3, 0);
    lv_obj_set_style_border_width(s_action_btn, 0, 0);
    lv_obj_set_style_radius(s_action_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_all(s_action_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_action_btn, 0, 0);
    lv_obj_clear_flag(s_action_btn, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_action_btn, action_btn_cb, LV_EVENT_SHORT_CLICKED, NULL);

    s_action_lbl = lv_label_create(s_action_btn);
    lv_label_set_text(s_action_lbl, "At 0%");
    lv_obj_set_style_text_font(s_action_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(s_action_lbl, COL_TX3, 0);
    lv_obj_center(s_action_lbl);

    // Animation timer — created once, kept paused until fill/drain triggered
    s_anim_timer = lv_timer_create(anim_timer_cb, 100, NULL);
    lv_timer_pause(s_anim_timer);

    ESP_LOGI(TAG, "Tank overview screen created");
}

void ui_tank_overview_activate(int tank_idx) {
    if (!s_screen) return;
    if (tank_idx < 0 || tank_idx >= TANK_COUNT) return;

    // Cancel any in-progress animation from a previous visit
    if (s_animating && s_anim_timer) {
        lv_timer_pause(s_anim_timer);
        s_animating = false;
    }
    if (s_fill_outer) lv_obj_add_flag(s_fill_outer, LV_OBJ_FLAG_CLICKABLE);
    ovw_hide_target_line();

    s_tank_idx = tank_idx;

    Tank *t = &g_tanks[tank_idx];
    float pct = tank_level(t);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    // Header — name only (badge updated by ovw_refresh_fill)
    lv_label_set_text(s_name_lbl, t->name);
    ovw_refresh_fill();

    // Metric tiles — show only metrics that exist for this tank
    for (int k = 0; k < MAX_TANK_METRICS; k++) {
        if (!s_tile_rows[k]) continue;
        if (k >= t->metric_count) {
            lv_obj_add_flag(s_tile_rows[k], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_tile_rows[k], LV_OBJ_FLAG_HIDDEN);

        Metric *m = &t->metrics[k];
        uint32_t col = metric_color(m);

        if (s_tile_dots[k])
            lv_obj_set_style_bg_color(s_tile_dots[k], lv_color_hex(col), 0);

        lv_obj_t *key_lbl = lv_obj_get_child(s_tile_rows[k], 1);
        if (key_lbl) lv_label_set_text(key_lbl, m->label);

        if (s_tile_vals[k]) {
            char val_str[24];
            snprintf(val_str, sizeof(val_str), "%.1f %s", m->value, m->unit);
            lv_label_set_text(s_tile_vals[k], val_str);
            lv_obj_set_style_text_color(s_tile_vals[k], lv_color_hex(col), 0);
        }
    }

    // Initialise control panel to current level
    s_target_pct = pct;
    if (s_slider)  lv_slider_set_value(s_slider, (int)pct, LV_ANIM_OFF);
    update_action_btn();
}

void ui_tank_overview_deactivate(void) {
    if (s_animating && s_anim_timer) {
        lv_timer_pause(s_anim_timer);
        s_animating = false;
    }
    if (s_fill_outer) lv_obj_add_flag(s_fill_outer, LV_OBJ_FLAG_CLICKABLE);
    ovw_hide_target_line();
}
