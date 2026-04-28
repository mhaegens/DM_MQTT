#include "ui_tanks.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_detail.h"
#include "data_model.h"
#include "mqtt_task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────────────────
//  ui_tanks.c — Three animated tank columns with wave physics + drag-to-adjust.
//  Wave is rendered into LVGL canvas buffers using lv_canvas pixel drawing.
//  Physics tick runs as a 33ms LVGL timer (~30 fps on P4).
// ─────────────────────────────────────────────────────────────────────────────

static const char *TAG = "tanks";

#define TANK_W       220    // column width per tank
#define TANK_LABEL_H  24    // name label above tank
#define STRIP_H       90    // metric chip strip below tank
#define TANK_H       (CONTENT_H - TANK_LABEL_H - STRIP_H - SZ_GAP * 3)

static lv_obj_t *s_screen      = NULL;
static lv_obj_t *s_canvases[TANK_COUNT] = {NULL};
static lv_obj_t *s_pct_labels[TANK_COUNT] = {NULL};
static lv_timer_t *s_phys_timer = NULL;

// Canvas buffers in PSRAM (TANK_W × TANK_H × 2 bytes for RGB565)
static uint8_t *s_cbuf[TANK_COUNT] = {NULL};

// Active drag state
static int   s_drag_tank   = -1;
static int   s_drag_y_start = 0;
static float s_drag_pct_start = 0;

// ─── Fill color from level ────────────────────────────────────────────────────
static lv_color_t tank_fill_color(float pct) {
    if (pct < 15.0f) return COL_ERR;
    if (pct < 40.0f) return COL_WARN;
    if (pct < 75.0f) return COL_HI;
    return lv_color_hex(0x0092D1);  // cyan for near-full
}

// ─── Draw one tank frame into its canvas ─────────────────────────────────────
//  The tank body shape is a cylinder (top) + tapered cone (bottom), matching
//  the HTML SVG mask: rect(0,0,W,82%) + trapezoid path for bottom 22%.
//  Fill is drawn as a solid rect up to the wave surface, then a sine wave
//  replaces the top edge.
static void draw_tank(int idx) {
    Tank *t = &g_tanks[idx];
    lv_obj_t *canvas = s_canvases[idx];
    if (!canvas) return;

    int W = TANK_W;
    int H = TANK_H;
    float pct = t->phys.pct / 100.0f;  // 0.0 – 1.0

    // Clear canvas to background color
    lv_canvas_fill_bg(canvas, COL_BASE3, OPA_FULL);

    // ── Tank body outline ─────────────────────────────────────────────────────
    // Cylindrical zone: y 0 → H*0.82
    // Cone zone:        y H*0.78 → H  (tapers from full width to ~33% width)
    lv_draw_rect_dsc_t outline;
    lv_draw_rect_dsc_init(&outline);
    outline.bg_opa    = LV_OPA_TRANSP;
    outline.border_color = COL_BD_STR;
    outline.border_width = 2;
    outline.radius    = SZ_RADIUS_SM;
    int cyl_h = (int)(H * 0.82f);
    lv_canvas_draw_rect(canvas, 0, 0, W, cyl_h, &outline);

    // ── Fill ──────────────────────────────────────────────────────────────────
    lv_color_t fill_col = tank_fill_color(t->phys.pct);
    int fill_top_y = (int)((1.0f - pct) * (float)cyl_h);  // y where fill begins
    if (fill_top_y < cyl_h) {
        lv_draw_rect_dsc_t fill;
        lv_draw_rect_dsc_init(&fill);
        fill.bg_color = fill_col;
        fill.bg_opa   = LV_OPA_80;
        fill.radius   = 0;
        lv_canvas_draw_rect(canvas, 1, fill_top_y, W - 2,
                            cyl_h - fill_top_y, &fill);
    }

    // ── Wave surface ─────────────────────────────────────────────────────────
    // Rasterise the two-frequency sine wave as a polyline, then fill below it.
    float phase  = t->phys.phase;
    float slosh  = t->phys.slosh;
    float amp    = t->phys.dragging ? 5.0f : 2.5f;

    if (fill_top_y < cyl_h - 1) {
        lv_point_t pts[TANK_W + 2];
        for (int x = 0; x < W; x++) {
            float xf = (float)x / (float)W;
            float y = sinf(xf * (float)M_PI * 4.0f + phase) * amp
                    + sinf(xf * (float)M_PI * 6.0f + phase * 0.7f) * amp * 0.4f
                    + slosh;
            pts[x].x = x;
            pts[x].y = (lv_coord_t)(fill_top_y + (int)y);
            if (pts[x].y < 0)      pts[x].y = 0;
            if (pts[x].y >= cyl_h) pts[x].y = cyl_h - 1;
        }
        lv_draw_line_dsc_t wave_line;
        lv_draw_line_dsc_init(&wave_line);
        wave_line.color = lv_color_mix(fill_col, COL_BASE, 180);
        wave_line.width = 2;
        for (int x = 0; x < W - 1; x++) {
            lv_canvas_draw_line(canvas, pts + x, 2, &wave_line);
        }
    }

    // ── Tick marks at 25%, 50%, 75% ───────────────────────────────────────────
    lv_draw_line_dsc_t tick;
    lv_draw_line_dsc_init(&tick);
    tick.color = lv_color_hex(0xC0C8D8);
    tick.width = 1;
    for (int p = 25; p <= 75; p += 25) {
        int y = (int)((1.0f - p / 100.0f) * cyl_h);
        lv_point_t p0 = {W - 20, y}, p1 = {W - 4, y};
        lv_canvas_draw_line(canvas, &p0, 2, &tick);
    }

    // ── Percentage label ─────────────────────────────────────────────────────
    if (s_pct_labels[idx]) {
        lv_label_set_text_fmt(s_pct_labels[idx], "%d%%", (int)t->phys.pct);
        lv_color_t txt_col = (pct < 0.25f) ? COL_TX : COL_BASE;
        lv_obj_set_style_text_color(s_pct_labels[idx], txt_col, 0);
    }
}

