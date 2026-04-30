// ui_tanks.c — Three tank columns with fill-level bars + metric chips.
#include "ui_tanks.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_detail.h"
#include "data.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "tanks";

#define COL_GAP   8
#define ROW_PAD  12
#define TANK_BODY_H  300   // height of the fill-bar container
#define CHIP_H        36   // height of each metric chip

static lv_obj_t *s_screen                     = NULL;
static lv_obj_t *s_fill_bars[TANK_COUNT]      = {NULL};
static lv_obj_t *s_pct_labels[TANK_COUNT]     = {NULL};
static lv_obj_t *s_status_badges[TANK_COUNT]  = {NULL};
static lv_timer_t *s_refresh_timer            = NULL;

// Per-metric chip value labels (for refresh)
static lv_obj_t *s_chip_vals[TANK_COUNT][MAX_TANK_METRICS] = {{NULL}};

// Per-tank drag state
static int16_t  s_press_y[TANK_COUNT];
static float    s_press_pct[TANK_COUNT];
static bool     s_dragging[TANK_COUNT];

// ─── Status badge text from level ─────────────────────────────────────────────
static const char *status_text(float pct) {
    if (pct < 15.0f) return "Critical";
    if (pct < 40.0f) return "Low";
    return "OK";
}

static lv_color_t status_color(float pct) {
    if (pct < 15.0f) return COL_ERR;
    if (pct < 40.0f) return COL_WARN;
    return COL_OK;
}

// ─── Update fill bar height and colour from current level ──────────────────
static void refresh_tank(int idx) {
    if (!s_fill_bars[idx] || !s_pct_labels[idx]) return;
    Tank *t    = &g_tanks[idx];
    float pct  = tank_level(t);
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    lv_obj_t *fill  = s_fill_bars[idx];
    lv_obj_t *outer = lv_obj_get_parent(fill);
    int total_h     = lv_obj_get_height(outer) - 2;
    int fill_h      = (int)(total_h * pct / 100.0f);
    if (fill_h < 0) fill_h = 0;

    lv_color_t fc = lv_color_hex(tank_fill_color(pct));

    lv_obj_set_height(fill, fill_h);
    lv_obj_set_pos(fill, 1, total_h - fill_h + 1);
    lv_obj_set_style_bg_color(fill, fc, 0);

    lv_obj_t *shine = lv_obj_get_child(fill, 0);
    if (shine) {
        int shine_h = fill_h > 12 ? fill_h - 6 : fill_h;
        lv_obj_set_size(shine, lv_obj_get_width(fill) / 3, shine_h);
        lv_obj_set_pos(shine, 3, 0);
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)pct);
    lv_label_set_text(s_pct_labels[idx], buf);

    // Update status badge
    if (s_status_badges[idx]) {
        lv_label_set_text(s_status_badges[idx], status_text(pct));
        lv_obj_set_style_text_color(s_status_badges[idx], status_color(pct), 0);
    }

    // Update metric chip values
    for (int k = 1; k < t->metric_count; k++) {
        if (!s_chip_vals[idx][k]) continue;
        Metric *m = &t->metrics[k];
        char val_str[24];
        snprintf(val_str, sizeof(val_str), "%.1f %s", m->value, m->unit);
        lv_label_set_text(s_chip_vals[idx][k], val_str);
        lv_obj_set_style_text_color(s_chip_vals[idx][k],
                                    lv_color_hex(metric_color(m)), 0);
    }
}

static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (g_current_view != VIEW_TANKS) return;
    for (int i = 0; i < TANK_COUNT; i++) refresh_tank(i);
}

// ─── Tank label button tap ─────────────────────────────────────────────────
static void label_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_push_tank_ovw(idx);
}

// ─── Chip tap ──────────────────────────────────────────────────────────────
typedef struct { int tank; int metric; } TankMetricCtx;

static void chip_tap_cb(lv_event_t *e) {
    TankMetricCtx *ctx = (TankMetricCtx *)lv_event_get_user_data(e);
    ui_detail_open_tank_metric(ctx->tank, ctx->metric);
}

