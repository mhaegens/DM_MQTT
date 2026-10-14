// ui_tanks.c — Conical vessels (1–3), with live level and drag controls.
#include "ui_tanks.h"
#include "ui_tank_shape.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_detail.h"
#include "data.h"
#include "../mqtt_task.h"
#include "../config.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "tanks";

// ─── Layout constants ─────────────────────────────────────────────────────────
#define COL_GAP          8
#define ROW_PAD         12
#define TANK_TOTAL_H   460    // total tank widget height (px)
#define TANK_MAX_W     300    // width cap so 1–2 tanks stay tank-shaped
#define LABEL_BTN_H     56    // tank name / status button height (2 rows)
#define LABEL_BTN_GAP    6    // gap between tank widget and label button

// ─── Per-tank column state ────────────────────────────────────────────────────
typedef struct {
    int        tank_idx;
    lv_coord_t col_w;
    lv_obj_t  *tank_widget;
    lv_obj_t  *pct_label;
    lv_obj_t  *tick_labels[3];
    lv_coord_t tick_ys[3];
    lv_obj_t  *badge_label;
    lv_obj_t  *badge_pill;
} tank_col_t;

static tank_col_t  s_cols[MAX_TANKS];
static lv_obj_t   *s_screen       = NULL;
static lv_timer_t *s_refresh_timer = NULL;

// Per-tank drag state
static int16_t s_press_y[MAX_TANKS];
static float   s_press_pct[MAX_TANKS];
static bool    s_dragging[MAX_TANKS];
static uint32_t s_last_pub_ms[MAX_TANKS];  // lv_tick_get() at last throttled publish
static float   s_last_render_pct[MAX_TANKS] = {-1.0f, -1.0f, -1.0f};  // skip refresh when unchanged

#define DRAG_PUB_INTERVAL_MS 50

// ─── Status helpers ────────────────────────────────────────────────────────────
static const char *status_text(float pct) {
    if (pct < 15.0f) return "CRITICAL";
    if (pct < 40.0f) return "LOW LEVEL";
    return "OPERATIONAL";
}

static lv_color_t status_color(float pct) {
    if (pct < 15.0f) return COL_ERR;
    if (pct < 40.0f) return COL_WARN;
    return COL_OK;
}

static lv_color_t status_bg(float pct) {
    if (pct < 15.0f) return COL_ERR_LT;
    if (pct < 40.0f) return COL_WARN_LT;
    return COL_OK_LT;
}

static uint32_t status_bd(float pct) {
    if (pct < 15.0f) return COL_ERR_BD;
    if (pct < 40.0f) return COL_WARN_BD;
    return COL_OK_BD;
}

// ─── Custom drawing ───────────────────────────────────────────────────────────
static void tank_draw_cb(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    tank_col_t *col = lv_obj_get_user_data(obj);
    if (!col) return;
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    DATA_LOCK();
    float level = tank_level(&g_tanks[col->tank_idx]);
    DATA_UNLOCK();
    ui_tank_shape_draw(lv_event_get_layer(event), &area, level,
                       lv_color_hex(tank_fill_color(level)));
}

// ─── Update fill and labels from current level ────────────────────────────────
static void refresh_tank(int idx) {
    tank_col_t *col = &s_cols[idx];
    if (!col->tank_widget) return;

    DATA_LOCK();
    Tank  *t   = &g_tanks[idx];
    float  pct = tank_level(t);
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    DATA_UNLOCK();

    // Level unchanged since last render — skip the invalidate entirely
    // (the 500ms timer would otherwise repaint an identical tank)
    if (pct == s_last_render_pct[idx]) return;
    s_last_render_pct[idx] = pct;

    lv_obj_invalidate(col->tank_widget);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)pct);
    lv_label_set_text(col->pct_label, buf);

    lv_obj_update_layout(col->pct_label);
    lv_coord_t label_h = lv_obj_get_height(col->pct_label);
    lv_coord_t label_w = lv_obj_get_width(col->pct_label);
    lv_coord_t waterline = ui_tank_shape_level_y(TANK_TOTAL_H, pct);
    lv_coord_t label_y = waterline + 16;
    lv_coord_t max_y = ui_tank_shape_join_y(TANK_TOTAL_H) - label_h - 24;
    if (label_y > max_y) label_y = max_y;
    if (label_y < 70) label_y = 70;
    lv_obj_set_pos(col->pct_label, (col->col_w - label_w) / 2, label_y);
    lv_obj_set_style_text_color(col->pct_label,
        label_y + label_h / 2 >= waterline ? lv_color_white() : COL_TX2, 0);

    if (col->badge_label) {
        lv_label_set_text(col->badge_label, status_text(pct));
        lv_obj_set_style_text_color(col->badge_label, status_color(pct), 0);
    }
    if (col->badge_pill) {
        lv_obj_set_style_bg_color(col->badge_pill, status_bg(pct), 0);
        lv_obj_set_style_border_color(col->badge_pill, lv_color_hex(status_bd(pct)), 0);
    }
}

static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (g_current_view != VIEW_TANKS) return;
    for (int i = 0; i < g_tank_count; i++) refresh_tank(i);
}