// ─── Physics timer callback (30 fps) ─────────────────────────────────────────
static void phys_tick(lv_timer_t *timer) {
    if (g_current_view != VIEW_TANKS) return;
    float dt = 0.033f;
    for (int i = 0; i < TANK_COUNT; i++) {
        TankPhysics *p = &g_tanks[i].phys;
        p->phase += dt * (1.2f + fabsf(p->slosh) * 0.3f);
        if (!p->dragging) p->slosh *= 0.92f;
        draw_tank(i);
    }
}

// ─── Touch callbacks on tank canvases ────────────────────────────────────────
static void tank_press_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);
    s_drag_tank     = idx;
    s_drag_y_start  = pt.y;
    s_drag_pct_start = g_tanks[idx].phys.pct;
    g_tanks[idx].phys.dragging = true;
}

static void tank_move_cb(lv_event_t *e) {
    if (s_drag_tank < 0) return;
    int idx = s_drag_tank;
    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);
    int   dy    = pt.y - s_drag_y_start;
    float dpct  = -(float)dy / (float)TANK_H * 100.0f;
    float new_pct = s_drag_pct_start + dpct;
    if (new_pct < 0)   new_pct = 0;
    if (new_pct > 100) new_pct = 100;

    g_tanks[idx].phys.pct   = new_pct;
    g_tanks[idx].level.v    = new_pct;
    g_tanks[idx].phys.slosh = -dpct * 0.09f;

    // Throttled MQTT publish (400 ms)
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (now_ms - g_tanks[idx].phys.last_mqtt_ms > 400) {
        mqtt_pub_tank_level(g_tanks[idx].id, new_pct);
        g_tanks[idx].phys.last_mqtt_ms = now_ms;
    }
}

static void tank_release_cb(lv_event_t *e) {
    if (s_drag_tank < 0) return;
    int idx = s_drag_tank;
    g_tanks[idx].phys.dragging = false;
    // Final publish on release
    mqtt_pub_tank_level(g_tanks[idx].id, g_tanks[idx].phys.pct);
    s_drag_tank = -1;
}

static void tank_tap_cb(lv_event_t *e) {
    // Single tap (no drag) → open level detail view
    if (s_drag_tank >= 0) return;   // was a drag, not a tap
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_detail_open_tank_level(idx);
}

// ─── Sub-metric chip tap ──────────────────────────────────────────────────────
typedef struct { int tank; int metric; } ChipCtx;
static void chip_tap_cb(lv_event_t *e) {
    ChipCtx *ctx = (ChipCtx *)lv_event_get_user_data(e);
    ui_detail_open_tank_metric(ctx->tank, ctx->metric);
}

