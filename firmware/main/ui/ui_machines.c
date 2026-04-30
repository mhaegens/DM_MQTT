// ui_machines.c — 2×2 machine card grid.
// Each card: header (name + type + status dot), body (sparkline, arc gauges,
// metric tiles), footer (Start/Stop + Pulse buttons).
// Tapping any metric opens ui_push_detail(machine.name, metric.label).

#include "ui_machines.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data.h"
#include "mqtt_task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "machines";

// ─── Grid geometry ────────────────────────────────────────────────────────────
// Content area: CONTENT_W × CONTENT_H (720 × 584)
// 2 columns, 2 rows, 10px gap, 14px outer padding
#define GRID_PAD   14
#define GRID_GAP   10
#define CARD_W     ((CONTENT_W - 2 * GRID_PAD - GRID_GAP) / 2)
#define CARD_H     ((CONTENT_H - 2 * GRID_PAD - GRID_GAP) / 2)

// Card internal layout
#define HDR_H      56    // header row height
#define FTR_H      52    // footer row height
#define BODY_H     (CARD_H - HDR_H - FTR_H - 2) // 2px border lines
#define DOT_D      14    // status dot diameter
#define ARC_W      72
#define ARC_H      50
#define SPARK_H    40

// ─── Persistent widget references (for refresh / pulse) ───────────────────────
static lv_obj_t *s_screen                  = NULL;
static lv_obj_t *s_cards[MACHINE_COUNT]    = {NULL};
static lv_obj_t *s_dots[MACHINE_COUNT]     = {NULL};  // status dot
static lv_obj_t *s_toggle_btns[MACHINE_COUNT] = {NULL}; // Start/Stop button
static lv_obj_t *s_toggle_lbls[MACHINE_COUNT] = {NULL}; // button label
static lv_obj_t *s_charts[MACHINE_COUNT]   = {NULL};  // sparkline chart
static lv_chart_series_t *s_series[MACHINE_COUNT] = {NULL};

// ─── Event callback context types ─────────────────────────────────────────────
typedef struct { int machine; int metric; } MetricCtx;
typedef struct { int machine; }             MachCtx;

// ─── Forward declarations ─────────────────────────────────────────────────────
static void build_machine_card(lv_obj_t *grid, int idx);
static void update_card_border(int idx);

// ─── Flash restore timer callback ────────────────────────────────────────────
static void flash_restore_cb(lv_timer_t *timer) {
    lv_obj_t *card = (lv_obj_t *)lv_timer_get_user_data(timer);
    lv_obj_set_style_bg_color(card, COL_BASE, 0);
    lv_timer_delete(timer);
}

