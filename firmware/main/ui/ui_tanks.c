// ui_tanks.c — Dynamic hopper-shaped tanks (1–3): cylindrical top + tapered bottom.
// Rendered via LV_EVENT_DRAW_MAIN with lv_draw_rect and lv_draw_triangle.
#include "ui_tanks.h"
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
#define TANK_UPPER_RATIO 70   // % of TANK_TOTAL_H = cylindrical section
#define TANK_TIP_W      28    // flat base width at bottom of taper (px)
#define TANK_RADIUS     36    // top-corner border radius
#define TANK_TIP_RADIUS  8    // rounded corner radius at taper tip
#define LABEL_BTN_H     48    // tank name / badge button height (2 rows)
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

#define DRAG_PUB_INTERVAL_MS 400

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

// ─── Hopper shape helpers ──────────────────────────────────────────────────────

// Width of the hopper at fill_px pixels from the bottom.
// At fill_px=0: TANK_TIP_W; at fill_px=taper_h: full column width.
static lv_coord_t hopper_width_at_fill(lv_coord_t col_w, lv_coord_t taper_h,
                                        lv_coord_t fill_px) {
    if (fill_px <= 0)          return TANK_TIP_W;
    if (fill_px >= taper_h)    return col_w;
    return TANK_TIP_W + (col_w - TANK_TIP_W) * fill_px / taper_h;
}

// Draw a filled rectangle with no border.
static void draw_filled_rect(lv_layer_t *layer,
                              lv_coord_t x1, lv_coord_t y1,
                              lv_coord_t x2, lv_coord_t y2,
                              lv_color_t color, lv_opa_t opa, int32_t radius) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color    = color;
    dsc.bg_opa      = opa;
    dsc.border_width = 0;
    dsc.radius      = radius;
    lv_area_t area = {x1, y1, x2, y2};
    lv_draw_rect(layer, &dsc, &area);
}

// Draw a trapezoid as two triangles (top_x1..top_x2 at top_y, narrowing to
// bot_x1..bot_x2 at bot_y). bot_radius > 0 adds rounded corners at the bottom.
static void draw_taper(lv_layer_t *layer,
                        lv_coord_t top_x1, lv_coord_t top_x2,
                        lv_coord_t bot_x1, lv_coord_t bot_x2,
                        lv_coord_t top_y,  lv_coord_t bot_y,
                        lv_color_t color,  lv_opa_t opa,
                        int32_t bot_radius) {
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa   = opa;

    lv_coord_t tri_bot_y = (bot_radius > 0) ? bot_y - bot_radius : bot_y;

    if (tri_bot_y > top_y) {
        // Triangle 1: top-left, top-right, bottom-right
        dsc.p[0].x = top_x1; dsc.p[0].y = top_y;
        dsc.p[1].x = top_x2; dsc.p[1].y = top_y;
        dsc.p[2].x = bot_x2; dsc.p[2].y = tri_bot_y;
        lv_draw_triangle(layer, &dsc);

        // Triangle 2: top-left, bottom-left, bottom-right
        dsc.p[0].x = top_x1; dsc.p[0].y = top_y;
        dsc.p[1].x = bot_x1; dsc.p[1].y = tri_bot_y;
        dsc.p[2].x = bot_x2; dsc.p[2].y = tri_bot_y;
        lv_draw_triangle(layer, &dsc);
    }

    if (bot_radius > 0) {
        draw_filled_rect(layer, bot_x1, tri_bot_y, bot_x2, bot_y,
                         color, opa, bot_radius);
    }
}

