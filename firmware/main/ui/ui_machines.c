// ui_machines.c — dynamic scrollable machine card grid (up to MAX_MACHINES).
// Each card: header (name + type + status dot), body (sparkline + metric
// cells sized to fill the card), footer (Start/Stop + live pulse counter).
// The grid adapts to the machine count: 1 machine = one full-area card,
// 2 machines = two full-height columns, 3+ = the standard 2×2 grid.
// Tapping any metric opens ui_push_detail(machine.name, metric.label).
//
// Cell styling mirrors the HTML PoC (sapdm-demo-ui-v7.html): arc metrics are
// compact open-bottom gauges (198°–342°) with the value in the opening, other
// metrics are left-aligned label/value/unit tiles, and rows too short for a
// stacked tile collapse to a single label——value line. Labels are pinned to
// one line (dotted, never word-wrapped). Indicator size caps scale with the
// optional "ui.indicator_scale" percent in the config file.

#include "ui_machines.h"
#include "src/misc/cache/instance/lv_image_cache.h"
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
#include <ctype.h>
#include <math.h>

static const char *TAG = "machines";

// ─── String helpers ───────────────────────────────────────────────────────────
static void to_upper_str(const char *src, char *dst, size_t max) {
    if (!src || !dst || max == 0) return;
    size_t i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = toupper((unsigned char)src[i]);
    dst[i] = 0;
}

static void fmt_value(float v, char *buf, size_t n) {
    if (fabsf(v - roundf(v)) < 0.05f)
        snprintf(buf, n, "%.0f", v);
    else
        snprintf(buf, n, "%.1f", v);
}

// ─── Grid geometry ────────────────────────────────────────────────────────────
// Content area: CONTENT_W × CONTENT_H (720 × 584)
#define GRID_PAD   14
#define GRID_GAP   10

// Card internal layout
#define HDR_H      56    // header row height
#define FTR_H      48    // footer row height
#define DOT_D      14    // status dot diameter
#define CELL_GAP    6    // gap between body rows and metric cells

// Metric cell layout — proportions match the HTML PoC machine cards
#define LBL_H          20   // one FONT_MONO_XXS line — fixed height so labels dot, never wrap
#define NAME_H         26   // one FONT_SANS_SM line (card name)
#define CELLS_PER_ROW   3   // the PoC caps metric tiles at 3 per row
#define ARC_D_MAX      96   // gauge bounding-box cap at indicator_scale 100
#define ARC_D_MIN      44   // below this a gauge is unreadable — render a tile instead
#define SPARK_H_MAX   132   // sparkline strip cap at indicator_scale 100
#define SPARK_H_MIN    48   // header row + minimal chart
#define TILE_COL_MIN   64   // row height below which cells collapse to one line
#define ROW_MIN        40   // cell-row minimum the spark strip must leave room for

// Apply the config file's "ui.indicator_scale" percent to a pixel size
static int scaled(int px) { return px * g_ui_indicator_scale / 100; }

// Card dimensions adapt to the machine count (computed in ui_machines_create)
static int s_grid_cols = 2;
static int s_card_w    = 0;
static int s_card_h    = 0;
static int s_body_h    = 0;   // card height minus header/footer/borders

// ─── Persistent widget references (for refresh) ───────────────────────────────
static lv_obj_t *s_screen                  = NULL;
static lv_obj_t *s_cards[MAX_MACHINES]    = {NULL};
static lv_obj_t *s_dots[MAX_MACHINES]     = {NULL};  // status dot
static lv_obj_t *s_toggle_btns[MAX_MACHINES] = {NULL}; // Start/Stop button
static lv_obj_t *s_toggle_lbls[MAX_MACHINES] = {NULL}; // button label
static lv_obj_t *s_charts[MAX_MACHINES]   = {NULL};  // sparkline chart
static lv_chart_series_t *s_series[MAX_MACHINES] = {NULL};
static lv_obj_t *s_pulse_lbls[MAX_MACHINES] = {NULL}; // footer pulse counter
static int       s_prev_pulses[MAX_MACHINES];

// History last rendered into each sparkline — refresh rewrites the chart only
// when the data actually moved, so idle ticks stop invalidating (and
// ghost-retaking) every visible card. Advanced only when the widget is
// updated, so a card scrolled back into view catches up on the next tick.
static float     s_hist_shadow[MAX_MACHINES][HISTORY_LEN];
static int       s_hist_shadow_len[MAX_MACHINES] = {0};

// Per-machine, per-metric value labels and arc widgets (for live refresh)
static lv_obj_t *s_metric_val_lbls[MAX_MACHINES][MAX_MACH_METRICS] = {{NULL}};
static lv_obj_t *s_metric_arcs[MAX_MACHINES][MAX_MACH_METRICS]     = {{NULL}};
static bool      s_last_render_running[MAX_MACHINES];  // last state drawn, skip redundant restyles

// Last-rendered metric value/color — refresh skips a metric entirely when
// nothing changed, so an idle tick no longer restyles and invalidates every
// visible label and arc.
static float     s_prev_val[MAX_MACHINES][MAX_MACH_METRICS];
static uint32_t  s_prev_col[MAX_MACHINES][MAX_MACH_METRICS];
static bool      s_prev_valid[MAX_MACHINES][MAX_MACH_METRICS];

// ─── Event callback context types ─────────────────────────────────────────────
typedef struct { int machine; int metric; } MetricCtx;
typedef struct { int machine; }             MachCtx;

// ─── Forward declarations ─────────────────────────────────────────────────────
static void build_machine_card(lv_obj_t *grid, int idx);
static void update_card_border(int idx);
static void style_status_dot(lv_obj_t *dot, bool running);
static void apply_card_visibility(void);

// ─── Ghost scroll ─────────────────────────────────────────────────────────────
// A full-viewport scroll frame dirties everything under the finger, so direct
// mode degenerates to a complete software repaint (charts, arcs, dozens of
// rounded rects) per frame — the source of the machines-tab scroll jank.
// Instead, every card is pre-rendered once into an opaque RGB565 image in
// PSRAM; on LV_EVENT_SCROLL_BEGIN the live cards swap for those images (a
// straight row-copy blit each), and on LV_EVENT_SCROLL_END they swap back.
// Snapshots are retaken off-gesture whenever a card's pixels change, so the
// swap is never visible. Kill switch: UI_SCROLL_GHOSTS in config.h.
#if UI_SCROLL_GHOSTS
static lv_obj_t      *s_ghost_imgs[MAX_MACHINES] = {NULL};
static lv_draw_buf_t *s_ghost_bufs[MAX_MACHINES] = {NULL};
static bool           s_ghost_dirty[MAX_MACHINES];
static bool           s_ghost_mode   = false;
static bool           s_ghosts_active = false;  // grid taller than viewport?