// ─── Toggle (Start/Stop) callback ─────────────────────────────────────────────
static void toggle_cb(lv_event_t *e) {
    MachCtx *ctx = (MachCtx *)lv_event_get_user_data(e);
    int idx = ctx->machine;
    Machine *mc = &g_machines[idx];

    mc->running = !mc->running;
    mqtt_pub_machine_status(mc->id, mc->running);

    // Update button appearance
    if (mc->running) {
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

    // Update status dot color
    lv_obj_set_style_bg_color(s_dots[idx], mc->running ? COL_OK : COL_ERR, 0);

    // Update card border color
    update_card_border(idx);
}

// ─── Pulse callback ───────────────────────────────────────────────────────────
static void pulse_cb(lv_event_t *e) {
    MachCtx *ctx = (MachCtx *)lv_event_get_user_data(e);
    int idx = ctx->machine;
    Machine *mc = &g_machines[idx];

    mc->pulses++;
    mqtt_pub_machine_pulse(mc->id, mc->pulses);

    // Brief visual flash: temporarily brighten card bg, restore after 200ms
    lv_obj_set_style_bg_color(s_cards[idx], COL_HI_LT, 0);
    lv_timer_t *t = lv_timer_create(flash_restore_cb, 200, s_cards[idx]);
    lv_timer_set_repeat_count(t, 1);
}

// ─── Metric tile / arc tap callback ──────────────────────────────────────────
static void metric_tap_cb(lv_event_t *e) {
    MetricCtx *ctx = (MetricCtx *)lv_event_get_user_data(e);
    Machine *mc = &g_machines[ctx->machine];
    Metric  *m  = &mc->metrics[ctx->metric];
    ui_push_detail(mc->name, m->label);
}

// ─── Update card border color (running = green, stopped = red) ───────────────
static void update_card_border(int idx) {
    Machine *mc = &g_machines[idx];
    lv_color_t bd_col = mc->running ? lv_color_hex(COL_OK_BD) : lv_color_hex(COL_ERR_BD);
    lv_obj_set_style_border_color(s_cards[idx], bd_col, 0);
}

// ─── Build one metric tile (for non-SPARK, non-ARC metrics) ──────────────────
static void build_metric_tile(lv_obj_t *parent, int midx, int kidx, Metric *m) {
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_height(tile, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_style_bg_color(tile, COL_BASE2, 0);
    lv_obj_set_style_border_color(tile, COL_BD, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(tile, 8, 0);
    lv_obj_set_style_pad_ver(tile, 6, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(tile, 2, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    // Key label (caption)
    lv_obj_t *key_lbl = lv_label_create(tile);
    lv_label_set_text(key_lbl, m->label);
    lv_obj_set_style_text_font(key_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(key_lbl, COL_TX3, 0);

    // Value label
    lv_color_t val_col = lv_color_hex(metric_color(m));
    lv_obj_t *val_lbl = lv_label_create(tile);
    char val_str[24];
    snprintf(val_str, sizeof(val_str), "%.1f", m->value);
    lv_label_set_text(val_lbl, val_str);
    lv_obj_set_style_text_font(val_lbl, FONT_MONO_XS, 0);
    lv_obj_set_style_text_color(val_lbl, val_col, 0);

    // Unit label
    lv_obj_t *unit_lbl = lv_label_create(tile);
    lv_label_set_text(unit_lbl, m->unit);
    lv_obj_set_style_text_font(unit_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(unit_lbl, COL_TX3, 0);

    // Tap to open detail
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    MetricCtx *ctx = malloc(sizeof(MetricCtx));
    ctx->machine = midx;
    ctx->metric  = kidx;
    lv_obj_add_event_cb(tile, metric_tap_cb, LV_EVENT_SHORT_CLICKED, ctx);
}

// ─── Build arc gauge widget for one VIZ_ARC metric ───────────────────────────
static void build_arc_widget(lv_obj_t *parent, int midx, int kidx, Metric *m) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, ARC_W + 4, ARC_H + 18);  // arc + value label below
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(cont, 2, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Arc widget
    lv_obj_t *arc = lv_arc_create(cont);
    lv_obj_set_size(arc, ARC_W, ARC_H);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_bg_angles(arc, 135, 45);   // 270° sweep
    lv_arc_set_range(arc, (int16_t)m->min_val, (int16_t)m->max_val);
    lv_arc_set_value(arc, (int16_t)m->value);

    // BG arc: gray
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xE0E4EF), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);

    // Indicator arc: metric threshold color
    lv_color_t arc_col = lv_color_hex(metric_color(m));
    lv_obj_set_style_arc_color(arc, arc_col, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);

    // Hide knob
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    // Remove clickability from the arc itself
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    // Make container tappable → open detail
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    MetricCtx *ctx = malloc(sizeof(MetricCtx));
    ctx->machine = midx;
    ctx->metric  = kidx;
    lv_obj_add_event_cb(cont, metric_tap_cb, LV_EVENT_SHORT_CLICKED, ctx);

    // Value label below arc
    lv_obj_t *val_lbl = lv_label_create(cont);
    char val_str[24];
    snprintf(val_str, sizeof(val_str), "%.0f %s", m->value, m->unit);
    lv_label_set_text(val_lbl, val_str);
    lv_obj_set_style_text_font(val_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(val_lbl, arc_col, 0);
    lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_CENTER, 0);
}

// ─── Build one machine card ────────────────────────────────────────────────────
static void build_machine_card(lv_obj_t *grid, int idx) {
    Machine *mc = &g_machines[idx];

    // ── Card shell ────────────────────────────────────────────────────────────
    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, COL_BASE, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card,
        mc->running ? lv_color_hex(COL_OK_BD) : lv_color_hex(COL_ERR_BD), 0);
    lv_obj_set_style_radius(card, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_cards[idx] = card;

    // Flex column: header / body / footer
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_STRETCH, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(card, 0, 0);

    // ── Header ────────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(card);
    lv_obj_set_size(hdr, LV_PCT(100), HDR_H);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, COL_BD, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_pad_hor(hdr, 12, 0);
    lv_obj_set_style_pad_ver(hdr, 8, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // Row layout: name+type column on left, dot on right
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left: name + type stacked
    lv_obj_t *title_col = lv_obj_create(hdr);
    lv_obj_set_size(title_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_col, 0, 0);
    lv_obj_set_style_pad_all(title_col, 0, 0);
    lv_obj_set_flex_flow(title_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(title_col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(title_col, 2, 0);
    lv_obj_clear_flag(title_col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_lbl = lv_label_create(title_col);
    lv_label_set_text(name_lbl, mc->name);
    lv_obj_set_style_text_font(name_lbl, FONT_SANS_MD, 0);
    lv_obj_set_style_text_color(name_lbl, COL_TX, 0);

    lv_obj_t *type_lbl = lv_label_create(title_col);
    lv_label_set_text(type_lbl, mc->type);
    lv_obj_set_style_text_font(type_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(type_lbl, COL_TX3, 0);

    // Right: status dot 14×14 circle
    lv_obj_t *dot = lv_obj_create(hdr);
    lv_obj_set_size(dot, DOT_D, DOT_D);
    lv_obj_set_style_bg_color(dot, mc->running ? COL_OK : COL_ERR, 0);
    lv_obj_set_style_radius(dot, DOT_D / 2, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    s_dots[idx] = dot;

    // ── Body ──────────────────────────────────────────────────────────────────
    lv_obj_t *body = lv_obj_create(card);
    lv_obj_set_size(body, LV_PCT(100), BODY_H);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_hor(body, 10, 0);
    lv_obj_set_style_pad_ver(body, 8, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_STRETCH, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(body, 6, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // ── Sparkline (first VIZ_SPARK metric with history) ──────────────────────
    int spark_idx = -1;
    for (int k = 0; k < mc->metric_count; k++) {
        if (mc->metrics[k].viz == VIZ_SPARK && mc->metrics[k].history_len > 0) {
            spark_idx = k;
            break;
        }
    }

    if (spark_idx >= 0) {
        Metric *sm = &mc->metrics[spark_idx];
        lv_color_t spark_col = lv_color_hex(sm->color ? sm->color : COL_HI);

        // Sparkline row: chart + current value label
        lv_obj_t *spark_row = lv_obj_create(body);
        lv_obj_set_size(spark_row, LV_PCT(100), SPARK_H + 4);
        lv_obj_set_style_bg_opa(spark_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spark_row, 0, 0);
        lv_obj_set_style_pad_all(spark_row, 0, 0);
        lv_obj_set_flex_flow(spark_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(spark_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(spark_row, 6, 0);
        lv_obj_clear_flag(spark_row, LV_OBJ_FLAG_SCROLLABLE);

        // Chart fills the rest of the row
        lv_obj_t *chart = lv_chart_create(spark_row);
        lv_obj_set_flex_grow(chart, 1);
        lv_obj_set_height(chart, SPARK_H);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, HISTORY_LEN);
        lv_chart_set_div_line_count(chart, 0, 0);
        lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);
        lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
        // No point markers
        lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

        lv_chart_series_t *ser = lv_chart_add_series(chart, spark_col,
                                                      LV_CHART_AXIS_PRIMARY_Y);
        for (int i = 0; i < sm->history_len; i++) {
            lv_chart_set_next_value(chart, ser, (lv_value_prec_t)sm->history[i]);
        }
        lv_chart_refresh(chart);

        s_charts[idx] = chart;
        s_series[idx] = ser;

        // Make chart tappable
        lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);
        MetricCtx *ctx = malloc(sizeof(MetricCtx));
        ctx->machine = idx;
        ctx->metric  = spark_idx;
        lv_obj_add_event_cb(chart, metric_tap_cb, LV_EVENT_SHORT_CLICKED, ctx);

        // Current value label on right
        lv_obj_t *cur_lbl = lv_label_create(spark_row);
        char val_str[20];
        snprintf(val_str, sizeof(val_str), "%.0f", sm->value);
        lv_label_set_text(cur_lbl, val_str);
        lv_obj_set_style_text_font(cur_lbl, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(cur_lbl, spark_col, 0);
    }

    // ── Arc gauges row ────────────────────────────────────────────────────────
    // Count VIZ_ARC metrics
    int arc_count = 0;
    for (int k = 0; k < mc->metric_count; k++) {
        if (mc->metrics[k].viz == VIZ_ARC) arc_count++;
    }

    if (arc_count > 0) {
        lv_obj_t *arc_row = lv_obj_create(body);
        lv_obj_set_size(arc_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(arc_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(arc_row, 0, 0);
        lv_obj_set_style_pad_all(arc_row, 0, 0);
        lv_obj_set_flex_flow(arc_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(arc_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(arc_row, 8, 0);
        lv_obj_clear_flag(arc_row, LV_OBJ_FLAG_SCROLLABLE);

        for (int k = 0; k < mc->metric_count; k++) {
            if (mc->metrics[k].viz == VIZ_ARC) {
                build_arc_widget(arc_row, idx, k, &mc->metrics[k]);
            }
        }
    }

    // ── Tile metrics row (non-SPARK, non-ARC) ─────────────────────────────────
    int tile_count = 0;
    for (int k = 0; k < mc->metric_count; k++) {
        VizType v = mc->metrics[k].viz;
        if (v != VIZ_SPARK && v != VIZ_ARC) tile_count++;
    }

    if (tile_count > 0) {
        lv_obj_t *tile_row = lv_obj_create(body);
        lv_obj_set_size(tile_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(tile_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile_row, 0, 0);
        lv_obj_set_style_pad_all(tile_row, 0, 0);
        lv_obj_set_flex_flow(tile_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tile_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_STRETCH, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(tile_row, 6, 0);
        lv_obj_clear_flag(tile_row, LV_OBJ_FLAG_SCROLLABLE);

        for (int k = 0; k < mc->metric_count; k++) {
            VizType v = mc->metrics[k].viz;
            if (v != VIZ_SPARK && v != VIZ_ARC) {
                build_metric_tile(tile_row, idx, k, &mc->metrics[k]);
            }
        }
    }

    // ── Footer ────────────────────────────────────────────────────────────────
    lv_obj_t *ftr = lv_obj_create(card);
    lv_obj_set_size(ftr, LV_PCT(100), FTR_H);
    lv_obj_set_style_bg_opa(ftr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ftr, 0, 0);
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

    // Allocate persistent MachCtx (one per machine, reused by both buttons)
    MachCtx *mctx = malloc(sizeof(MachCtx));
    mctx->machine = idx;

    MachCtx *mctx2 = malloc(sizeof(MachCtx));
    mctx2->machine = idx;

    // Stop / Start button
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

    // Pulse button (ghost style)
    lv_obj_t *pulse_btn = lv_obj_create(ftr);
    lv_obj_set_size(pulse_btn, LV_SIZE_CONTENT, FTR_H - 12);
    lv_obj_set_flex_grow(pulse_btn, 1);
    lv_obj_set_style_bg_color(pulse_btn, COL_BASE, 0);
    lv_obj_set_style_border_color(pulse_btn, COL_BD_STR, 0);
    lv_obj_set_style_border_width(pulse_btn, 1, 0);
    lv_obj_set_style_radius(pulse_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(pulse_btn, 14, 0);
    lv_obj_set_style_pad_ver(pulse_btn, 0, 0);
    lv_obj_clear_flag(pulse_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pulse_lbl = lv_label_create(pulse_btn);
    lv_label_set_text(pulse_lbl, "Pulse");
    lv_obj_set_style_text_font(pulse_lbl, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(pulse_lbl, COL_TX2, 0);
    lv_obj_center(pulse_lbl);

    lv_obj_add_flag(pulse_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pulse_btn, pulse_cb, LV_EVENT_SHORT_CLICKED, mctx2);
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
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    g_screens[VIEW_MACHINES] = s_screen;

    // 2×2 grid container
    lv_obj_t *grid = lv_obj_create(s_screen);
    lv_obj_set_size(grid, CONTENT_W - 2 * GRID_PAD, CONTENT_H - 2 * GRID_PAD);
    lv_obj_set_pos(grid, GRID_PAD, GRID_PAD);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    // Use flex wrap for 2-column layout
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(grid, GRID_GAP, 0);

    for (int i = 0; i < MACHINE_COUNT; i++) {
        build_machine_card(grid, i);
    }
}

// ─── Public: refresh all arc / tile values from g_machines ───────────────────
// NOTE: sparkline and arc widgets are rebuilt by ui_machines_create at boot;
// for live data the caller should call ui_machines_refresh() which updates
// visible label text and chart data.
void ui_machines_refresh(void) {
    // Refresh status dots and toggle button labels / colors to match current state
    for (int i = 0; i < MACHINE_COUNT; i++) {
        Machine *mc = &g_machines[i];
        if (!s_dots[i] || !s_toggle_btns[i]) continue;

        // Dot color
        lv_obj_set_style_bg_color(s_dots[i], mc->running ? COL_OK : COL_ERR, 0);

        // Card border
        update_card_border(i);

        // Toggle button
        if (mc->running) {
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

        // Push new sparkline point if chart exists
        if (s_charts[i] && s_series[i]) {
            int spark_idx = -1;
            for (int k = 0; k < mc->metric_count; k++) {
                if (mc->metrics[k].viz == VIZ_SPARK && mc->metrics[k].history_len > 0) {
                    spark_idx = k;
                    break;
                }
            }
            if (spark_idx >= 0) {
                lv_chart_set_next_value(s_charts[i], s_series[i],
                    (lv_value_prec_t)mc->metrics[spark_idx].value);
                lv_chart_refresh(s_charts[i]);
            }
        }
    }
}

// ─── Public: pulse the first running machine ──────────────────────────────────
void ui_machines_pulse_first(void) {
    for (int i = 0; i < MACHINE_COUNT; i++) {
        if (g_machines[i].running) {
            g_machines[i].pulses++;
            mqtt_pub_machine_pulse(g_machines[i].id, g_machines[i].pulses);

            // Flash card background briefly
            if (s_cards[i]) {
                lv_obj_set_style_bg_color(s_cards[i], COL_HI_LT, 0);
                lv_timer_t *t = lv_timer_create(flash_restore_cb, 200, s_cards[i]);
                lv_timer_set_repeat_count(t, 1);
            }
            ESP_LOGI(TAG, "Pulse: %s pulses=%d", g_machines[i].id, g_machines[i].pulses);
            return;
        }
    }
}
