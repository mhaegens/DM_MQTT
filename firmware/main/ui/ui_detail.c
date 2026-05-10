// ui_detail.c — Detail overlay: header + viz card + step/slider + random gen.
#include "ui_detail.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ─── Static context ─────────────────────────────────────────────────────────
static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_entity_lbl = NULL;
static lv_obj_t *s_metric_lbl = NULL;
static lv_obj_t *s_value_lbl  = NULL;
static lv_obj_t *s_unit_lbl   = NULL;
static lv_obj_t *s_viz_card   = NULL;
static lv_obj_t *s_step_row   = NULL;
static lv_obj_t *s_step_val_lbl = NULL;
static lv_obj_t *s_slider_row = NULL;
static lv_obj_t *s_slider     = NULL;

// Viz child pointers (valid after build_viz(), cleared before rebuild)
static lv_obj_t          *s_viz_chart    = NULL;
static lv_chart_series_t *s_viz_series   = NULL;
static lv_obj_t          *s_viz_arc      = NULL;
static lv_obj_t          *s_viz_arc_lbl  = NULL;
static lv_obj_t          *s_viz_fill     = NULL;
static lv_obj_t          *s_viz_fill_outer = NULL;
static lv_obj_t          *s_viz_ph_bar   = NULL;
static lv_obj_t          *s_viz_ph_lbl   = NULL;
static lv_obj_t          *s_viz_bul_fill = NULL;
static lv_obj_t          *s_viz_bul_outer = NULL;

// Random generator
static lv_obj_t   *s_rand_start_btn   = NULL;
static lv_obj_t   *s_rand_start_lbl   = NULL;
static lv_obj_t   *s_interval_btns[5] = {NULL};
static lv_timer_t *s_rand_timer        = NULL;
static int         s_rand_interval_idx = 1;   // default 1s

static const int   s_intervals[]      = {500, 1000, 2000, 5000, 10000};
static const char *s_interval_labels[] = {"0.5s", "1s", "2s", "5s", "10s"};

// Metric context
static char  s_entity[32]      = "";
static char  s_metric[32]      = "";
static float s_value           = 0.0f;
static float s_step            = 1.0f;
static float s_min             = 0.0f;
static float s_max             = 100.0f;
static char  s_unit[16]        = "";
static uint32_t s_color        = 0x0070F2;
static VizType   s_viz         = VIZ_SPARK;
static InputType s_input_type  = INPUT_STEP;
static float     s_history[HISTORY_LEN];
static int       s_history_len = 0;
static bool      s_viz_needs_rebuild = true;
static int       s_slider_factor    = 1;

// Back-reference for data model updates
static int s_ctx_tank    = -1;
static int s_ctx_machine = -1;
static int s_ctx_metric  = -1;

// ─── Viz builder ─────────────────────────────────────────────────────────────

