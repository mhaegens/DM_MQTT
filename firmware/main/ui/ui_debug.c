// ui_debug.c — Debug log screen: connection grid + rolling 7-line log panel.
#include "ui_debug.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define LOG_LINES 7
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_log_labels[LOG_LINES] = {NULL};
static lv_obj_t *s_broker_val  = NULL;
static lv_obj_t *s_mqtt_val    = NULL;
static lv_obj_t *s_ip_val      = NULL;
static lv_obj_t *s_rssi_val    = NULL;
static lv_obj_t *s_int_ram_val = NULL;
static lv_obj_t *s_spi_ram_val = NULL;
static lv_timer_t *s_hw_timer  = NULL;

static void debug_sim_btn_cb(lv_event_t *e) {
    (void)e;
    ui_debug_log("PUB", "factory/machines/cnc01/rpm", "4850");
}

static void hw_stats_timer_cb(lv_timer_t *t) {
    (void)t;
    size_t int_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t int_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t spi_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    if (s_int_ram_val) {
        lv_label_set_text_fmt(s_int_ram_val, "%u KB free\n(min %u KB)",
                              (unsigned)(int_free / 1024),
                              (unsigned)(int_min  / 1024));
        lv_color_t c = (int_free < 50*1024) ? COL_ERR :
                       (int_free < 100*1024) ? COL_WARN : COL_OK;
        lv_obj_set_style_text_color(s_int_ram_val, c, 0);
    }
    if (s_spi_ram_val) {
        lv_label_set_text_fmt(s_spi_ram_val, "%u KB free",
                              (unsigned)(spi_free / 1024));
        lv_obj_set_style_text_color(s_spi_ram_val, COL_OK, 0);
    }
    ui_debug_refresh();
}

// ─── stat card ────────────────────────────────────────────────────────────────
typedef struct { const char *key; lv_obj_t **out; } StatDef;