// ─── Build screen ─────────────────────────────────────────────────────────────
void ui_tanks_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    g_screens[VIEW_TANKS] = s_screen;

    int col_w = SZ_SCREEN / TANK_COUNT;

    for (int i = 0; i < TANK_COUNT; i++) {
        Tank *t = &g_tanks[i];
        int x = i * col_w;

        // Column name label
        lv_obj_t *name = lv_label_create(s_screen);
        lv_label_set_text(name, t->name);
        lv_obj_set_style_text_font(name, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(name, COL_TX3, 0);
        lv_obj_set_pos(name, x + 4, SZ_GAP);

        // Allocate canvas buffer in PSRAM
        size_t buf_size = (size_t)TANK_W * TANK_H * LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565);
        s_cbuf[i] = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
        if (!s_cbuf[i]) {
            ESP_LOGE(TAG, "OOM for tank %d canvas", i);
            continue;
        }

        // Canvas widget
        s_canvases[i] = lv_canvas_create(s_screen);
        lv_canvas_set_buffer(s_canvases[i], s_cbuf[i],
                             TANK_W, TANK_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(s_canvases[i], x + (col_w - TANK_W) / 2,
                       TANK_LABEL_H + SZ_GAP);

        // Touch handlers
        lv_obj_add_flag(s_canvases[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_canvases[i], tank_press_cb,
                            LV_EVENT_PRESSED,   (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_canvases[i], tank_move_cb,
                            LV_EVENT_PRESSING,  (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_canvases[i], tank_release_cb,
                            LV_EVENT_RELEASED,  (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_canvases[i], tank_tap_cb,
                            LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);

        // Percentage overlay label
        s_pct_labels[i] = lv_label_create(s_screen);
        lv_label_set_text_fmt(s_pct_labels[i], "%d%%", (int)t->phys.pct);
        lv_obj_set_style_text_font(s_pct_labels[i], FONT_MONO_MD, 0);
        lv_obj_set_style_text_color(s_pct_labels[i], COL_BASE, 0);
        // Positioned over tank — updated each frame in draw_tank()
        lv_obj_set_pos(s_pct_labels[i], x + col_w / 2 - 22,
                       TANK_LABEL_H + SZ_GAP + TANK_H * 3 / 4);

        // Sub-metric chip strip
        int strip_y = TANK_LABEL_H + SZ_GAP + TANK_H + SZ_GAP;
        lv_obj_t *strip = lv_obj_create(s_screen);
        lv_obj_set_size(strip, col_w - SZ_GAP, STRIP_H);
        lv_obj_set_pos(strip, x + SZ_GAP / 2, strip_y);
        lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(strip, 0, 0);
        lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(strip, 0, 0);
        lv_obj_set_style_pad_gap(strip, 4, 0);

        // Build chips based on tank type
        // Each chip: dot + label + value  (tappable → detail view)
        // Metric indices: 0=temp, 1=flow_in/pressure/ph, 2=flow_out (waste only)
        struct { const char *label; Metric *m; const char *unit; int midx; } chips[3];
        int nchips = 0;
        chips[nchips++] = {"Temp",  &t->tank_temp, "\xc2\xb0""C", 1};
        if (i == 0)  chips[nchips++] = {"Flow In",  &t->flow_in,  "L/m", 2};
        if (i == 1)  chips[nchips++] = {"Pressure", &t->pressure, "bar", 3};
        if (i == 2) {
            chips[nchips++] = {"pH",      &t->ph,       "pH",  4};
            chips[nchips++] = {"Flow Out",&t->flow_out, "L/m", 5};
        }

        for (int c = 0; c < nchips; c++) {
            lv_obj_t *chip = lv_obj_create(strip);
            lv_obj_set_size(chip, LV_PCT(100), SZ_BTN_SM / 2);
            lv_obj_set_style_bg_color(chip, COL_BASE, 0);
            lv_obj_set_style_radius(chip, SZ_RADIUS_SM, 0);
            lv_obj_set_style_border_color(chip, COL_BD, 0);
            lv_obj_set_style_border_width(chip, 1, 0);
            lv_obj_set_style_pad_hor(chip, 6, 0);
            lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // Dot
            lv_obj_t *dot = lv_obj_create(chip);
            lv_obj_set_size(dot, SZ_DOT, SZ_DOT);
            lv_obj_set_style_bg_color(dot, COL_OK, 0);
            lv_obj_set_style_radius(dot, SZ_DOT / 2, 0);
            lv_obj_set_style_border_width(dot, 0, 0);

            // Label
            lv_obj_t *lbl = lv_label_create(chip);
            lv_label_set_text(lbl, chips[c].label);
            lv_obj_set_style_text_font(lbl, FONT_SANS_XXS, 0);
            lv_obj_set_style_text_color(lbl, COL_TX2, 0);

            // Value
            lv_obj_t *val = lv_label_create(chip);
            lv_label_set_text_fmt(val, "%.1f %s", chips[c].m->v, chips[c].unit);
            lv_obj_set_style_text_font(val, FONT_MONO_XXS, 0);
            lv_obj_set_style_text_color(val, COL_TX, 0);

            // Make tappable
            lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
            ChipCtx *ctx = malloc(sizeof(ChipCtx));
            ctx->tank   = i;
            ctx->metric = chips[c].midx;
            lv_obj_add_event_cb(chip, chip_tap_cb, LV_EVENT_SHORT_CLICKED, ctx);
        }
    }

    // Physics timer — 33ms = ~30fps
    s_phys_timer = lv_timer_create(phys_tick, 33, NULL);
    lv_timer_pause(s_phys_timer);
}

void ui_tanks_activate(void) {
    if (s_phys_timer) lv_timer_resume(s_phys_timer);
    for (int i = 0; i < TANK_COUNT; i++) draw_tank(i);
}

void ui_tanks_deactivate(void) {
    if (s_phys_timer) lv_timer_pause(s_phys_timer);
}