// Composite an ARGB8888 snapshot over the page background into RGB565. The
// pixels outside the card's rounded corners must show COL_BG, and an opaque
// RGB565 image blits with no per-pixel blending during scroll.
static lv_draw_buf_t *ghost_composite(const lv_draw_buf_t *src) {
    const uint32_t w = src->header.w, h = src->header.h;
    lv_draw_buf_t *dst = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, 0);
    if (!dst) return NULL;
    const uint32_t bg_r = 0xF2, bg_g = 0xF4, bg_b = 0xFA;   // COL_BG 0xF2F4FA
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *sp = src->data + y * src->header.stride;
        uint16_t      *dp = (uint16_t *)(dst->data + y * dst->header.stride);
        for (uint32_t x = 0; x < w; x++, sp += 4) {
            uint32_t b = sp[0], g = sp[1], r = sp[2], a = sp[3];  // lv_color32_t
            if (a != 255) {
                r = (r * a + bg_r * (255 - a) + 127) / 255;
                g = (g * a + bg_g * (255 - a) + 127) / 255;
                b = (b * a + bg_b * (255 - a) + 127) / 255;
            }
            dp[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
    return dst;
}

// Re-render card idx into its ghost image. Runs on the LVGL task, never
// mid-gesture (callers are refresh/tap paths, all off-scroll).
static bool ghost_retake_now(int idx) {
    if (!s_ghosts_active || s_ghost_mode) return false;
    if (!s_cards[idx] || !s_ghost_imgs[idx]) return false;
    if (lv_obj_get_width(s_cards[idx]) <= 0) return false;   // layout not resolved yet

    // Snapshot renders regardless of viewport position, but not while HIDDEN
    // (scroll culling) — lift the flag for the render, restore it after.
    bool was_hidden = lv_obj_has_flag(s_cards[idx], LV_OBJ_FLAG_HIDDEN);
    if (was_hidden) lv_obj_clear_flag(s_cards[idx], LV_OBJ_FLAG_HIDDEN);
    lv_draw_buf_t *argb = lv_snapshot_take(s_cards[idx], LV_COLOR_FORMAT_ARGB8888);
    if (was_hidden) lv_obj_add_flag(s_cards[idx], LV_OBJ_FLAG_HIDDEN);
    if (!argb) return false;

    lv_draw_buf_t *rgb = ghost_composite(argb);
    lv_draw_buf_destroy(argb);
    if (!rgb) return false;

    // Snapshot includes any ext-draw margin — recenter the image on the card
    // slot so a size mismatch cannot shift the ghost.
    lv_obj_set_pos(s_ghost_imgs[idx],
                   (idx % s_grid_cols) * (s_card_w + GRID_GAP) - ((int)rgb->header.w - s_card_w) / 2,
                   (idx / s_grid_cols) * (s_card_h + GRID_GAP) - ((int)rgb->header.h - s_card_h) / 2);
    lv_image_set_src(s_ghost_imgs[idx], rgb);

    lv_draw_buf_t *old = s_ghost_bufs[idx];
    s_ghost_bufs[idx] = rgb;
    if (old) {
        lv_image_cache_drop(old);   // the decoder may hold the old pointer
        lv_draw_buf_destroy(old);
    }
    return true;
}

static void ghost_retake(int idx) {
    if (s_ghosts_active && idx >= 0 && idx < g_machine_count) s_ghost_dirty[idx] = true;
}

static void ghost_cache_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (g_current_view != VIEW_MACHINES || s_ghost_mode || lv_obj_is_scrolling(s_screen)) return;
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in))
        if (lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED) return;
    // At most one expensive snapshot per idle pass; navigation and taps never
    // build ten cards' snapshots synchronously. Dirty/missing ghosts stay live.
    for (int i = 0; i < g_machine_count; i++) {
        if (!s_ghost_dirty[i]) continue;
        s_ghost_dirty[i] = !ghost_retake_now(i);
        break;
    }
}

static void ghost_mode_enter(void) {
    if (!s_ghosts_active || s_ghost_mode) return;
    // Backfill any snapshot that is still missing (first scroll after an
    // allocation failure) — normally all exist and this is free.
    for (int i = 0; i < g_machine_count; i++)
        if (!s_ghost_bufs[i]) ghost_retake(i);
    s_ghost_mode = true;
    apply_card_visibility();
}

static void ghost_mode_exit(void) {
    if (!s_ghost_mode) return;
    s_ghost_mode = false;
    for (int i = 0; i < g_machine_count; i++)
        if (s_ghost_imgs[i]) lv_obj_add_flag(s_ghost_imgs[i], LV_OBJ_FLAG_HIDDEN);
    apply_card_visibility();
}

static void machines_scroll_begin_cb(lv_event_t *e) {
    (void)e;
    if (g_current_view == VIEW_MACHINES) ghost_mode_enter();
}

static void machines_scroll_end_cb(lv_event_t *e) {
    (void)e;
    ghost_mode_exit();
}
#else
static void ghost_retake(int idx) { (void)idx; }
#endif  // UI_SCROLL_GHOSTS

// ─── Toggle (Start/Stop) callback ─────────────────────────────────────────────
static void toggle_cb(lv_event_t *e) {
    MachCtx *ctx = (MachCtx *)lv_event_get_user_data(e);
    int idx = ctx->machine;
    Machine *mc = &g_machines[idx];

    DATA_LOCK();
    mc->running = !mc->running;
    bool running = mc->running;
    DATA_UNLOCK();
    s_last_render_running[idx] = running;
    ESP_LOGI(TAG, "Toggle: %s -> %s", mc->id, running ? "running" : "stopped");
    mqtt_pub_machine_status(mc->id, running);

    // Update button appearance
    if (running) {
        lv_obj_set_style_bg_color(s_toggle_btns[idx], COL_ERR_LT, 0);
        lv_obj_set_style_border_color(s_toggle_btns[idx], lv_color_hex(COL_ERR_BD), 0);
        lv_obj_set_style_text_color(s_toggle_lbls[idx], COL_ERR, 0);
        lv_label_set_text(s_toggle_lbls[idx], "Stop");
    } else {
        lv_obj_set_style_bg_color(s_toggle_btns[idx], COL_OK_LT, 0);
        lv_obj_set_style_border_color(s_toggle_btns[idx], lv_color_hex(COL_OK_BD), 0);
        lv_obj_set_style_text_color(s_toggle_lbls[idx], COL_OK, 0);
        lv_label_set_text(s_toggle_lbls[idx], "Start");
    }

    // Update status dot color + ring
    style_status_dot(s_dots[idx], running);

    // Update card border color
    update_card_border(idx);

    ghost_retake(idx);
}