void ui_debug_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    g_screens[VIEW_DEBUG] = s_screen;

    // ── 3×2 absolute-positioned stat card grid ────────────────────────────────
    int cols = 3;
    int gw = (SZ_SCREEN - SZ_GAP * (cols + 1)) / cols;  // ≈226px per card
    int gh = 64;

    static const StatDef defs[6] = {
        {"Broker",       NULL},
        {"MQTT",         NULL},
        {"Device IP",    NULL},
        {"WiFi RSSI",    NULL},
        {"Internal RAM", NULL},
        {"SPIRAM",       NULL},
    };
    lv_obj_t **vals[6] = {
        &s_broker_val, &s_mqtt_val, &s_ip_val,
        &s_rssi_val, &s_int_ram_val, &s_spi_ram_val
    };

    for (int i = 0; i < 6; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = SZ_GAP + col * (gw + SZ_GAP);
        int y = SZ_GAP + row * (gh + SZ_GAP);

        lv_obj_t *cell = lv_obj_create(s_screen);
        lv_obj_set_size(cell, gw, gh);
        lv_obj_set_pos(cell, x, y);
        lv_obj_set_style_bg_color(cell, COL_BASE, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(cell, SZ_RADIUS_SM, 0);
        lv_obj_set_style_border_color(cell, COL_BD, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_pad_all(cell, 10, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *k = lv_label_create(cell);
        lv_label_set_text(k, defs[i].key);
        lv_obj_set_style_text_font(k, FONT_SANS_XXS, 0);
        lv_obj_set_style_text_color(k, COL_TX3, 0);
        lv_obj_set_pos(k, 0, 0);

        *vals[i] = lv_label_create(cell);
        lv_label_set_text(*vals[i], "—");
        lv_obj_set_style_text_font(*vals[i], FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(*vals[i], COL_TX, 0);
        lv_obj_set_pos(*vals[i], 0, 22);
    }

    // ── Log panel ──────────────────────────────────────────────────────────────
    int log_y = SZ_GAP + (gh + SZ_GAP) * 2;
    int log_h = CONTENT_H - log_y - SZ_BTN - SZ_GAP * 2;

    lv_obj_t *log_panel = lv_obj_create(s_screen);
    lv_obj_set_size(log_panel, SZ_SCREEN - SZ_GAP * 2, log_h);
    lv_obj_set_pos(log_panel, SZ_GAP, log_y);
    lv_obj_set_style_bg_color(log_panel, COL_DBG_BG, 0);
    lv_obj_set_style_bg_opa(log_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(log_panel, SZ_RADIUS_SM, 0);
    lv_obj_set_style_border_width(log_panel, 0, 0);
    lv_obj_set_style_pad_all(log_panel, 8, 0);
    lv_obj_clear_flag(log_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(log_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(log_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(log_panel, 2, 0);

    for (int i = 0; i < LOG_LINES; i++) {
        s_log_labels[i] = lv_label_create(log_panel);
        lv_label_set_text(s_log_labels[i], " ");
        lv_obj_set_style_text_font(s_log_labels[i], FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(s_log_labels[i], COL_TX3, 0);
    }

    // ── Simulate MQTT Publish button ──────────────────────────────────────────
    lv_obj_t *sim_btn = lv_btn_create(s_screen);
    lv_obj_set_size(sim_btn, SZ_SCREEN - SZ_GAP * 2, SZ_BTN);
    lv_obj_set_pos(sim_btn, SZ_GAP, CONTENT_H - SZ_BTN - SZ_GAP);
    lv_obj_set_style_bg_color(sim_btn, COL_HI, 0);
    lv_obj_set_style_radius(sim_btn, SZ_RADIUS, 0);
    lv_obj_t *sim_lbl = lv_label_create(sim_btn);
    lv_label_set_text(sim_lbl, "Simulate MQTT Publish");
    lv_obj_set_style_text_font(sim_lbl, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(sim_lbl, COL_BASE, 0);
    lv_obj_center(sim_lbl);
    lv_obj_add_event_cb(sim_btn, debug_sim_btn_cb, LV_EVENT_CLICKED, NULL);

    // 1s timer — created paused, resumed only when Debug tab is active
    s_hw_timer = lv_timer_create(hw_stats_timer_cb, 1000, NULL);
    lv_timer_pause(s_hw_timer);

    ui_debug_refresh();
}

void ui_debug_activate(void) {
    hw_stats_timer_cb(NULL);   // immediate update
    if (s_hw_timer) lv_timer_resume(s_hw_timer);
}

void ui_debug_deactivate(void) {
    if (s_hw_timer) lv_timer_pause(s_hw_timer);
}

void ui_debug_refresh(void) {
    if (s_broker_val) lv_label_set_text(s_broker_val, MQTT_BROKER_URI);
    if (s_mqtt_val)   lv_label_set_text(s_mqtt_val,
                          g_mqtt_connected ? "Connected" : "Disconnected");
    if (s_ip_val)     lv_label_set_text(s_ip_val, g_device_ip);
    if (s_rssi_val)   lv_label_set_text_fmt(s_rssi_val, "%d dBm", g_wifi_rssi);
    if (s_mqtt_val)   lv_obj_set_style_text_color(s_mqtt_val,
                          g_mqtt_connected ? COL_OK : COL_ERR, 0);
}

void ui_debug_log(const char *type, const char *topic, const char *value) {
    // Shift all labels up (oldest at index 0 scrolls off)
    for (int i = 0; i < LOG_LINES - 1; i++) {
        lv_label_set_text(s_log_labels[i],
                          lv_label_get_text(s_log_labels[i + 1]));
        lv_obj_set_style_text_color(s_log_labels[i], COL_TX3, 0);
    }
    int64_t sec = esp_timer_get_time() / 1000000;
    char line[96];
    snprintf(line, sizeof(line), "%3llds %s %s->%s", (long long)sec, type, topic, value);
    lv_label_set_text(s_log_labels[LOG_LINES - 1], line);

    lv_color_t col;
    if      (strcmp(type, "OK")  == 0) col = COL_DBG_OK;
    else if (strcmp(type, "ERR") == 0) col = COL_DBG_ERR;
    else                               col = COL_DBG_PUB;
    lv_obj_set_style_text_color(s_log_labels[LOG_LINES - 1], col, 0);
}