// ─── Custom draw callback ──────────────────────────────────────────────────────
static void tank_draw_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;

    lv_obj_t   *obj   = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    tank_col_t *col   = (tank_col_t *)lv_obj_get_user_data(obj);
    if (!col) return;

    Tank  *t   = &g_tanks[col->tank_idx];
    float  pct = tank_level(t);
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    lv_color_t fc = lv_color_hex(tank_fill_color(pct));

    lv_area_t  coords;
    lv_obj_get_coords(obj, &coords);
    lv_coord_t ox      = coords.x1;
    lv_coord_t oy      = coords.y1;
    lv_coord_t w       = lv_obj_get_width(obj);
    lv_coord_t h       = TANK_TOTAL_H;
    lv_coord_t upper_h = h * TANK_UPPER_RATIO / 100;
    lv_coord_t taper_h = h - upper_h;
    lv_coord_t tip_x1  = ox + (w - TANK_TIP_W) / 2;
    lv_coord_t tip_x2  = ox + (w + TANK_TIP_W) / 2;
    lv_coord_t bot_y   = oy + h;
    lv_coord_t jct_y   = oy + upper_h;

    /* 1. Background: upper rect with rounded top + 1px border */
    {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color    = COL_BASE2;
        dsc.bg_opa      = LV_OPA_COVER;
        dsc.border_color = COL_BD_STR;
        dsc.border_width = 1;
        dsc.border_opa  = LV_OPA_COVER;
        dsc.border_side = LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_TOP;
        dsc.radius      = TANK_RADIUS;
        lv_area_t area = {ox, oy, ox + w - 1, jct_y};
        lv_draw_rect(layer, &dsc, &area);
    }

    /* 2. Background: taper (covers the bottom border of the rect above) */
    draw_taper(layer, ox, ox + w, tip_x1, tip_x2, jct_y, bot_y,
               COL_BASE2, LV_OPA_COVER, TANK_TIP_RADIUS);

    /* 3. Fill */
    lv_coord_t fill_px = (lv_coord_t)(pct * (float)h / 100.0f);
    if (fill_px > 0) {
        lv_coord_t waterline = oy + h - fill_px;

        if (waterline >= jct_y) {
            /* Fill only inside taper */
            lv_coord_t fw   = hopper_width_at_fill(w, taper_h, fill_px);
            lv_coord_t ftx1 = ox + (w - fw) / 2;
            lv_coord_t ftx2 = ox + (w + fw) / 2;
            draw_taper(layer, ftx1, ftx2, tip_x1, tip_x2,
                       waterline, bot_y, fc, LV_OPA_COVER, TANK_TIP_RADIUS);
        } else {
            /* Fill spans rect + full taper */
            lv_coord_t fill_r = (waterline <= oy + TANK_RADIUS) ? TANK_RADIUS : 0;
            draw_filled_rect(layer, ox, waterline, ox + w - 1, jct_y + fill_r,
                             fc, LV_OPA_COVER, fill_r);
            draw_taper(layer, ox, ox + w, tip_x1, tip_x2,
                       jct_y, bot_y, fc, LV_OPA_COVER, TANK_TIP_RADIUS);
        }

        /* Shine strip on left side of fill */
        lv_coord_t shine_y1 = waterline + 4;
        lv_coord_t shine_y2 = bot_y - 6;
        if (shine_y2 > shine_y1) {
            draw_filled_rect(layer, ox + 3, shine_y1, ox + 9, shine_y2,
                             lv_color_white(), LV_OPA_20, SZ_RADIUS_SM);
        }
    }

    /* 4. Taper outline: diagonal lines for the slanted edges — drawn after fill */
    {
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.color = COL_BD_STR;
        line.width = 1;
        line.opa   = LV_OPA_COVER;
        line.p1.x = ox;       line.p1.y = jct_y;
        line.p2.x = tip_x1;   line.p2.y = bot_y;
        lv_draw_line(layer, &line);
        line.p1.x = ox + w;   line.p1.y = jct_y;
        line.p2.x = tip_x2;   line.p2.y = bot_y;
        lv_draw_line(layer, &line);
    }

    /* 5. Tick lines at 25%, 50%, 75% fill marks */
    static const int TICK_PCTS[] = {75, 50, 25};
    for (int i = 0; i < 3; i++) {
        lv_coord_t tick_y_off = h * (100 - TICK_PCTS[i]) / 100;
        lv_coord_t tick_y     = oy + tick_y_off;
        lv_coord_t depth      = tick_y_off - upper_h;
        lv_coord_t tick_w;
        if (depth <= 0) {
            tick_w = w;
        } else {
            tick_w = hopper_width_at_fill(w, taper_h, taper_h - depth);
        }
        lv_coord_t tx1 = ox + (w - tick_w) / 2;
        lv_draw_rect_dsc_t tdsc;
        lv_draw_rect_dsc_init(&tdsc);
        tdsc.bg_color    = lv_color_hex(0xC0C8D8);
        tdsc.bg_opa      = LV_OPA_COVER;
        tdsc.border_width = 0;
        tdsc.radius      = 0;
        lv_area_t tick_area = {tx1, tick_y, tx1 + tick_w - 1, tick_y + 1};
        lv_draw_rect(layer, &tdsc, &tick_area);
    }
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

    lv_coord_t fill_px  = (lv_coord_t)(pct * TANK_TOTAL_H / 100.0f);
    lv_coord_t fill_mid = TANK_TOTAL_H - fill_px / 2;
    lv_coord_t lbl_h    = lv_obj_get_height(col->pct_label);
    lv_coord_t lbl_w    = lv_obj_get_width(col->pct_label);
    lv_coord_t col_w    = col->col_w;
    lv_coord_t label_y  = fill_mid - lbl_h / 2;
    if (label_y > TANK_TOTAL_H - lbl_h - 2) label_y = TANK_TOTAL_H - lbl_h - 2;
    if (label_y < 0) label_y = 0;
    lv_obj_set_pos(col->pct_label, (col_w - lbl_w) / 2, label_y);

    if (col->badge_label) {
        lv_label_set_text(col->badge_label, status_text(pct));
        lv_obj_set_style_text_color(col->badge_label, status_color(pct), 0);
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
static void position_tick_labels(int col_idx) {
    tank_col_t *col    = &s_cols[col_idx];
    lv_coord_t  w      = col->col_w;
    lv_coord_t  upper_h = TANK_TOTAL_H * TANK_UPPER_RATIO / 100;
    lv_coord_t  taper_h = TANK_TOTAL_H - upper_h;

    static const int TICK_PCTS[] = {75, 50, 25};
    for (int i = 0; i < 3; i++) {
        lv_coord_t tick_y_off = TANK_TOTAL_H * (100 - TICK_PCTS[i]) / 100;
        lv_coord_t depth      = tick_y_off - upper_h;
        lv_coord_t tick_w;
        if (depth <= 0) {
            tick_w = w;
        } else {
            tick_w = hopper_width_at_fill(w, taper_h, taper_h - depth);
        }
        lv_coord_t right_edge = (w + tick_w) / 2;
        lv_coord_t lbl_w      = lv_obj_get_width(col->tick_labels[i]);
        lv_coord_t lbl_h      = lv_obj_get_height(col->tick_labels[i]);
        lv_obj_set_pos(col->tick_labels[i],
                       right_edge - lbl_w - 3,
                       tick_y_off - lbl_h / 2);
    }
}

// ─── Build one tank column ─────────────────────────────────────────────────────
static void build_tank_col(lv_obj_t *parent, int idx) {
    Tank      *t    = &g_tanks[idx];
    lv_coord_t col_w = (g_tank_count > 0)
        ? (CONTENT_W - COL_GAP * (g_tank_count + 1)) / g_tank_count
        : CONTENT_W - 2 * COL_GAP;
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

    ESP_LOGI(TAG, "Tanks screen created");
}

void ui_tanks_activate(void) {
    if (s_refresh_timer) lv_timer_resume(s_refresh_timer);
    for (int i = 0; i < g_tank_count; i++) refresh_tank(i);
}

void ui_tanks_deactivate(void) {
    if (s_refresh_timer) lv_timer_pause(s_refresh_timer);
}
