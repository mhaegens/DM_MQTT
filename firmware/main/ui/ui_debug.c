// ui_debug.c — Debug log screen: connection grid + rolling 7-line log panel.
#include "ui_debug.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data_model.h"
#include "config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define LOG_LINES 7
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_log_labels[LOG_LINES] = {NULL};
static lv_obj_t *s_broker_val = NULL;
static lv_obj_t *s_mqtt_val   = NULL;
static lv_obj_t *s_ip_val     = NULL;
static lv_obj_t *s_rssi_val   = NULL;
static int       s_log_head   = 0;
static char      s_log_buf[LOG_LINES][80];

static void debug_sim_btn_cb(lv_event_t *e) {
    (void)e;
    ui_debug_log("PUB", "factory/machines/cnc01/rpm", "4850");
}

void ui_debug_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, SZ_GAP, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    g_screens[VIEW_DEBUG] = s_screen;

    // Connection info grid (2×2)
    static const char *keys[] = {"Broker", "MQTT", "Device IP", "RSSI"};
    lv_obj_t **vals[] = {&s_broker_val, &s_mqtt_val, &s_ip_val, &s_rssi_val};
    int gw = (SZ_SCREEN - SZ_GAP * 3) / 2, gh = 56;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *cell = lv_obj_create(s_screen);
        lv_obj_set_size(cell, gw, gh);
        lv_obj_set_pos(cell, SZ_GAP + (i%2)*(gw+SZ_GAP), SZ_GAP + (i/2)*(gh+SZ_GAP));
        lv_obj_set_style_bg_color(cell, COL_BASE, 0);
        lv_obj_set_style_radius(cell, SZ_RADIUS_SM, 0);
        lv_obj_set_style_border_color(cell, COL_BD, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_pad_all(cell, 8, 0);

        lv_obj_t *k = lv_label_create(cell);
        lv_label_set_text(k, keys[i]);
        lv_obj_set_style_text_font(k, FONT_SANS_XXS, 0);
        lv_obj_set_style_text_color(k, COL_TX3, 0);
        lv_obj_set_pos(k, 0, 0);

        *vals[i] = lv_label_create(cell);
        lv_obj_set_style_text_font(*vals[i], FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(*vals[i], COL_TX, 0);
        lv_obj_set_pos(*vals[i], 0, 20);
    }

    // Log panel (dark background)
    int log_y = SZ_GAP + (gh + SZ_GAP) * 2;
    int log_h = CONTENT_H - log_y - SZ_BTN - SZ_GAP * 2;
    lv_obj_t *log_panel = lv_obj_create(s_screen);
    lv_obj_set_size(log_panel, SZ_SCREEN - SZ_GAP * 2, log_h);
    lv_obj_set_pos(log_panel, 0, log_y);
    lv_obj_set_style_bg_color(log_panel, COL_DBG_BG, 0);
    lv_obj_set_style_radius(log_panel, SZ_RADIUS_SM, 0);
    lv_obj_set_style_border_width(log_panel, 0, 0);
    lv_obj_set_style_pad_all(log_panel, 8, 0);
    lv_obj_set_flex_flow(log_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(log_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (int i = 0; i < LOG_LINES; i++) {
        s_log_labels[i] = lv_label_create(log_panel);
        lv_label_set_text(s_log_labels[i], "—");
        lv_obj_set_style_text_font(s_log_labels[i], FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(s_log_labels[i], COL_TX3, 0);
    }

    // Simulate MQTT Publish button
    lv_obj_t *sim_btn = lv_btn_create(s_screen);
    lv_obj_set_size(sim_btn, SZ_SCREEN - SZ_GAP * 2, SZ_BTN);
    lv_obj_set_pos(sim_btn, 0, CONTENT_H - SZ_BTN - SZ_GAP);
    lv_obj_set_style_bg_color(sim_btn, COL_HI, 0);
    lv_obj_set_style_radius(sim_btn, SZ_RADIUS, 0);
    lv_obj_t *sim_lbl = lv_label_create(sim_btn);
    lv_label_set_text(sim_lbl, "Simulate MQTT Publish");
    lv_obj_set_style_text_font(sim_lbl, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(sim_lbl, COL_BASE, 0);
    lv_obj_center(sim_lbl);
    lv_obj_add_event_cb(sim_btn, debug_sim_btn_cb, LV_EVENT_CLICKED, NULL);

    ui_debug_refresh();
}

void ui_debug_refresh(void) {
    if (s_broker_val) lv_label_set_text_fmt(s_broker_val, "%s:%d",
                          g_mqtt_broker, MQTT_BROKER_PORT);
    if (s_mqtt_val)   lv_label_set_text(s_mqtt_val,
                          g_mqtt_connected ? "Connected" : "Disconnected");
    if (s_ip_val)     lv_label_set_text(s_ip_val, g_device_ip);
    if (s_rssi_val)   lv_label_set_text_fmt(s_rssi_val, "%d dBm", g_wifi_rssi);
    if (s_mqtt_val)   lv_obj_set_style_text_color(s_mqtt_val,
                          g_mqtt_connected ? COL_OK : COL_ERR, 0);
}

void ui_debug_log(const char *type, const char *topic, const char *value) {
    // Shift all labels down (oldest at top scrolls off)
    for (int i = 0; i < LOG_LINES - 1; i++) {
        lv_label_set_text(s_log_labels[i],
                          lv_label_get_text(s_log_labels[i + 1]));
        lv_obj_set_style_text_color(s_log_labels[i], COL_TX3, 0);
    }
    char line[80];
    snprintf(line, sizeof(line), "%s %s \xe2\x86\x92 %s", type, topic, value);
    lv_label_set_text(s_log_labels[LOG_LINES - 1], line);
    lv_obj_set_style_text_color(s_log_labels[LOG_LINES - 1], COL_DBG_PUB, 0);
}