static void build_viz(void)
{
    if (!s_viz_card) return;
    lv_obj_clean(s_viz_card);

    // Reset viz child pointers
    s_viz_chart = NULL; s_viz_series = NULL;
    s_viz_arc = NULL; s_viz_arc_lbl = NULL;
    s_viz_fill = NULL; s_viz_fill_outer = NULL;
    s_viz_ph_bar = NULL; s_viz_ph_lbl = NULL;
    s_viz_bul_fill = NULL; s_viz_bul_outer = NULL;

    int cw = lv_obj_get_content_width(s_viz_card);
    int ch = lv_obj_get_content_height(s_viz_card);
    lv_color_t col = lv_color_hex(s_color);

    switch (s_viz) {

    case VIZ_SPARK: {
        lv_obj_t *chart = lv_chart_create(s_viz_card);
        lv_obj_set_size(chart, cw, ch);
        lv_obj_center(chart);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_div_line_count(chart, 0, 0);
        lv_chart_set_point_count(chart, HISTORY_LEN);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y,
                           (int32_t)s_min, (int32_t)(s_max > s_min ? s_max : s_min + 1));
        lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_obj_set_style_pad_all(chart, 0, 0);
        lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
        lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
        lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
        lv_obj_set_style_line_color(chart, col, LV_PART_ITEMS);
        lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_chart_series_t *ser = lv_chart_add_series(chart, col,
                                                      LV_CHART_AXIS_PRIMARY_Y);
        int n = s_history_len < HISTORY_LEN ? s_history_len : HISTORY_LEN;
        for (int i = 0; i < n; i++)
            lv_chart_set_value_by_id(chart, ser, i, (int32_t)s_history[i]);
        lv_chart_refresh(chart);

        s_viz_chart  = chart;
        s_viz_series = ser;
        break;
    }

    case VIZ_ARC: {
        int arc_sz = ch < cw ? ch : cw;
        arc_sz -= 20;
        if (arc_sz < 60) arc_sz = 60;

        lv_obj_t *arc = lv_arc_create(s_viz_card);
        lv_obj_set_size(arc, arc_sz, arc_sz);
        lv_obj_center(arc);
        lv_arc_set_bg_start_angle(arc, 135);
        lv_arc_set_bg_end_angle(arc, 45);
        lv_arc_set_range(arc, (int32_t)s_min, (int32_t)(s_max > s_min ? s_max : s_min + 1));
        lv_arc_set_value(arc, (int32_t)s_value);
        lv_obj_set_style_arc_color(arc, col, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, COL_BASE3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 12, LV_PART_MAIN);
        lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f\n%s", s_value, s_unit);
        lv_obj_t *lbl = lv_label_create(s_viz_card);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, FONT_MONO_MD, 0);
        lv_obj_set_style_text_color(lbl, col, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);

        s_viz_arc     = arc;
        s_viz_arc_lbl = lbl;
        break;
    }

    case VIZ_THERMO: {
        int tw = 60;
        int th = ch - 30;
        if (th < 40) th = 40;

        // Wrapper
        lv_obj_t *wrap = lv_obj_create(s_viz_card);
        lv_obj_remove_style_all(wrap);
        lv_obj_set_size(wrap, tw + 20, th + tw + 10);
        lv_obj_center(wrap);
        lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

        // Tube
        lv_obj_t *tube = lv_obj_create(wrap);
        lv_obj_set_size(tube, tw, th);
        lv_obj_set_pos(tube, 10, 0);
        lv_obj_set_style_bg_color(tube, COL_BASE2, 0);
        lv_obj_set_style_border_color(tube, COL_BD_STR, 0);
        lv_obj_set_style_border_width(tube, 1, 0);
        lv_obj_set_style_radius(tube, tw / 2, 0);
        lv_obj_set_style_clip_corner(tube, true, 0);
        lv_obj_set_style_pad_all(tube, 0, 0);
        lv_obj_set_style_shadow_width(tube, 0, 0);
        lv_obj_clear_flag(tube, LV_OBJ_FLAG_SCROLLABLE);
        s_viz_fill_outer = tube;

        float range = s_max - s_min;
        float pct   = range > 0 ? (s_value - s_min) / range : 0;
        int fill_h  = (int)((th - 2) * pct);
        if (fill_h < 0) fill_h = 0;

        lv_obj_t *fill = lv_obj_create(tube);
        lv_obj_set_size(fill, tw - 2, fill_h);
        lv_obj_set_pos(fill, 1, th - 2 - fill_h + 1);
        lv_obj_set_style_bg_color(fill, col, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, 0, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_set_style_shadow_width(fill, 0, 0);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        s_viz_fill = fill;

        // Bulb
        lv_obj_t *bulb = lv_obj_create(wrap);
        lv_obj_set_size(bulb, tw + 10, tw + 10);
        lv_obj_set_pos(bulb, 5, th - 5);
        lv_obj_set_style_radius(bulb, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bulb, col, 0);
        lv_obj_set_style_border_color(bulb, COL_BD_STR, 0);
        lv_obj_set_style_border_width(bulb, 1, 0);
        lv_obj_set_style_pad_all(bulb, 0, 0);
        lv_obj_set_style_shadow_width(bulb, 0, 0);
        lv_obj_clear_flag(bulb, LV_OBJ_FLAG_SCROLLABLE);
        break;
    }

    case VIZ_PH: {
        int bw = cw - 20;

        // Value label
        char buf[20];
        snprintf(buf, sizeof(buf), "pH %.1f", s_value);
        lv_obj_t *ph_lbl = lv_label_create(s_viz_card);
        lv_label_set_text(ph_lbl, buf);
        lv_obj_set_style_text_font(ph_lbl, FONT_MONO_MD, 0);
        lv_obj_set_style_text_color(ph_lbl, COL_PUR, 0);
        lv_obj_set_style_text_align(ph_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(ph_lbl, (cw - bw) / 2, 16);
        s_viz_ph_lbl = ph_lbl;

        // Gradient bar
        lv_obj_t *ph_bar = lv_obj_create(s_viz_card);
        lv_obj_set_size(ph_bar, bw, 48);
        lv_obj_set_pos(ph_bar, (cw - bw) / 2, ch / 2 - 24);
        lv_obj_set_style_bg_color(ph_bar, lv_color_hex(0xFF6B35), 0);
        lv_obj_set_style_bg_grad_color(ph_bar, lv_color_hex(0x0070F2), 0);
        lv_obj_set_style_bg_grad_dir(ph_bar, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(ph_bar, 0, 0);
        lv_obj_set_style_radius(ph_bar, SZ_RADIUS_SM, 0);
        lv_obj_set_style_clip_corner(ph_bar, true, 0);
        lv_obj_set_style_pad_all(ph_bar, 0, 0);
        lv_obj_set_style_shadow_width(ph_bar, 0, 0);
        lv_obj_clear_flag(ph_bar, LV_OBJ_FLAG_SCROLLABLE);
        s_viz_ph_bar = ph_bar;

        // Marker
        float range = s_max > s_min ? s_max - s_min : 14.0f;
        int pos_x   = (int)((s_value - s_min) / range * bw) - 1;
        if (pos_x < 0) pos_x = 0;
        if (pos_x > bw - 3) pos_x = bw - 3;
        lv_obj_t *marker = lv_obj_create(ph_bar);
        lv_obj_set_size(marker, 3, 48);
        lv_obj_set_pos(marker, pos_x, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(marker, 0, 0);
        lv_obj_set_style_radius(marker, 0, 0);
        lv_obj_set_style_pad_all(marker, 0, 0);
        lv_obj_set_style_shadow_width(marker, 0, 0);
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);

        // Acid / Alkaline labels
        lv_obj_t *acid_lbl = lv_label_create(s_viz_card);
        lv_label_set_text(acid_lbl, "Acid");
        lv_obj_set_style_text_font(acid_lbl, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(acid_lbl, lv_color_hex(0xFF6B35), 0);
        lv_obj_set_pos(acid_lbl, (cw - bw) / 2, ch / 2 + 32);

        lv_obj_t *alk_lbl = lv_label_create(s_viz_card);
        lv_label_set_text(alk_lbl, "Alkaline");
        lv_obj_set_style_text_font(alk_lbl, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(alk_lbl, lv_color_hex(0x0070F2), 0);
        lv_obj_set_pos(alk_lbl, (cw - bw) / 2 + bw - 64, ch / 2 + 32);
        break;
    }

    case VIZ_TANK: {
        int fw = 120;
        int fh = ch - 20;
        if (fh < 40) fh = 40;

        lv_obj_t *outer = lv_obj_create(s_viz_card);
        lv_obj_set_size(outer, fw, fh);
        lv_obj_align(outer, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(outer, COL_BASE2, 0);
        lv_obj_set_style_border_color(outer, COL_BD_STR, 0);
        lv_obj_set_style_border_width(outer, 1, 0);
        lv_obj_set_style_radius(outer, SZ_RADIUS_SM, 0);
        lv_obj_set_style_clip_corner(outer, true, 0);
        lv_obj_set_style_pad_all(outer, 0, 0);
        lv_obj_set_style_shadow_width(outer, 0, 0);
        lv_obj_clear_flag(outer, LV_OBJ_FLAG_SCROLLABLE);
        s_viz_fill_outer = outer;

        float pct = s_value;
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;
        int fill_h = (int)((fh - 2) * pct / 100.0f);

        lv_obj_t *fill = lv_obj_create(outer);
        lv_obj_set_size(fill, fw - 2, fill_h);
        lv_obj_set_pos(fill, 1, fh - 2 - fill_h + 1);
        lv_obj_set_style_bg_color(fill, lv_color_hex(tank_fill_color(pct)), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_80, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, 0, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_set_style_shadow_width(fill, 0, 0);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        s_viz_fill = fill;

        lv_obj_t *pct_lbl = lv_label_create(outer);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", (int)pct);
        lv_label_set_text(pct_lbl, buf);
        lv_obj_set_style_text_font(pct_lbl, FONT_MONO_MD, 0);
        lv_obj_set_style_text_color(pct_lbl, COL_BASE, 0);
        lv_obj_center(pct_lbl);
        break;
    }

    case VIZ_BULLET: {
        int bw = cw - 20;
        int bh = 40;

        lv_obj_t *outer = lv_obj_create(s_viz_card);
        lv_obj_set_size(outer, bw, bh);
        lv_obj_align(outer, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(outer, COL_BASE3, 0);
        lv_obj_set_style_border_width(outer, 0, 0);
        lv_obj_set_style_radius(outer, SZ_RADIUS_SM, 0);
        lv_obj_set_style_clip_corner(outer, true, 0);
        lv_obj_set_style_pad_all(outer, 0, 0);
        lv_obj_set_style_shadow_width(outer, 0, 0);
        lv_obj_clear_flag(outer, LV_OBJ_FLAG_SCROLLABLE);
        s_viz_bul_outer = outer;

        float range = s_max - s_min;
        float pct   = range > 0 ? (s_value - s_min) / range : 0;
        int fw = (int)(bw * pct);
        if (fw < 0) fw = 0;

        lv_obj_t *fill = lv_obj_create(outer);
        lv_obj_set_size(fill, fw, bh);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_style_bg_color(fill, col, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, 0, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_set_style_shadow_width(fill, 0, 0);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        s_viz_bul_fill = fill;

        // Marker lines at 60% and 80%
        for (int m = 60; m <= 80; m += 20) {
            int mx = bw * m / 100;
            lv_obj_t *mark = lv_obj_create(outer);
            lv_obj_set_size(mark, 2, bh);
            lv_obj_set_pos(mark, mx - 1, 0);
            lv_obj_set_style_bg_color(mark, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(mark, LV_OPA_60, 0);
            lv_obj_set_style_border_width(mark, 0, 0);
            lv_obj_set_style_radius(mark, 0, 0);
            lv_obj_set_style_pad_all(mark, 0, 0);
            lv_obj_set_style_shadow_width(mark, 0, 0);
            lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
        }
        break;
    }
    }

    s_viz_needs_rebuild = false;
}

// ─── Viz update (values only, no full rebuild) ────────────────────────────────
static void update_viz(void)
{
    if (s_viz_needs_rebuild) {
        build_viz();
        return;
    }

    lv_color_t col = lv_color_hex(s_color);

    switch (s_viz) {
    case VIZ_SPARK:
        if (s_viz_chart && s_viz_series) {
            // Push latest value into chart
            if (s_history_len > 0)
                lv_chart_set_next_value(s_viz_chart, s_viz_series,
                                        (int32_t)s_history[s_history_len - 1]);
            lv_chart_refresh(s_viz_chart);
        }
        break;

    case VIZ_ARC:
        if (s_viz_arc) {
            lv_arc_set_value(s_viz_arc, (int32_t)s_value);
        }
        if (s_viz_arc_lbl) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f\n%s", s_value, s_unit);
            lv_label_set_text(s_viz_arc_lbl, buf);
        }
        break;

    case VIZ_THERMO:
        if (s_viz_fill && s_viz_fill_outer) {
            int th = lv_obj_get_height(s_viz_fill_outer) - 2;
            float range = s_max - s_min;
            float pct   = range > 0 ? (s_value - s_min) / range : 0;
            int fh = (int)(th * pct);
            if (fh < 0) fh = 0;
            lv_obj_set_height(s_viz_fill, fh);
            lv_obj_set_pos(s_viz_fill, 1, th - fh + 1);
            lv_obj_set_style_bg_color(s_viz_fill, col, 0);
        }
        break;

    case VIZ_PH:
        if (s_viz_ph_bar) {
            int bw = lv_obj_get_width(s_viz_ph_bar);
            float range = s_max > s_min ? s_max - s_min : 14.0f;
            int pos_x = (int)((s_value - s_min) / range * bw) - 1;
            if (pos_x < 0) pos_x = 0;
            if (pos_x > bw - 3) pos_x = bw - 3;
            // Marker is first child of ph_bar
            lv_obj_t *marker = lv_obj_get_child(s_viz_ph_bar, 0);
            if (marker) lv_obj_set_pos(marker, pos_x, 0);
        }
        if (s_viz_ph_lbl) {
            char buf[20];
            snprintf(buf, sizeof(buf), "pH %.1f", s_value);
            lv_label_set_text(s_viz_ph_lbl, buf);
        }
        break;

    case VIZ_TANK:
        if (s_viz_fill && s_viz_fill_outer) {
            int fh_total = lv_obj_get_height(s_viz_fill_outer) - 2;
            float pct = s_value;
            if (pct < 0.0f) pct = 0.0f;
            if (pct > 100.0f) pct = 100.0f;
            int fh = (int)(fh_total * pct / 100.0f);
            lv_obj_set_height(s_viz_fill, fh);
            lv_obj_set_pos(s_viz_fill, 1, fh_total - fh + 1);
            lv_obj_set_style_bg_color(s_viz_fill,
                                      lv_color_hex(tank_fill_color(pct)), 0);
            // Update pct label (child 0 of outer)
            lv_obj_t *pct_lbl = lv_obj_get_child(s_viz_fill_outer, 0);
            if (pct_lbl) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d%%", (int)pct);
                lv_label_set_text(pct_lbl, buf);
            }
        }
        break;

    case VIZ_BULLET:
        if (s_viz_bul_fill && s_viz_bul_outer) {
            int bw = lv_obj_get_width(s_viz_bul_outer);
            float range = s_max - s_min;
            float pct   = range > 0 ? (s_value - s_min) / range : 0;
            int fw = (int)(bw * pct);
            if (fw < 0) fw = 0;
            lv_obj_set_width(s_viz_bul_fill, fw);
        }
        break;
    }
}

// ─── Random generator ─────────────────────────────────────────────────────────

static void update_interval_btns(void)
{
    for (int i = 0; i < 5; i++) {
        if (!s_interval_btns[i]) continue;
        bool active = (i == s_rand_interval_idx);
        lv_obj_set_style_bg_color(s_interval_btns[i],
                                  active ? COL_HI : COL_BASE2, 0);
        lv_obj_t *lbl = lv_obj_get_child(s_interval_btns[i], 0);
        if (lbl)
            lv_obj_set_style_text_color(lbl,
                                        active ? COL_BASE : COL_TX2, 0);
    }
}

static void push_history(float v)
{
    if (s_history_len < HISTORY_LEN) {
        s_history[s_history_len++] = v;
    } else {
        memmove(s_history, s_history + 1,
                (HISTORY_LEN - 1) * sizeof(float));
        s_history[HISTORY_LEN - 1] = v;
    }
}

static void rand_gen_once(void)
{
    float range = s_max - s_min;
    float r = s_min + (float)rand() / (float)RAND_MAX * range;
    if (r < s_min) r = s_min;
    if (r > s_max) r = s_max;
    s_value = r;
    push_history(r);

    // Update data model
    if (s_ctx_tank >= 0 && s_ctx_metric >= 0)
        g_tanks[s_ctx_tank].metrics[s_ctx_metric].value = s_value;
    else if (s_ctx_machine >= 0 && s_ctx_metric >= 0)
        g_machines[s_ctx_machine].metrics[s_ctx_metric].value = s_value;

    // Refresh detail labels and viz
    if (s_value_lbl) {
        char buf[24];
        if (s_step >= 1.0f)
            snprintf(buf, sizeof(buf), "%.0f", s_value);
        else
            snprintf(buf, sizeof(buf), "%.1f", s_value);
        lv_label_set_text(s_value_lbl, buf);
    }
    if (s_step_val_lbl) {
        char buf[24];
        if (s_step >= 1.0f)
            snprintf(buf, sizeof(buf), "%.0f", s_value);
        else
            snprintf(buf, sizeof(buf), "%.1f", s_value);
        lv_label_set_text(s_step_val_lbl, buf);
    }
    if (s_input_type == INPUT_SLIDER && s_slider) {
        int32_t sv = (int32_t)((s_value - s_min) * s_slider_factor);
        lv_slider_set_value(s_slider, sv, LV_ANIM_OFF);
    }
    update_viz();
}

static void rand_timer_cb(lv_timer_t *timer) { (void)timer; rand_gen_once(); }

static void rand_once_btn_cb(lv_event_t *e) { (void)e; rand_gen_once(); }

static void rand_start_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_rand_timer) {
        lv_timer_delete(s_rand_timer);
        s_rand_timer = NULL;
        if (s_rand_start_lbl) lv_label_set_text(s_rand_start_lbl, "Start Auto");
        if (s_rand_start_btn)
            lv_obj_set_style_bg_color(s_rand_start_btn, COL_HI, 0);
    } else {
        int ms = s_intervals[s_rand_interval_idx];
        s_rand_timer = lv_timer_create(rand_timer_cb, ms, NULL);
        if (s_rand_start_lbl) lv_label_set_text(s_rand_start_lbl, "Stop Auto");
        if (s_rand_start_btn)
            lv_obj_set_style_bg_color(s_rand_start_btn, COL_ERR, 0);
    }
}

static void interval_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_rand_interval_idx = idx;
    update_interval_btns();
    if (s_rand_timer) {
        lv_timer_delete(s_rand_timer);
        s_rand_timer = lv_timer_create(rand_timer_cb, s_intervals[idx], NULL);
    }
}

// ─── Internal: refresh labels from context ───────────────────────────────────
static void detail_refresh_labels(void)
{
    if (s_metric_lbl) lv_label_set_text(s_metric_lbl, s_metric);
    if (s_entity_lbl) lv_label_set_text(s_entity_lbl, s_entity);
    if (s_unit_lbl)   lv_label_set_text(s_unit_lbl, s_unit);

    char buf[24];
    if (s_step >= 1.0f)
        snprintf(buf, sizeof(buf), "%.0f", s_value);
    else
        snprintf(buf, sizeof(buf), "%.1f", s_value);

    if (s_value_lbl)     lv_label_set_text(s_value_lbl, buf);
    if (s_step_val_lbl)  lv_label_set_text(s_step_val_lbl, buf);

    if (s_input_type == INPUT_SLIDER && s_slider) {
        int32_t sv = (int32_t)((s_value - s_min) * s_slider_factor);
        lv_slider_set_value(s_slider, sv, LV_ANIM_OFF);
    }

    // Update data model
    if (s_ctx_tank >= 0 && s_ctx_metric >= 0)
        g_tanks[s_ctx_tank].metrics[s_ctx_metric].value = s_value;
    else if (s_ctx_machine >= 0 && s_ctx_metric >= 0)
        g_machines[s_ctx_machine].metrics[s_ctx_metric].value = s_value;

    update_viz();
}

// ─── Step / slider callbacks ──────────────────────────────────────────────────
static void step_btn_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    s_value += dir * s_step;
    if (s_value < s_min) s_value = s_min;
    if (s_value > s_max) s_value = s_max;
    push_history(s_value);
    detail_refresh_labels();
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    int32_t raw   = lv_slider_get_value(obj);
    s_value = s_min + (float)raw / (float)s_slider_factor;
    if (s_value < s_min) s_value = s_min;
    if (s_value > s_max) s_value = s_max;
    push_history(s_value);
    detail_refresh_labels();
}

// ─── Screen create ────────────────────────────────────────────────────────────
void ui_detail_create(lv_obj_t *parent)
{
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, CONTENT_W, DETAIL_H);
    lv_obj_set_pos(s_screen, 0, SZ_CRUMB);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, SZ_GAP, 0);
    lv_obj_set_style_pad_gap(s_screen, SZ_GAP, 0);
    lv_obj_set_style_outline_width(s_screen, 0, 0);
    lv_obj_set_style_shadow_width(s_screen, 0, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_screens[VIEW_DETAIL] = s_screen;

    // ── Header card ──────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(s_screen);
    lv_obj_set_size(hdr, CONTENT_W - SZ_GAP * 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(hdr, COL_BASE, 0);
    lv_obj_set_style_border_color(hdr, COL_BD, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_radius(hdr, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(hdr, SZ_CARD_PAD + 6, 0);
    lv_obj_set_style_shadow_width(hdr, 8, 0);
    lv_obj_set_style_shadow_ofs_y(hdr, 3, 0);
    lv_obj_set_style_shadow_color(hdr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(hdr, LV_OPA_10, 0);
    lv_obj_set_style_outline_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hdr_left = lv_obj_create(hdr);
    lv_obj_set_size(hdr_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_left, 0, 0);
    lv_obj_set_style_outline_width(hdr_left, 0, 0);
    lv_obj_set_style_shadow_width(hdr_left, 0, 0);
    lv_obj_set_style_pad_all(hdr_left, 0, 0);
    lv_obj_clear_flag(hdr_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr_left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hdr_left, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(hdr_left, 4, 0);

    s_metric_lbl = lv_label_create(hdr_left);
    lv_label_set_text(s_metric_lbl, "—");
    lv_obj_set_style_text_font(s_metric_lbl, FONT_SANS_MD, 0);
    lv_obj_set_style_text_color(s_metric_lbl, COL_TX, 0);

    s_entity_lbl = lv_label_create(hdr_left);
    lv_label_set_text(s_entity_lbl, "—");
    lv_obj_set_style_text_font(s_entity_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(s_entity_lbl, COL_TX2, 0);

    lv_obj_t *hdr_right = lv_obj_create(hdr);
    lv_obj_set_size(hdr_right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_right, 0, 0);
    lv_obj_set_style_outline_width(hdr_right, 0, 0);
    lv_obj_set_style_shadow_width(hdr_right, 0, 0);
    lv_obj_set_style_pad_all(hdr_right, 0, 0);
    lv_obj_clear_flag(hdr_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr_right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hdr_right, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_row(hdr_right, 2, 0);

    s_value_lbl = lv_label_create(hdr_right);
    lv_label_set_text(s_value_lbl, "-");
    lv_obj_set_style_text_font(s_value_lbl, FONT_SANS_XL, 0);
    lv_obj_set_style_text_color(s_value_lbl, COL_HI, 0);

    s_unit_lbl = lv_label_create(hdr_right);
    lv_label_set_text(s_unit_lbl, "");
    lv_obj_set_style_text_font(s_unit_lbl, FONT_MONO_XS, 0);
    lv_obj_set_style_text_color(s_unit_lbl, COL_TX2, 0);

    // ── Viz card ──────────────────────────────────────────────────────────────
    int hdr_approx = 84;
    int rand_h     = 112;
    int viz_h = DETAIL_H - SZ_GAP * 2 - hdr_approx - SZ_BTN_SM - rand_h - SZ_GAP * 4;
    if (viz_h < 80) viz_h = 80;

    s_viz_card = lv_obj_create(s_screen);
    lv_obj_set_size(s_viz_card, CONTENT_W - SZ_GAP * 2, viz_h);
    lv_obj_set_style_bg_color(s_viz_card, COL_BASE, 0);
    lv_obj_set_style_border_color(s_viz_card, COL_BD, 0);
    lv_obj_set_style_border_width(s_viz_card, 1, 0);
    lv_obj_set_style_radius(s_viz_card, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(s_viz_card, SZ_GAP, 0);
    lv_obj_set_style_shadow_width(s_viz_card, 8, 0);
    lv_obj_set_style_shadow_ofs_y(s_viz_card, 3, 0);
    lv_obj_set_style_shadow_color(s_viz_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_viz_card, LV_OPA_10, 0);
    lv_obj_set_style_outline_width(s_viz_card, 0, 0);
    lv_obj_clear_flag(s_viz_card, LV_OBJ_FLAG_SCROLLABLE);

    // ── Step input row ────────────────────────────────────────────────────────
    s_step_row = lv_obj_create(s_screen);
    lv_obj_set_size(s_step_row, CONTENT_W - SZ_GAP * 2, SZ_BTN_SM);
    lv_obj_set_style_bg_color(s_step_row, COL_BASE, 0);
    lv_obj_set_style_border_color(s_step_row, COL_BD_STR, 0);
    lv_obj_set_style_border_width(s_step_row, 1, 0);
    lv_obj_set_style_radius(s_step_row, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_all(s_step_row, 0, 0);
    lv_obj_set_style_shadow_width(s_step_row, 0, 0);
    lv_obj_set_style_outline_width(s_step_row, 0, 0);
    lv_obj_clear_flag(s_step_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_step_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_step_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *minus_btn = lv_btn_create(s_step_row);
    lv_obj_set_size(minus_btn, SZ_BTN_SM, SZ_BTN_SM);
    lv_obj_set_style_bg_color(minus_btn, COL_BASE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(minus_btn, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(minus_btn, 0, 0);
    lv_obj_set_style_radius(minus_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(minus_btn, 0, 0);
    lv_obj_add_event_cb(minus_btn, step_btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(-1));
    lv_obj_t *minus_lbl = lv_label_create(minus_btn);
    lv_label_set_text(minus_lbl, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(minus_lbl, FONT_SANS_LG, 0);
    lv_obj_set_style_text_color(minus_lbl, COL_TX2, 0);
    lv_obj_center(minus_lbl);

    // Center value label
    s_step_val_lbl = lv_label_create(s_step_row);
    lv_label_set_text(s_step_val_lbl, "-");
    lv_obj_set_style_text_font(s_step_val_lbl, FONT_MONO_MD, 0);
    lv_obj_set_style_text_color(s_step_val_lbl, COL_TX, 0);
    lv_obj_set_style_text_align(s_step_val_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_flex_grow(s_step_val_lbl, 1);

    lv_obj_t *plus_btn = lv_btn_create(s_step_row);
    lv_obj_set_size(plus_btn, SZ_BTN_SM, SZ_BTN_SM);
    lv_obj_set_style_bg_color(plus_btn, COL_BASE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(plus_btn, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(plus_btn, 0, 0);
    lv_obj_set_style_radius(plus_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(plus_btn, 0, 0);
    lv_obj_add_event_cb(plus_btn, step_btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(1));
    lv_obj_t *plus_lbl = lv_label_create(plus_btn);
    lv_label_set_text(plus_lbl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(plus_lbl, FONT_SANS_LG, 0);
    lv_obj_set_style_text_color(plus_lbl, COL_TX2, 0);
    lv_obj_center(plus_lbl);

    // ── Slider row (hidden by default) ────────────────────────────────────────
    s_slider_row = lv_obj_create(s_screen);
    lv_obj_set_size(s_slider_row, CONTENT_W - SZ_GAP * 2, SZ_BTN_SM);
    lv_obj_set_style_bg_color(s_slider_row, COL_BASE, 0);
    lv_obj_set_style_border_color(s_slider_row, COL_BD_STR, 0);
    lv_obj_set_style_border_width(s_slider_row, 1, 0);
    lv_obj_set_style_radius(s_slider_row, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(s_slider_row, 16, 0);
    lv_obj_set_style_pad_ver(s_slider_row, 0, 0);
    lv_obj_set_style_shadow_width(s_slider_row, 0, 0);
    lv_obj_set_style_outline_width(s_slider_row, 0, 0);
    lv_obj_clear_flag(s_slider_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_slider_row, LV_OBJ_FLAG_HIDDEN);

    s_slider = lv_slider_create(s_slider_row);
    lv_obj_set_width(s_slider, LV_PCT(100));
    lv_obj_center(s_slider);
    lv_obj_set_style_bg_color(s_slider, COL_HI, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, COL_BASE3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, COL_HI, LV_PART_KNOB);
    lv_obj_set_style_height(s_slider, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(s_slider, 3, LV_PART_MAIN);
    lv_obj_set_style_width(s_slider, SZ_THUMB, LV_PART_KNOB);
    lv_obj_set_style_height(s_slider, SZ_THUMB, LV_PART_KNOB);
    lv_obj_set_style_radius(s_slider, SZ_THUMB / 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider, -(SZ_THUMB / 2 - 3), LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ── Random generator section ──────────────────────────────────────────────
    lv_obj_t *rand_sect = lv_obj_create(s_screen);
    lv_obj_set_size(rand_sect, CONTENT_W - SZ_GAP * 2, rand_h);
    lv_obj_set_style_bg_color(rand_sect, COL_BASE, 0);
    lv_obj_set_style_border_color(rand_sect, COL_BD, 0);
    lv_obj_set_style_border_width(rand_sect, 1, 0);
    lv_obj_set_style_radius(rand_sect, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_all(rand_sect, 8, 0);
    lv_obj_set_style_pad_gap(rand_sect, 8, 0);
    lv_obj_set_style_shadow_width(rand_sect, 0, 0);
    lv_obj_set_style_outline_width(rand_sect, 0, 0);
    lv_obj_clear_flag(rand_sect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(rand_sect, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rand_sect, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Row 1: "Gen Once" + interval buttons
    lv_obj_t *row1 = lv_obj_create(rand_sect);
    lv_obj_remove_style_all(row1);
    lv_obj_set_size(row1, LV_PCT(100), 44);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(row1, 6, 0);

    lv_obj_t *gen_once = lv_btn_create(row1);
    lv_obj_set_size(gen_once, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_bg_color(gen_once, COL_BASE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(gen_once, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(gen_once, COL_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(gen_once, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(gen_once, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(gen_once, 0, 0);
    lv_obj_set_style_pad_hor(gen_once, 12, LV_PART_MAIN);
    lv_obj_add_event_cb(gen_once, rand_once_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gen_once_lbl = lv_label_create(gen_once);
    lv_label_set_text(gen_once_lbl, "Gen Once");
    lv_obj_set_style_text_font(gen_once_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(gen_once_lbl, COL_TX, 0);
    lv_obj_center(gen_once_lbl);

    for (int i = 0; i < 5; i++) {
        lv_obj_t *ibtn = lv_btn_create(row1);
        lv_obj_set_size(ibtn, LV_SIZE_CONTENT, 44);
        lv_obj_set_style_border_color(ibtn, COL_BD, LV_PART_MAIN);
        lv_obj_set_style_border_width(ibtn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(ibtn, SZ_RADIUS_SM, 0);
        lv_obj_set_style_shadow_width(ibtn, 0, 0);
        lv_obj_set_style_pad_hor(ibtn, 10, LV_PART_MAIN);
        lv_obj_add_event_cb(ibtn, interval_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *ilbl = lv_label_create(ibtn);
        lv_label_set_text(ilbl, s_interval_labels[i]);
        lv_obj_set_style_text_font(ilbl, FONT_MONO_XXS, 0);
        lv_obj_center(ilbl);
        s_interval_btns[i] = ibtn;
    }
    update_interval_btns();

    // Row 2: Start/Stop button
    s_rand_start_btn = lv_btn_create(rand_sect);
    lv_obj_set_size(s_rand_start_btn, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(s_rand_start_btn, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_rand_start_btn, COL_HI_DK, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_rand_start_btn, 0, 0);
    lv_obj_set_style_radius(s_rand_start_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(s_rand_start_btn, 0, 0);
    lv_obj_add_event_cb(s_rand_start_btn, rand_start_btn_cb, LV_EVENT_CLICKED, NULL);

    s_rand_start_lbl = lv_label_create(s_rand_start_btn);
    lv_label_set_text(s_rand_start_lbl, "Start Auto");
    lv_obj_set_style_text_font(s_rand_start_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(s_rand_start_lbl, COL_BASE, 0);
    lv_obj_center(s_rand_start_lbl);
}

// ─── Internal: open context helper ───────────────────────────────────────────
static void open_context(const char *entity, Metric *m,
                         int ctx_tank, int ctx_machine, int ctx_metric)
{
    // Stop running random timer
    if (s_rand_timer) {
        lv_timer_delete(s_rand_timer);
        s_rand_timer = NULL;
        if (s_rand_start_lbl) lv_label_set_text(s_rand_start_lbl, "Start Auto");
        if (s_rand_start_btn)
            lv_obj_set_style_bg_color(s_rand_start_btn, COL_HI, 0);
    }

    strncpy(s_entity, entity,  sizeof(s_entity)  - 1);
    strncpy(s_metric, m->label, sizeof(s_metric) - 1);
    strncpy(s_unit,   m->unit,  sizeof(s_unit)   - 1);
    s_value      = m->value;
    s_step       = m->step    > 0.0f ? m->step    : 1.0f;
    s_min        = m->min_val;
    s_max        = m->max_val;
    s_color      = metric_color(m);
    s_viz        = m->viz;
    s_input_type = m->input;
    s_ctx_tank    = ctx_tank;
    s_ctx_machine = ctx_machine;
    s_ctx_metric  = ctx_metric;

    // Copy history
    s_history_len = m->history_len < HISTORY_LEN ? m->history_len : HISTORY_LEN;
    for (int i = 0; i < s_history_len; i++) s_history[i] = m->history[i];

    // Slider factor
    if (s_step >= 1.0f)        s_slider_factor = 1;
    else if (s_step >= 0.1f)   s_slider_factor = 10;
    else                        s_slider_factor = 100;

    // Slider range
    if (s_slider) {
        int32_t sl_max = (int32_t)((s_max - s_min) * s_slider_factor);
        lv_slider_set_range(s_slider, 0, sl_max);
        int32_t sv = (int32_t)((s_value - s_min) * s_slider_factor);
        lv_slider_set_value(s_slider, sv, LV_ANIM_OFF);
    }

    // Show/hide input rows based on input type
    if (s_step_row && s_slider_row) {
        if (s_input_type == INPUT_SLIDER) {
            lv_obj_add_flag(s_step_row, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_slider_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_step_row, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_slider_row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    s_viz_needs_rebuild = true;
    detail_refresh_labels();
}

// ─── Public: open from tanks ──────────────────────────────────────────────────
void ui_detail_open_tank_level(int tank_idx)
{
    if (tank_idx < 0 || tank_idx >= g_tank_count) return;
    Tank *t = &g_tanks[tank_idx];
    open_context(t->name, &t->metrics[0], tank_idx, -1, 0);
    ui_push_detail(t->name, t->metrics[0].label);
}

void ui_detail_open_tank_metric(int tank_idx, int metric_idx)
{
    if (tank_idx < 0 || tank_idx >= g_tank_count) return;
    Tank *t = &g_tanks[tank_idx];
    if (metric_idx < 0 || metric_idx >= t->metric_count) return;
    open_context(t->name, &t->metrics[metric_idx], tank_idx, -1, metric_idx);
    ui_push_detail(t->name, t->metrics[metric_idx].label);
}

void ui_detail_open_machine_metric(int machine_idx, int metric_idx)
{
    if (machine_idx < 0 || machine_idx >= MACHINE_COUNT) return;
    Machine *mc = &g_machines[machine_idx];
    if (metric_idx < 0 || metric_idx >= mc->metric_count) return;
    open_context(mc->name, &mc->metrics[metric_idx], -1, machine_idx, metric_idx);
    ui_push_detail(mc->name, mc->metrics[metric_idx].label);
}

void ui_detail_step(int direction)
{
    s_value += direction * s_step;
    if (s_value < s_min) s_value = s_min;
    if (s_value > s_max) s_value = s_max;
    push_history(s_value);
    detail_refresh_labels();
}