// ─── Metric cell tap callback ─────────────────────────────────────────────────
static void metric_tap_cb(lv_event_t *e) {
    MetricCtx *ctx = (MetricCtx *)lv_event_get_user_data(e);
    ui_detail_open_machine_metric(ctx->machine, ctx->metric);
}

// ─── Update card border color (running = green, stopped = red) ───────────────
static void update_card_border(int idx) {
    Machine *mc = &g_machines[idx];
    lv_color_t bd_col = mc->running ? lv_color_hex(COL_OK_BD) : lv_color_hex(COL_ERR_BD);
    lv_obj_set_style_border_color(s_cards[idx], bd_col, 0);
}

// ─── Status dot fill + soft ring (the PoC dot glows; a tinted ring here) ──────
static void style_status_dot(lv_obj_t *dot, bool running) {
    lv_obj_set_style_bg_color(dot, running ? COL_OK : COL_ERR, 0);
    lv_obj_set_style_outline_color(dot, running ? COL_OK_LT : COL_ERR_LT, 0);
}

// ─── Sparkline scaling ────────────────────────────────────────────────────────
// The chart Y axis is a fixed 0–1000 and every point is normalised into a
// window hugging the history's min/max — same auto-zoom the PoC uses, so a
// 2000–7000 RPM walk draws as a full-height wiggle. (Previously the values
// went in raw against LVGL's default 0–100 range: anything larger clipped
// flat against the strip edge, anything smaller hugged the bottom.)
static void spark_window(const float *h, int len, float *mn_out, float *mx_out) {
    float lo = h[0], hi = h[0];
    for (int i = 1; i < len; i++) {
        if (h[i] < lo) lo = h[i];
        if (h[i] > hi) hi = h[i];
    }
    float span = hi - lo;
    if (span < 1e-3f) span = (fabsf(hi) > 1.0f) ? fabsf(hi) * 0.1f : 1.0f;
    *mn_out = lo - span * 0.08f;
    *mx_out = hi + span * 0.10f;
}

static void spark_fill_chart(lv_obj_t *chart, lv_chart_series_t *ser,
                             const float *h, int len) {
    if (len < 1 || len > HISTORY_LEN) return;
    float mn, mx;
    spark_window(h, len, &mn, &mx);
    float k = 1000.0f / (mx - mn);
    for (int i = 0; i < HISTORY_LEN; i++) {
        int j = i - (HISTORY_LEN - len);   // newest sample lands rightmost
        lv_chart_set_value_by_id(chart, ser, i,
            j < 0 ? LV_CHART_POINT_NONE
                  : (int32_t)((h[j] - mn) * k));
    }
    lv_chart_refresh(chart);
}

// ─── One-line metric key label ────────────────────────────────────────────────
// Fixed single-line height so LV_LABEL_LONG_DOT truncates with dots — with a
// content-sized height the label word-wraps instead ("SPINDLE LOAD" used to
// break mid-word over three lines). Caller sets the width.
static lv_obj_t *make_key_label(lv_obj_t *parent, const char *text) {
    char up[48];
    to_upper_str(text, up, sizeof(up));
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(lbl, COL_TX3, 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);
    lv_obj_set_height(lbl, LBL_H);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl, up);
    return lbl;
}