// ─── Tank body events: drag-to-set-level + tap-to-detail ──────────────────
static void tank_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= TANK_COUNT) return;
    Tank *t = &g_tanks[idx];

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        s_press_y[idx]   = pt.y;
        s_press_pct[idx] = tank_level(t);
        s_dragging[idx]  = false;
    }
    else if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        int dy = s_press_y[idx] - pt.y;
        if (dy > 8 || dy < -8) s_dragging[idx] = true;
        if (s_dragging[idx]) {
            lv_obj_t *outer = lv_event_get_target(e);
            int h = lv_obj_get_height(outer);
            if (h <= 0) return;
            float new_pct = s_press_pct[idx] + (float)dy / (float)h * 100.0f;
            if (new_pct < 0.0f)   new_pct = 0.0f;
            if (new_pct > 100.0f) new_pct = 100.0f;
            t->metrics[0].value = new_pct;
            refresh_tank(idx);
        }
    }
    else if (code == LV_EVENT_SHORT_CLICKED) {
        if (!s_dragging[idx]) {
            ui_detail_open_tank_level(idx);
        }
        s_dragging[idx] = false;
    }
    else if (code == LV_EVENT_RELEASED) {
        s_dragging[idx] = false;
    }
}

// ─── Build one tank column ─────────────────────────────────────────────────
static void build_tank_col(lv_obj_t *parent, int idx) {
    Tank *t     = &g_tanks[idx];
    int col_w   = (CONTENT_W - COL_GAP * (TANK_COUNT + 1)) / TANK_COUNT;

    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, col_w, CONTENT_H - ROW_PAD * 2);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(col, 6, 0);

    // ── Fill-bar outer container ──────────────────────────────────────────
    lv_obj_t *outer = lv_obj_create(col);
    lv_obj_set_size(outer, col_w, TANK_BODY_H);
    lv_obj_set_style_bg_color(outer, COL_BASE2, 0);
    lv_obj_set_style_border_color(outer, COL_BD_STR, 0);
    lv_obj_set_style_border_width(outer, 1, 0);
    lv_obj_set_style_radius(outer, SZ_RADIUS_SM, 0);
    lv_obj_set_style_clip_corner(outer, true, 0);
    lv_obj_set_style_pad_all(outer, 0, 0);
    lv_obj_clear_flag(outer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(outer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(outer, tank_event_cb, LV_EVENT_PRESSED,        (void *)(intptr_t)idx);
    lv_obj_add_event_cb(outer, tank_event_cb, LV_EVENT_PRESSING,       (void *)(intptr_t)idx);
    lv_obj_add_event_cb(outer, tank_event_cb, LV_EVENT_SHORT_CLICKED,  (void *)(intptr_t)idx);
    lv_obj_add_event_cb(outer, tank_event_cb, LV_EVENT_RELEASED,       (void *)(intptr_t)idx);

    // Fill bar (anchored to bottom, sized by pct%)
    lv_obj_t *fill = lv_obj_create(outer);
    float pct = tank_level(t);
    int fill_h = (int)((TANK_BODY_H - 2) * pct / 100.0f);
    lv_obj_set_size(fill, col_w - 2, fill_h);
    lv_obj_set_pos(fill, 1, TANK_BODY_H - 2 - fill_h);
    lv_obj_set_style_bg_color(fill, lv_color_hex(tank_fill_color(pct)), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_80, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 0, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    lv_obj_set_style_outline_width(fill, 0, 0);
    lv_obj_set_style_shadow_width(fill, 0, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_fill_bars[idx] = fill;

    // Shine strip
    lv_obj_t *shine = lv_obj_create(fill);
    lv_obj_set_size(shine, (col_w - 2) / 3, fill_h > 12 ? fill_h - 6 : fill_h);
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
        int ty = (int)((TANK_BODY_H - 2) * (100 - p) / 100);
        lv_obj_t *tick = lv_obj_create(outer);
        lv_obj_set_size(tick, col_w / 4, 1);
        lv_obj_set_pos(tick, col_w - col_w / 4 - 2, ty);
        lv_obj_set_style_bg_color(tick, lv_color_hex(0xC0C8D8), 0);
        lv_obj_set_style_border_width(tick, 0, 0);
        lv_obj_set_style_radius(tick, 0, 0);
        lv_obj_set_style_pad_all(tick, 0, 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE);
    }

    // Percentage label centered on fill bar
    lv_obj_t *pct_lbl = lv_label_create(outer);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)pct);
    lv_label_set_text(pct_lbl, buf);
    lv_obj_set_style_text_font(pct_lbl, FONT_MONO_MD, 0);
    lv_obj_set_style_text_color(pct_lbl, COL_BASE, 0);
    lv_obj_center(pct_lbl);
    s_pct_labels[idx] = pct_lbl;

    // ── Tank label button (name + status badge) ───────────────────────────
    lv_obj_t *label_btn = lv_obj_create(col);
    lv_obj_set_size(label_btn, col_w, 40);
    lv_obj_set_style_bg_color(label_btn, COL_BASE, 0);
    lv_obj_set_style_border_color(label_btn, COL_BD, 0);
    lv_obj_set_style_border_width(label_btn, 1, 0);
    lv_obj_set_style_radius(label_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(label_btn, 8, 0);
    lv_obj_set_style_pad_ver(label_btn, 0, 0);
    lv_obj_set_style_shadow_width(label_btn, 0, 0);
    lv_obj_clear_flag(label_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(label_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(label_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(label_btn, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(label_btn, label_btn_cb, LV_EVENT_SHORT_CLICKED,
                        (void *)(intptr_t)idx);

    lv_obj_t *name_lbl = lv_label_create(label_btn);
    lv_label_set_text(name_lbl, t->name);
    lv_obj_set_style_text_font(name_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(name_lbl, COL_TX2, 0);

    lv_obj_t *badge_lbl = lv_label_create(label_btn);
    lv_label_set_text(badge_lbl, status_text(pct));
    lv_obj_set_style_text_font(badge_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(badge_lbl, status_color(pct), 0);
    s_status_badges[idx] = badge_lbl;

    // ── Metric chip strip (skip index 0 = level) ──────────────────────────
    for (int k = 1; k < t->metric_count; k++) {
        Metric *m = &t->metrics[k];

        lv_obj_t *chip = lv_obj_create(col);
        lv_obj_set_size(chip, col_w, CHIP_H);
        lv_obj_set_style_bg_color(chip, COL_BASE, 0);
        lv_obj_set_style_radius(chip, SZ_RADIUS_SM, 0);
        lv_obj_set_style_border_color(chip, COL_BD, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_pad_hor(chip, 8, 0);
        lv_obj_set_style_pad_ver(chip, 0, 0);
        lv_obj_set_style_shadow_width(chip, 0, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(chip, 5, 0);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);

        TankMetricCtx *ctx = malloc(sizeof(TankMetricCtx));
        ctx->tank   = idx;
        ctx->metric = k;
        lv_obj_add_event_cb(chip, chip_tap_cb, LV_EVENT_SHORT_CLICKED, ctx);

        // Colored 8px status dot
        lv_obj_t *dot = lv_obj_create(chip);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_bg_color(dot, lv_color_hex(metric_color(m)), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *key = lv_label_create(chip);
        lv_label_set_text(key, m->label);
        lv_obj_set_style_text_font(key, FONT_SANS_XXS, 0);
        lv_obj_set_style_text_color(key, COL_TX2, 0);
        lv_obj_set_flex_grow(key, 1);

        lv_obj_t *val = lv_label_create(chip);
        char val_str[24];
        snprintf(val_str, sizeof(val_str), "%.1f %s", m->value, m->unit);
        lv_label_set_text(val, val_str);
        lv_obj_set_style_text_font(val, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(val, lv_color_hex(metric_color(m)), 0);
        s_chip_vals[idx][k] = val;
    }
}

// ─── Public ────────────────────────────────────────────────────────────────
void ui_tanks_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, CONTENT_W, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, ROW_PAD, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    g_screens[VIEW_TANKS] = s_screen;

    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(s_screen, COL_GAP, 0);

    for (int i = 0; i < TANK_COUNT; i++) {
        build_tank_col(s_screen, i);
    }

    s_refresh_timer = lv_timer_create(refresh_timer_cb, 500, NULL);
    lv_timer_pause(s_refresh_timer);

    ESP_LOGI(TAG, "Tanks screen created");
}

void ui_tanks_activate(void) {
    if (s_refresh_timer) lv_timer_resume(s_refresh_timer);
    for (int i = 0; i < TANK_COUNT; i++) refresh_tank(i);
}

void ui_tanks_deactivate(void) {
    if (s_refresh_timer) lv_timer_pause(s_refresh_timer);
}