// ─── Label button tap → drill into tank overview ───────────────────────────────
static void label_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_push_tank_ovw(idx);
}

// ─── Tank widget drag + tap events ────────────────────────────────────────────
static void tank_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_tank_count) return;
    Tank *t = &g_tanks[idx];

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        s_press_y[idx]   = pt.y;
        s_press_pct[idx] = tank_level(t);
        s_dragging[idx]  = false;
    } else if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        int dy = s_press_y[idx] - pt.y;
        if (dy > 8 || dy < -8) s_dragging[idx] = true;
        if (s_dragging[idx]) {
            g_tank_drag_active[idx] = true;
            lv_obj_t *widget = lv_event_get_target(e);
            int h = lv_obj_get_height(widget);
            if (h <= 0) return;
            float new_pct = s_press_pct[idx] + (float)dy / (float)h * 100.0f;
            if (new_pct < 0.0f)   new_pct = 0.0f;
            if (new_pct > 100.0f) new_pct = 100.0f;
            DATA_LOCK();
            t->metrics[0].value = new_pct;
            DATA_UNLOCK();
            refresh_tank(idx);
            uint32_t now = lv_tick_get();
            if (now - s_last_pub_ms[idx] >= DRAG_PUB_INTERVAL_MS) {
                s_last_pub_ms[idx] = now;
                mqtt_pub_tank_metric(t->id, t->metrics[0].key, new_pct);
            }
        }
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        if (!s_dragging[idx]) {
            ui_push_tank_ovw(idx);
        }
        s_dragging[idx] = false;
    } else if (code == LV_EVENT_RELEASED) {
        if (s_dragging[idx]) {
            mqtt_pub_tank_metric(t->id, t->metrics[0].key, t->metrics[0].value);
        }
        g_tank_drag_active[idx] = false;
        s_dragging[idx] = false;
    }
}

// ─── Position tick number labels after layout resolves ────────────────────────
static void position_tick_labels(int index)
{
    tank_col_t *col = &s_cols[index];
    static const int levels[] = {75, 50, 25};
    for (int i = 0; i < 3; ++i) {
        int right = ui_tank_shape_right_at(col->col_w, TANK_TOTAL_H, levels[i]);
        int y = ui_tank_shape_level_y(TANK_TOTAL_H, levels[i]);
        lv_obj_set_pos(col->tick_labels[i], right - lv_obj_get_width(col->tick_labels[i]) - 16,
                       y - lv_obj_get_height(col->tick_labels[i]) / 2);
    }
}