// ─── Build one metric cell ────────────────────────────────────────────────────
// Cells share a row and grow equally. Three renderings, picked from the space
// the cell actually gets (all styled after the HTML PoC):
//  · gauge    — VIZ_ARC with room: compact 198°–342° open-bottom arc, label
//               above, value in the opening (PoC .arctile)
//  · tile     — label / value / unit stacked, left-aligned (PoC .mtile)
//  · one-line — label left, value+unit right, for rows too short to stack
static void build_metric_cell(lv_obj_t *parent, int midx, int kidx, Metric *m,
                              int cell_h, int cell_w) {
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_set_height(cell, LV_PCT(100));
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_style_bg_color(cell, COL_BASE2, 0);
    lv_obj_set_style_border_color(cell, COL_BD, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_radius(cell, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

    lv_color_t val_col = lv_color_hex(metric_color(m));
    lv_obj_t *val_lbl = NULL;
    char val_str[32];
    fmt_value(m->value, val_str, sizeof(val_str));

    // Gauge box the cell can afford. The drawn part of the open-bottom gauge —
    // value text included — ends at ~0.76·d; the rest of the square arc widget
    // is empty, so the height budget divides by 0.76 rather than 1.
    int avail = cell_h - 8 - LBL_H - 2;            // pads + label + gap
    int d = (avail - 12) * 100 / 76;               // 12 ≈ value-text descent
    if (d > cell_w - 12)        d = cell_w - 12;
    if (d > scaled(ARC_D_MAX))  d = scaled(ARC_D_MAX);

    if (cell_h < TILE_COL_MIN) {
        // ── One-line cell: LABEL ······ value unit ───────────────────────────
        lv_obj_set_style_pad_hor(cell, 8, 0);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(cell, 4, 0);

        lv_obj_t *key_lbl = make_key_label(cell, m->label);
        lv_obj_set_flex_grow(key_lbl, 1);

        val_lbl = lv_label_create(cell);
        lv_label_set_text(val_lbl, val_str);
        lv_obj_set_style_text_font(val_lbl, FONT_SANS_XS, 0);
        lv_obj_set_style_text_color(val_lbl, val_col, 0);

        if (m->unit[0]) {
            lv_obj_t *unit_lbl = lv_label_create(cell);
            lv_label_set_text(unit_lbl, m->unit);
            lv_obj_set_style_text_font(unit_lbl, FONT_MONO_XXS, 0);
            lv_obj_set_style_text_color(unit_lbl, COL_TX3, 0);
        }
    } else if (m->viz == VIZ_ARC && d >= ARC_D_MIN) {
        // ── Gauge cell — manual placement, group vertically centred ──────────
        int content_h = LBL_H + 2 + d * 76 / 100 + 12;
        int top = (cell_h - content_h) / 2;
        if (top < 4) top = 4;

        lv_obj_t *key_lbl = make_key_label(cell, m->label);
        lv_obj_set_width(key_lbl, cell_w - 14);
        lv_obj_set_style_text_align(key_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(key_lbl, LV_ALIGN_TOP_MID, 0, top);

        lv_obj_t *arc = lv_arc_create(cell);
        lv_obj_set_size(arc, d, d);
        lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, top + LBL_H + 2);
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
        lv_arc_set_bg_angles(arc, 198, 342);   // 144° over the top, open bottom
        lv_arc_set_range(arc, (int32_t)m->min_val, (int32_t)m->max_val);
        lv_arc_set_value(arc, (int32_t)m->value);

        int stroke = d / 9;
        if (stroke < 5)  stroke = 5;
        if (stroke > 10) stroke = 10;

        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0xE0E4EF), LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, stroke, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

        lv_obj_set_style_arc_color(arc, val_col, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, stroke, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

        // Value in the gauge opening, just below centre (no unit)
        val_lbl = lv_label_create(arc);
        lv_label_set_text(val_lbl, val_str);
        lv_obj_set_style_text_font(val_lbl,
            d >= 84 ? FONT_SANS_SM : (d >= 56 ? FONT_SANS_XS : FONT_MONO_XXS), 0);
        lv_obj_set_style_text_color(val_lbl, val_col, 0);
        lv_obj_align(val_lbl, LV_ALIGN_CENTER, 0, d * 26 / 100);

        if (midx < MAX_MACHINES && kidx < MAX_MACH_METRICS)
            s_metric_arcs[midx][kidx] = arc;
    } else {
        // ── Stacked tile: label / value / unit, left-aligned ─────────────────
        lv_obj_set_style_pad_hor(cell, 10, 0);
        lv_obj_set_style_pad_ver(cell, 6, 0);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_gap(cell, 2, 0);

        lv_obj_t *key_lbl = make_key_label(cell, m->label);
        lv_obj_set_width(key_lbl, LV_PCT(100));

        val_lbl = lv_label_create(cell);
        lv_label_set_text(val_lbl, val_str);
        lv_obj_set_style_text_font(val_lbl,
            cell_h >= 110 ? FONT_SANS_MD : FONT_SANS_SM, 0);
        lv_obj_set_style_text_color(val_lbl, val_col, 0);

        if (m->unit[0]) {
            lv_obj_t *unit_lbl = lv_label_create(cell);
            lv_label_set_text(unit_lbl, m->unit);
            lv_obj_set_style_text_font(unit_lbl, FONT_MONO_XXS, 0);
            lv_obj_set_style_text_color(unit_lbl, COL_TX3, 0);
        }
    }

    // Tap to open detail
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    MetricCtx *ctx = malloc(sizeof(MetricCtx));
    ctx->machine = midx;
    ctx->metric  = kidx;
    lv_obj_add_event_cb(cell, metric_tap_cb, LV_EVENT_SHORT_CLICKED, ctx);

    if (midx < MAX_MACHINES && kidx < MAX_MACH_METRICS)
        s_metric_val_lbls[midx][kidx] = val_lbl;
}

// ─── Build one machine card ────────────────────────────────────────────────────
static void build_machine_card(lv_obj_t *grid, int idx) {
    Machine *mc = &g_machines[idx];

    // ── Card shell ────────────────────────────────────────────────────────────
    // Cards are positioned absolutely (fixed grid). Flex ROW_WRAP is
    // deliberately NOT used: flex skips LV_OBJ_FLAG_HIDDEN children, so the
    // scroll-culling in apply_card_visibility() would reflow the whole grid.
    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_size(card, s_card_w, s_card_h);
    lv_obj_set_pos(card, (idx % s_grid_cols) * (s_card_w + GRID_GAP),
                         (idx / s_grid_cols) * (s_card_h + GRID_GAP));
    lv_obj_set_style_bg_color(card, COL_BASE, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card,
        mc->running ? lv_color_hex(COL_OK_BD) : lv_color_hex(COL_ERR_BD), 0);
    lv_obj_set_style_radius(card, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_cards[idx] = card;
    s_last_render_running[idx] = mc->running;   // card is built in this state

    // Flex column: header / body / footer
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(card, 0, 0);

    // ── Header ────────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(card);
    lv_obj_set_size(hdr, LV_PCT(100), HDR_H);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, COL_BD, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_pad_hor(hdr, 12, 0);
    lv_obj_set_style_pad_ver(hdr, 6, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // Row layout: name+type column on left, dot on right
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left: name + type stacked. The column takes all width up to the dot and
    // both labels are pinned to one line each (dotted), so a long name or type
    // can never wrap onto extra lines or collide with the status dot.
    lv_obj_t *title_col = lv_obj_create(hdr);
    lv_obj_set_height(title_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(title_col, 1);
    lv_obj_set_style_bg_opa(title_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_col, 0, 0);
    lv_obj_set_style_pad_all(title_col, 0, 0);
    lv_obj_set_style_pad_right(title_col, 8, 0);   // clearance to the dot ring
    lv_obj_set_flex_flow(title_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(title_col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(title_col, 0, 0);
    lv_obj_clear_flag(title_col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_lbl = lv_label_create(title_col);
    lv_obj_set_style_text_font(name_lbl, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(name_lbl, COL_TX, 0);
    lv_obj_set_size(name_lbl, LV_PCT(100), NAME_H);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(name_lbl, mc->name);

    // Machine type — uppercase small caps line, as in the PoC
    lv_obj_t *type_lbl = make_key_label(title_col, mc->type);
    lv_obj_set_width(type_lbl, LV_PCT(100));

    // Right: status dot with a soft ring in the state tint
    lv_obj_t *dot = lv_obj_create(hdr);
    lv_obj_set_size(dot, DOT_D, DOT_D);
    lv_obj_set_style_radius(dot, DOT_D / 2, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_outline_width(dot, 3, 0);
    lv_obj_set_style_outline_pad(dot, 0, 0);
    lv_obj_set_style_outline_opa(dot, LV_OPA_COVER, 0);
    style_status_dot(dot, mc->running);
    s_dots[idx] = dot;

    // ── Body ──────────────────────────────────────────────────────────────────
    lv_obj_t *body = lv_obj_create(card);
    lv_obj_set_size(body, LV_PCT(100), s_body_h);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_hor(body, 10, 0);
    lv_obj_set_style_pad_ver(body, 6, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(body, CELL_GAP, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // ── Deterministic fill layout ─────────────────────────────────────────────
    // The body height is known, so the sparkline strip and the metric cell
    // rows split it exactly — a card with two metrics fills the same area as
    // a card with five, just with larger widgets.
    int inner_w = s_card_w - 4 - 20;   // card border 2×2 + body pad_hor 10×2
    int inner_h = s_body_h - 12;       // body pad_ver 6×2

    // First VIZ_SPARK metric with history becomes the sparkline strip;
    // every other metric becomes a cell (arc for VIZ_ARC, tile otherwise).
    int spark_idx = -1;
    for (int k = 0; k < mc->metric_count; k++) {
        if (mc->metrics[k].viz == VIZ_SPARK && mc->metrics[k].history_len > 0) {
            spark_idx = k;
            break;
        }
    }
    int n_cells = 0;
    for (int k = 0; k < mc->metric_count; k++)
        if (k != spark_idx) n_cells++;

    // Cells pack at most CELLS_PER_ROW per row (as the PoC does) — four
    // metrics in a single row left ~74 px per cell and unreadable labels.
    int rows = 0, per_row = 0;
    if (n_cells > 0) {
        rows    = (n_cells + CELLS_PER_ROW - 1) / CELLS_PER_ROW;
        per_row = (n_cells + rows - 1) / rows;
    }

    int spark_h = 0, cells_h = 0;
    if (spark_idx >= 0 && n_cells > 0) {
        spark_h = inner_h * 2 / 5;
        int spark_max = scaled(SPARK_H_MAX);
        if (spark_h > spark_max) spark_h = spark_max;
        // The strip yields before any cell row drops below ROW_MIN
        int cells_min = rows * ROW_MIN + (rows - 1) * CELL_GAP;
        if (inner_h - spark_h - CELL_GAP < cells_min)
            spark_h = inner_h - CELL_GAP - cells_min;
        if (spark_h < SPARK_H_MIN) spark_h = SPARK_H_MIN;
        cells_h = inner_h - spark_h - CELL_GAP;
    } else if (spark_idx >= 0) {
        spark_h = inner_h;             // sparkline-only machine: full body
    } else {
        cells_h = inner_h;             // no sparkline: cells get everything
    }

    // ── Sparkline strip ───────────────────────────────────────────────────────
    if (spark_idx >= 0) {
        Metric *sm = &mc->metrics[spark_idx];
        lv_color_t spark_col = sm->color ? lv_color_hex(sm->color) : COL_HI;

        lv_obj_t *spark_box = lv_obj_create(body);
        lv_obj_set_size(spark_box, LV_PCT(100), spark_h);
        lv_obj_set_style_bg_color(spark_box, COL_BASE2, 0);
        lv_obj_set_style_border_width(spark_box, 1, 0);
        lv_obj_set_style_border_color(spark_box, COL_BD, 0);
        lv_obj_set_style_radius(spark_box, SZ_RADIUS_SM, 0);
        lv_obj_set_style_pad_hor(spark_box, 8, 0);
        lv_obj_set_style_pad_ver(spark_box, 4, 0);
        lv_obj_set_flex_flow(spark_box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(spark_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(spark_box, 2, 0);
        lv_obj_clear_flag(spark_box, LV_OBJ_FLAG_SCROLLABLE);

        // Header row: label left + current value right
        lv_obj_t *spark_hdr = lv_obj_create(spark_box);
        lv_obj_set_size(spark_hdr, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(spark_hdr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spark_hdr, 0, 0);
        lv_obj_set_style_pad_all(spark_hdr, 0, 0);
        lv_obj_set_flex_flow(spark_hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(spark_hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(spark_hdr, LV_OBJ_FLAG_SCROLLABLE);

        // Label yields to the value: it grows into the free space and dots
        // when the row runs out, so the two can never overlap.
        lv_obj_t *key_lbl = make_key_label(spark_hdr, sm->label);
        lv_obj_set_flex_grow(key_lbl, 1);

        lv_obj_t *cur_lbl = lv_label_create(spark_hdr);
        char val_str[20];
        fmt_value(sm->value, val_str, sizeof(val_str));
        lv_label_set_text(cur_lbl, val_str);
        lv_obj_set_style_text_font(cur_lbl, FONT_SANS_SM, 0);
        lv_obj_set_style_text_color(cur_lbl, spark_col, 0);

        // Live refresh updates this label like any other metric value
        if (idx < MAX_MACHINES && spark_idx < MAX_MACH_METRICS)
            s_metric_val_lbls[idx][spark_idx] = cur_lbl;

        // Chart — full width, fills the strip below the header row
        int chart_h = spark_h - 30;    // pads 8 + header 20 + gap 2
        if (chart_h < 16) chart_h = 16;
        lv_obj_t *chart = lv_chart_create(spark_box);
        lv_obj_set_size(chart, LV_PCT(100), chart_h);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, HISTORY_LEN);
        lv_chart_set_div_line_count(chart, 0, 0);
        lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);
        lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

        lv_chart_series_t *ser = lv_chart_add_series(chart, spark_col, LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
        spark_fill_chart(chart, ser, sm->history, sm->history_len);

        s_charts[idx] = chart;
        s_series[idx] = ser;
        memcpy(s_hist_shadow[idx], sm->history,
               (size_t)sm->history_len * sizeof(float));
        s_hist_shadow_len[idx] = sm->history_len;

        // Make entire spark_box tappable (not just the chart)
        lv_obj_add_flag(spark_box, LV_OBJ_FLAG_CLICKABLE);
        MetricCtx *ctx = malloc(sizeof(MetricCtx));
        ctx->machine = idx;
        ctx->metric  = spark_idx;
        lv_obj_add_event_cb(spark_box, metric_tap_cb, LV_EVENT_SHORT_CLICKED, ctx);
    }

    // ── Metric cell rows ──────────────────────────────────────────────────────
    if (n_cells > 0) {
        int row_h  = (cells_h - (rows - 1) * CELL_GAP) / rows;
        int cell_w = (inner_w - (per_row - 1) * CELL_GAP) / per_row;

        lv_obj_t *row = NULL;
        int built = 0;
        for (int k = 0; k < mc->metric_count; k++) {
            if (k == spark_idx) continue;
            if (built % per_row == 0) {
                row = lv_obj_create(body);
                lv_obj_set_size(row, LV_PCT(100), row_h);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_set_style_pad_gap(row, CELL_GAP, 0);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            }
            build_metric_cell(row, idx, k, &mc->metrics[k], row_h, cell_w);
            built++;
        }
    }

    // ── Footer ────────────────────────────────────────────────────────────────
    lv_obj_t *ftr = lv_obj_create(card);
    lv_obj_set_size(ftr, LV_PCT(100), FTR_H);
    lv_obj_set_style_bg_opa(ftr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(ftr, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(ftr, COL_BD, 0);
    lv_obj_set_style_border_width(ftr, 1, 0);
    lv_obj_set_style_pad_hor(ftr, 10, 0);
    lv_obj_set_style_pad_ver(ftr, 6, 0);
    lv_obj_set_flex_flow(ftr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ftr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ftr, 8, 0);
    lv_obj_clear_flag(ftr, LV_OBJ_FLAG_SCROLLABLE);

    // Stop / Start button — fills the footer
    MachCtx *mctx = malloc(sizeof(MachCtx));
    mctx->machine = idx;

    lv_obj_t *tog_btn = lv_obj_create(ftr);
    lv_obj_set_size(tog_btn, LV_SIZE_CONTENT, FTR_H - 12);
    lv_obj_set_style_radius(tog_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_border_width(tog_btn, 1, 0);
    lv_obj_set_style_pad_hor(tog_btn, 14, 0);
    lv_obj_set_style_pad_ver(tog_btn, 0, 0);
    lv_obj_set_flex_grow(tog_btn, 1);
    lv_obj_clear_flag(tog_btn, LV_OBJ_FLAG_SCROLLABLE);

    if (mc->running) {
        lv_obj_set_style_bg_color(tog_btn, COL_ERR_LT, 0);
        lv_obj_set_style_border_color(tog_btn, lv_color_hex(COL_ERR_BD), 0);
    } else {
        lv_obj_set_style_bg_color(tog_btn, COL_OK_LT, 0);
        lv_obj_set_style_border_color(tog_btn, lv_color_hex(COL_OK_BD), 0);
    }

    lv_obj_t *tog_lbl = lv_label_create(tog_btn);
    lv_label_set_text(tog_lbl, mc->running ? "Stop" : "Start");
    lv_obj_set_style_text_font(tog_lbl, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(tog_lbl, mc->running ? COL_ERR : COL_OK, 0);
    lv_obj_center(tog_lbl);

    lv_obj_add_flag(tog_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tog_btn, toggle_cb, LV_EVENT_SHORT_CLICKED, mctx);

    s_toggle_btns[idx] = tog_btn;
    s_toggle_lbls[idx] = tog_lbl;

    // Pulse counter — production count published to MQTT
    // (factory/machines/<id>/pulses); auto_pulse machines tick it up live.
    lv_obj_t *pulse_pill = lv_obj_create(ftr);
    lv_obj_set_size(pulse_pill, LV_SIZE_CONTENT, FTR_H - 12);
    lv_obj_set_style_bg_color(pulse_pill, COL_BASE2, 0);
    lv_obj_set_style_border_color(pulse_pill, COL_BD, 0);
    lv_obj_set_style_border_width(pulse_pill, 1, 0);
    lv_obj_set_style_radius(pulse_pill, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(pulse_pill, 10, 0);
    lv_obj_set_style_pad_ver(pulse_pill, 0, 0);
    lv_obj_clear_flag(pulse_pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *pulse_lbl = lv_label_create(pulse_pill);
    lv_label_set_text_fmt(pulse_lbl, "%d pulses", mc->pulses);
    lv_obj_set_style_text_font(pulse_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(pulse_lbl, COL_TX2, 0);
    lv_obj_center(pulse_lbl);

    s_pulse_lbls[idx]  = pulse_lbl;
    s_prev_pulses[idx] = mc->pulses;

#if UI_SCROLL_GHOSTS
    // Image proxy shown in place of the card during scroll gestures. Same slot
    // position; pixels arrive via ghost_retake() once the card has a layout.
    lv_obj_t *ghost = lv_image_create(grid);
    lv_obj_set_pos(ghost, (idx % s_grid_cols) * (s_card_w + GRID_GAP),
                          (idx / s_grid_cols) * (s_card_h + GRID_GAP));
    lv_obj_add_flag(ghost, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ghost, LV_OBJ_FLAG_CLICKABLE);
    s_ghost_imgs[idx] = ghost;
#endif
}

// ─── Viewport-based card hiding — hides cards outside the scroll viewport so ──
// LVGL skips them entirely during render (layout + draw cost = 0 when hidden)
static void apply_card_visibility(void) {
    if (!s_screen) return;
    // Screen has never been laid out yet (either still LV_OBJ_FLAG_HIDDEN or
    // pre-first-render): coords resolve to zero-extent and every card fails
    // the in_view check, permanently flagging the whole grid HIDDEN. Bail.
    if (lv_obj_has_flag(s_screen, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t sa;
    lv_obj_get_coords(s_screen, &sa);
    if (sa.y2 <= sa.y1) return;  // not laid out yet — leave cards visible
    for (int i = 0; i < g_machine_count; i++) {
        if (!s_cards[i]) continue;
        lv_area_t ca;
        lv_obj_get_coords(s_cards[i], &ca);   // scroll moves hidden children too — coords stay valid
        bool in_view = !(ca.y2 < sa.y1 || ca.y1 > sa.y2);
#if UI_SCROLL_GHOSTS
        // During a scroll the image proxy stands in for the live card; a card
        // whose snapshot failed keeps its widgets on screen instead.
        bool use_ghost = s_ghost_mode && s_ghost_imgs[i] && s_ghost_bufs[i] && !s_ghost_dirty[i];
        lv_obj_t *shown  = use_ghost ? s_ghost_imgs[i] : s_cards[i];
        lv_obj_t *other  = use_ghost ? s_cards[i]      : s_ghost_imgs[i];
        if (other) lv_obj_add_flag(other, LV_OBJ_FLAG_HIDDEN);
        if (in_view) lv_obj_clear_flag(shown, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(shown, LV_OBJ_FLAG_HIDDEN);
#else
        if (in_view) lv_obj_clear_flag(s_cards[i], LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag(s_cards[i],   LV_OBJ_FLAG_HIDDEN);
#endif
    }
}

static void machines_scroll_cb(lv_event_t *e) {
    (void)e;
    if (g_current_view != VIEW_MACHINES) return;
    apply_card_visibility();
}

// ─── Periodic refresh timer callback ─────────────────────────────────────────
static void machines_refresh_cb(lv_timer_t *t) {
    (void)t;
    if (g_current_view != VIEW_MACHINES) return;
    if (s_screen && lv_obj_is_scrolling(s_screen)) return;
#if UI_SCROLL_GHOSTS
    // Safety net: if a SCROLL_END was ever swallowed (e.g. a press killed the
    // momentum animation), drop back to live cards on the next idle tick.
    if (s_ghost_mode) ghost_mode_exit();
#endif
    ui_machines_refresh();
}

// ─── Public: create machines screen ──────────────────────────────────────────
void ui_machines_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, CONTENT_W, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    g_screens[VIEW_MACHINES] = s_screen;

    // ── Empty state — config without machines ────────────────────────────────
    if (g_machine_count == 0) {
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

        lv_obj_t *icon = lv_label_create(panel);
        lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(icon, FONT_SANS_XL, 0);
        lv_obj_set_style_text_color(icon, COL_TX3, 0);

        lv_obj_t *title = lv_label_create(panel);
        lv_label_set_text(title, "No machines in this configuration");
        lv_obj_set_style_text_font(title, FONT_SANS_SM, 0);
        lv_obj_set_style_text_color(title, COL_TX2, 0);

        lv_obj_t *hint = lv_label_create(panel);
        lv_label_set_text(hint, "Switch config template from the Home screen");
        lv_obj_set_style_text_font(hint, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(hint, COL_TX3, 0);

        ESP_LOGW(TAG, "machines page has zero cards — empty state shown");
        return;
    }

    // ── Grid geometry — adapt card size to the machine count ─────────────────
    s_grid_cols  = (g_machine_count <= 1) ? 1 : 2;
    int vis_rows = (g_machine_count <= 2) ? 1 : 2;
    s_card_w = (CONTENT_W - 2 * GRID_PAD - (s_grid_cols - 1) * GRID_GAP) / s_grid_cols;
    s_card_h = (CONTENT_H - 2 * GRID_PAD - (vis_rows - 1) * GRID_GAP) / vis_rows;
    s_body_h = s_card_h - HDR_H - FTR_H - 4;   // 4 = card border top+bottom

    int rows_total = (g_machine_count + s_grid_cols - 1) / s_grid_cols;
    int grid_h = rows_total * s_card_h + (rows_total - 1) * GRID_GAP;

    // Scrollable card grid. Cards are placed at fixed coordinates — no flex —
    // so hiding off-screen cards never reflows the layout (flex skips hidden
    // children; see build_machine_card).
    lv_obj_t *grid = lv_obj_create(s_screen);
    lv_obj_set_size(grid, CONTENT_W - 2 * GRID_PAD, grid_h + GRID_PAD);
    lv_obj_set_pos(grid, GRID_PAD, GRID_PAD);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_bottom(grid, GRID_PAD, 0);  // breathing room after last row
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE); // s_screen owns the scroll

    for (int i = 0; i < g_machine_count; i++) {
        build_machine_card(grid, i);
    }

    // Hook scroll events to show/hide off-screen cards (zero render cost when hidden)
    lv_obj_add_event_cb(s_screen, machines_scroll_cb, LV_EVENT_SCROLL, NULL);

#if UI_SCROLL_GHOSTS
    // Ghosts only earn their PSRAM when the grid can actually scroll
    s_ghosts_active = (GRID_PAD + grid_h + GRID_PAD) > CONTENT_H;
    if (s_ghosts_active) {
        lv_timer_create(ghost_cache_timer_cb, 40, NULL);
        lv_obj_add_event_cb(s_screen, machines_scroll_begin_cb, LV_EVENT_SCROLL_BEGIN, NULL);
        lv_obj_add_event_cb(s_screen, machines_scroll_end_cb,   LV_EVENT_SCROLL_END,   NULL);
    }
#endif

    lv_timer_create(machines_refresh_cb, UI_REFRESH_MS, NULL);

    // Cards start visible; ui_machines_activate() re-runs the viewport cull
    // once the screen is actually laid out on the display. Running it here,
    // while the screen is still LV_OBJ_FLAG_HIDDEN, would hide every card
    // permanently (zero-extent coords → nothing tests in-view).
    ESP_LOGI(TAG, "machines page: built %d cards (%dx%d px, %d col)",
             g_machine_count, s_card_w, s_card_h, s_grid_cols);
}

// ─── Public: called from ui_main on view enter ───────────────────────────────
// Force every card back to visible (recover from a stale HIDDEN state from
// a previous run of apply_card_visibility() before layout), then re-cull
// against the now-real screen coords.
void ui_machines_activate(void) {
    if (!s_screen) return;
#if UI_SCROLL_GHOSTS
    ghost_mode_exit();   // never enter the tab with proxies on screen
#endif
    for (int i = 0; i < g_machine_count; i++) {
        if (s_cards[i]) lv_obj_clear_flag(s_cards[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_update_layout(s_screen);
    apply_card_visibility();
#if UI_SCROLL_GHOSTS
    // Take the initial snapshots now that layout is resolved — off-gesture,
    // so the first scroll never pays for them.
    if (s_ghosts_active)
        for (int i = 0; i < g_machine_count; i++)
            if (!s_ghost_bufs[i]) ghost_retake(i);
#endif
}

// ─── Public: refresh all arc / tile values from g_machines ───────────────────
// NOTE: sparkline and arc widgets are rebuilt by ui_machines_create at boot;
// for live data the caller should call ui_machines_refresh() which updates
// visible label text and chart data.

// Per-machine value snapshot: copied under DATA_LOCK, rendered after release,
// so the sim task never blocks on LVGL work.
typedef struct {
    bool     running;
    int      pulses;
    int      metric_count;
    float    value[MAX_MACH_METRICS];
    uint32_t color[MAX_MACH_METRICS];
    int      spark_idx;   // -1 when the machine has no sparkline metric
    float    hist[HISTORY_LEN];   // spark metric history (auto-zoom rescale)
    int      hist_len;
} MachSnap;

// Set label text only when it changed — avoids invalidating unchanged regions
static bool label_set_if_changed(lv_obj_t *lbl, const char *text) {
    if (strcmp(lv_label_get_text(lbl), text) == 0) return false;
    lv_label_set_text(lbl, text);
    return true;
}

void ui_machines_refresh(void) {
    static MachSnap snap[MAX_MACHINES];
    bool card_changed[MAX_MACHINES] = {false};

    // Phase 1: snapshot values under the lock — no LVGL calls here
    DATA_LOCK();
    int count = g_machine_count;
    for (int i = 0; i < count; i++) {
        Machine  *mc = &g_machines[i];
        MachSnap *s  = &snap[i];
        s->running      = mc->running;
        s->pulses       = mc->pulses;
        s->metric_count = mc->metric_count < MAX_MACH_METRICS
                        ? mc->metric_count : MAX_MACH_METRICS;
        s->spark_idx    = -1;
        for (int k = 0; k < s->metric_count; k++) {
            Metric *m = &mc->metrics[k];
            s->value[k] = m->value;
            s->color[k] = metric_color(m);
            if (s->spark_idx < 0 && m->viz == VIZ_SPARK && m->history_len > 0)
                s->spark_idx = k;
        }
        s->hist_len = 0;
        if (s->spark_idx >= 0) {
            Metric *m = &mc->metrics[s->spark_idx];
            s->hist_len = m->history_len;
            memcpy(s->hist, m->history, (size_t)s->hist_len * sizeof(float));
        }
    }
    DATA_UNLOCK();

    // Phase 2: widget updates — sim task is free to run meanwhile
    for (int i = 0; i < count; i++) {
        MachSnap *s = &snap[i];
        if (!s_dots[i] || !s_toggle_btns[i]) continue;
        // Skip cards outside the visible viewport — lv_obj_is_visible() does not
        // account for scroll position, so we compare absolute coords directly
        if (s_cards[i] && s_screen) {
            lv_area_t ca, sa;
            lv_obj_get_coords(s_cards[i], &ca);
            lv_obj_get_coords(s_screen,   &sa);
            if (ca.y2 < sa.y1 || ca.y1 > sa.y2) continue;
        }

        // Dot / border / toggle restyle only on a state change
        if (s->running != s_last_render_running[i]) {
            s_last_render_running[i] = s->running;
            style_status_dot(s_dots[i], s->running);
            update_card_border(i);
            if (s->running) {
                lv_obj_set_style_bg_color(s_toggle_btns[i], COL_ERR_LT, 0);
                lv_obj_set_style_border_color(s_toggle_btns[i], lv_color_hex(COL_ERR_BD), 0);
                lv_obj_set_style_text_color(s_toggle_lbls[i], COL_ERR, 0);
                lv_label_set_text(s_toggle_lbls[i], "Stop");
            } else {
                lv_obj_set_style_bg_color(s_toggle_btns[i], COL_OK_LT, 0);
                lv_obj_set_style_border_color(s_toggle_btns[i], lv_color_hex(COL_OK_BD), 0);
                lv_obj_set_style_text_color(s_toggle_lbls[i], COL_OK, 0);
                lv_label_set_text(s_toggle_lbls[i], "Start");
            }
            card_changed[i] = true;
        }

        // Pulse counter — only redraw when the count moved
        if (s_pulse_lbls[i] && s->pulses != s_prev_pulses[i]) {
            s_prev_pulses[i] = s->pulses;
            lv_label_set_text_fmt(s_pulse_lbls[i], "%d pulses", s->pulses);
            card_changed[i] = true;
        }

        // Redraw the sparkline only when its history actually moved — the
        // whole strip is rewritten each time because the auto-zoom window
        // shifts with the data (see spark_fill_chart)
        if (s_charts[i] && s_series[i] && s->hist_len > 0 &&
            (s->hist_len != s_hist_shadow_len[i] ||
             memcmp(s_hist_shadow[i], s->hist,
                    (size_t)s->hist_len * sizeof(float)) != 0)) {
            spark_fill_chart(s_charts[i], s_series[i], s->hist, s->hist_len);
            memcpy(s_hist_shadow[i], s->hist, (size_t)s->hist_len * sizeof(float));
            s_hist_shadow_len[i] = s->hist_len;
            card_changed[i] = true;
        }

        // Update value labels and arc widgets — but only for metrics whose
        // value or threshold color actually moved since the last render, so an
        // idle tick doesn't restyle and invalidate every widget on screen
        for (int k = 0; k < s->metric_count; k++) {
            if (s_prev_valid[i][k] &&
                s->value[k] == s_prev_val[i][k] &&
                s->color[k] == s_prev_col[i][k]) continue;
            s_prev_val[i][k]   = s->value[k];
            s_prev_col[i][k]   = s->color[k];
            s_prev_valid[i][k] = true;

            lv_obj_t *vlbl = s_metric_val_lbls[i][k];
            if (vlbl) {
                char val_str[32];
                fmt_value(s->value[k], val_str, sizeof(val_str));
                if (label_set_if_changed(vlbl, val_str)) card_changed[i] = true;
                lv_obj_set_style_text_color(vlbl, lv_color_hex(s->color[k]), 0);
                card_changed[i] = true;
            }
            lv_obj_t *arc = s_metric_arcs[i][k];
            if (arc) {
                lv_arc_set_value(arc, (int32_t)s->value[k]);
                lv_obj_set_style_arc_color(arc, lv_color_hex(s->color[k]),
                                           LV_PART_INDICATOR);
                card_changed[i] = true;
            }
        }
    }

#if UI_SCROLL_GHOSTS
    // Re-render the ghost of every card whose pixels changed this tick, while
    // no gesture is active — the card↔ghost swap then never shows a jump.
    if (s_ghosts_active && !s_ghost_mode)
        for (int i = 0; i < count; i++)
            if (card_changed[i]) ghost_retake(i);
#else
    (void)card_changed;
#endif
}