// ─── Build one tank column ─────────────────────────────────────────────────────
static void build_tank_col(lv_obj_t *parent, int idx) {
    Tank      *t    = &g_tanks[idx];
    lv_coord_t col_w = (CONTENT_W - COL_GAP * (g_tank_count + 1)) / g_tank_count;
    // Cap the width so 1–2 tanks stay tank-shaped instead of becoming a wall;
    // SPACE_EVENLY on the parent centers whatever is left over.
    if (col_w > TANK_MAX_W) col_w = TANK_MAX_W;
    tank_col_t *col = &s_cols[idx];
    col->tank_idx = idx;
    col->col_w    = col_w;

    /* Outer transparent column container */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, col_w, CONTENT_H - ROW_PAD * 2);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_shadow_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Tank widget — transparent background, custom draw fills it */
    lv_obj_t *tank_w = lv_obj_create(cont);
    lv_obj_set_size(tank_w, col_w, TANK_TOTAL_H);
    lv_obj_set_pos(tank_w, 0, 0);
    lv_obj_set_style_bg_opa(tank_w, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tank_w, 0, 0);
    lv_obj_set_style_outline_width(tank_w, 0, 0);
    lv_obj_set_style_pad_all(tank_w, 0, 0);
    lv_obj_set_style_shadow_width(tank_w, 0, 0);
    lv_obj_clear_flag(tank_w, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tank_w, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(tank_w, col);
    lv_obj_add_event_cb(tank_w, tank_draw_cb,   LV_EVENT_DRAW_MAIN,     NULL);
    lv_obj_add_event_cb(tank_w, tank_event_cb,  LV_EVENT_PRESSED,       (void *)(intptr_t)idx);
    lv_obj_add_event_cb(tank_w, tank_event_cb,  LV_EVENT_PRESSING,      (void *)(intptr_t)idx);
    lv_obj_add_event_cb(tank_w, tank_event_cb,  LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)idx);
    lv_obj_add_event_cb(tank_w, tank_event_cb,  LV_EVENT_RELEASED,      (void *)(intptr_t)idx);
    col->tank_widget = tank_w;

    /* Tick number labels (75, 50, 25) positioned after layout resolves */
    static const int TICK_PCTS[] = {75, 50, 25};
    for (int i = 0; i < 3; i++) {
        lv_coord_t tick_y_off = TANK_TOTAL_H * (100 - TICK_PCTS[i]) / 100;
        lv_obj_t *tl = lv_label_create(tank_w);
        lv_label_set_text_fmt(tl, "%d", TICK_PCTS[i]);
        lv_obj_set_style_text_color(tl, COL_TX3, 0);
        lv_obj_set_style_text_font(tl, FONT_SANS_XXS, 0);
        lv_obj_clear_flag(tl, LV_OBJ_FLAG_CLICKABLE);
        col->tick_labels[i] = tl;
        col->tick_ys[i]     = tick_y_off;
    }

    /* Percentage label — centered initially, repositioned by refresh_tank */
    float pct = tank_level(t);
    lv_obj_t *pct_lbl = lv_label_create(tank_w);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)pct);
    lv_label_set_text(pct_lbl, buf);
    lv_obj_set_style_text_font(pct_lbl, FONT_MONO_MD, 0);
    lv_obj_set_style_text_color(pct_lbl, lv_color_white(), 0);
    lv_obj_clear_flag(pct_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(pct_lbl);
    col->pct_label = pct_lbl;

    /* Label button (name + status badge) */
    lv_coord_t lbl_y = TANK_TOTAL_H + LABEL_BTN_GAP;
    lv_obj_t *label_btn = lv_obj_create(cont);
    lv_obj_set_size(label_btn, col_w, LABEL_BTN_H);
    lv_obj_set_pos(label_btn, 0, lbl_y);
    lv_obj_set_style_bg_color(label_btn, COL_BASE, 0);
    lv_obj_set_style_border_color(label_btn, COL_BD, 0);
    lv_obj_set_style_border_width(label_btn, 1, 0);
    lv_obj_set_style_radius(label_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(label_btn, 8, 0);
    lv_obj_set_style_pad_ver(label_btn, 0, 0);
    lv_obj_set_style_shadow_width(label_btn, 0, 0);
    lv_obj_clear_flag(label_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(label_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(label_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(label_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(label_btn, 3, 0);
    lv_obj_add_event_cb(label_btn, label_btn_cb, LV_EVENT_SHORT_CLICKED,
                        (void *)(intptr_t)idx);

    lv_obj_t *name_lbl = lv_label_create(label_btn);
    lv_label_set_text(name_lbl, t->name);
    lv_obj_set_style_text_font(name_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(name_lbl, COL_TX, 0);
    lv_obj_set_width(name_lbl, col_w - 16);
    lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);

    // Status pill — tinted background matching the level state
    lv_obj_t *pill = lv_obj_create(label_btn);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, status_bg(pct), 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(status_bd(pct)), 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_radius(pill, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_ver(pill, 2, 0);
    lv_obj_set_style_pad_hor(pill, 8, 0);
    lv_obj_set_style_shadow_width(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    col->badge_pill = pill;

    lv_obj_t *badge_lbl = lv_label_create(pill);
    lv_label_set_text(badge_lbl, status_text(pct));
    lv_obj_set_style_text_font(badge_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(badge_lbl, status_color(pct), 0);
    lv_obj_center(badge_lbl);
    col->badge_label = badge_lbl;
}

// ─── Public ────────────────────────────────────────────────────────────────────
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

    /* Empty state — config without tanks */
    if (g_tank_count == 0) {
        lv_obj_t *panel = lv_obj_create(s_screen);
        lv_obj_set_size(panel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_pad_all(panel, 0, 0);
        lv_obj_set_style_pad_gap(panel, 10, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_center(panel);

        extern const lv_image_dsc_t tank_icon;
        lv_obj_t *icon = lv_image_create(panel);
        lv_image_set_src(icon, &tank_icon);
        lv_obj_set_style_image_recolor(icon, COL_TX3, 0);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);

        lv_obj_t *title = lv_label_create(panel);
        lv_label_set_text(title, "No tanks in this configuration");
        lv_obj_set_style_text_font(title, FONT_SANS_SM, 0);
        lv_obj_set_style_text_color(title, COL_TX2, 0);

        lv_obj_t *hint = lv_label_create(panel);
        lv_label_set_text(hint, "Switch config template from the Home screen");
        lv_obj_set_style_text_font(hint, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(hint, COL_TX3, 0);

        ESP_LOGW(TAG, "tanks page has zero tanks — empty state shown");
        return;
    }

    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(s_screen, COL_GAP, 0);

    for (int i = 0; i < g_tank_count; i++) {
        build_tank_col(s_screen, i);
    }

    /* Force layout so lv_obj_get_width on labels returns correct values */
    lv_obj_update_layout(s_screen);
    for (int i = 0; i < g_tank_count; i++) {
        position_tick_labels(i);
    }

    s_refresh_timer = lv_timer_create(refresh_timer_cb, TANK_REFRESH_MS, NULL);
    lv_timer_pause(s_refresh_timer);

    ESP_LOGI(TAG, "Tanks screen created (%d tanks)", g_tank_count);
}

void ui_tanks_activate(void) {
    if (s_refresh_timer) lv_timer_resume(s_refresh_timer);
    for (int i = 0; i < g_tank_count; i++) refresh_tank(i);
}

void ui_tanks_deactivate(void) {
    if (s_refresh_timer) lv_timer_pause(s_refresh_timer);
}
