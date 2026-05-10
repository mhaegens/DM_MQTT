// ui_mqtt_detail.c — MQTT connection detail overlay (static config + live runtime)
#include "ui_mqtt_detail.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "config.h"
#include "data.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "mqtt_detail";

static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_status_dot = NULL;
static lv_obj_t *s_status_lbl = NULL;
static lv_obj_t *s_msg_lbl    = NULL;
static lv_obj_t *s_ip_lbl     = NULL;
static lv_obj_t *s_rssi_lbl   = NULL;
static lv_obj_t *s_wifi_lbl   = NULL;

// ─── Helper: bordered card container ─────────────────────────────────────────
static lv_obj_t *make_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CONTENT_W - SZ_GAP * 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, COL_BASE, 0);
    lv_obj_set_style_border_color(card, COL_BD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(card, SZ_CARD_PAD, 0);
    lv_obj_set_style_pad_gap(card, 10, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return card;
}

// ─── Helper: dimmed section label ────────────────────────────────────────────
static void make_section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(lbl, COL_TX3, 0);
    lv_obj_set_style_pad_left(lbl, SZ_GAP, 0);
}

// ─── Helper: key/value row; returns val label for dynamic rows ────────────────
static lv_obj_t *make_info_row(lv_obj_t *parent, const char *key, const char *val)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);

    lv_obj_t *key_lbl = lv_label_create(row);
    lv_label_set_text(key_lbl, key);
    lv_obj_set_style_text_font(key_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(key_lbl, COL_TX3, 0);
    lv_obj_set_width(key_lbl, 90);

    lv_obj_t *val_lbl = lv_label_create(row);
    lv_label_set_text(val_lbl, val);
    lv_obj_set_style_text_font(val_lbl, FONT_MONO_XS, 0);
    lv_obj_set_style_text_color(val_lbl, COL_HI, 0);
    lv_obj_set_flex_grow(val_lbl, 1);

    return val_lbl;
}

// ─── Reconnect stub ───────────────────────────────────────────────────────────
static void reconnect_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "reconnect stub — backend not wired yet");
}

// ─── Create ───────────────────────────────────────────────────────────────────
void ui_mqtt_detail_create(lv_obj_t *parent)
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
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_screens[VIEW_MQTT_DETAIL] = s_screen;

    // ── Status banner ─────────────────────────────────────────────────────────
    lv_obj_t *banner = lv_obj_create(s_screen);
    lv_obj_set_size(banner, CONTENT_W - SZ_GAP * 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(banner, COL_OK_LT, 0);
    lv_obj_set_style_border_color(banner, COL_OK, 0);
    lv_obj_set_style_border_width(banner, 1, 0);
    lv_obj_set_style_radius(banner, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(banner, SZ_CARD_PAD, 0);
    lv_obj_set_style_pad_column(banner, 12, 0);
    lv_obj_set_style_shadow_width(banner, 0, 0);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(banner, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(banner, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_status_dot = lv_obj_create(banner);
    lv_obj_set_size(s_status_dot, 16, 16);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, COL_OK, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_set_style_shadow_width(s_status_dot, 0, 0);
    lv_obj_clear_flag(s_status_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_status_lbl = lv_label_create(banner);
    lv_label_set_text(s_status_lbl, "CONNECTED");
    lv_obj_set_style_text_font(s_status_lbl, FONT_SANS_MD, 0);
    lv_obj_set_style_text_color(s_status_lbl, COL_OK, 0);

    // ── Broker Connection ─────────────────────────────────────────────────────
    make_section_label(s_screen, "BROKER CONNECTION");

    lv_obj_t *broker_card = make_card(s_screen);
    make_info_row(broker_card, "Broker",    MQTT_BROKER_URI);
    make_info_row(broker_card, "Port",      "443");
    make_info_row(broker_card, "Protocol",  "WebSocket Secure (WSS)");
    make_info_row(broker_card, "TLS",       "Enabled (Cloudflare)");
    make_info_row(broker_card, "Client ID", MQTT_CLIENT_ID);
    make_info_row(broker_card, "Keepalive", "60 s");

    // ── Runtime Status ────────────────────────────────────────────────────────
    make_section_label(s_screen, "RUNTIME STATUS");

    lv_obj_t *runtime_card = make_card(s_screen);
    s_msg_lbl  = make_info_row(runtime_card, "Messages", "0");
    s_ip_lbl   = make_info_row(runtime_card, "Device IP", "-");
    s_rssi_lbl = make_info_row(runtime_card, "RSSI",      "-");
    s_wifi_lbl = make_info_row(runtime_card, "WiFi",      "-");

    // ── Topics ────────────────────────────────────────────────────────────────
    make_section_label(s_screen, "TOPICS");

    lv_obj_t *topics_card = make_card(s_screen);
    make_info_row(topics_card, "Prefix",   TOPIC_PREFIX "/");
    make_info_row(topics_card, "Machines", TOPIC_PREFIX "/machines/{id}/telemetry");
    make_info_row(topics_card, "Tanks",    TOPIC_PREFIX "/tanks/{id}/telemetry");
    make_info_row(topics_card, "NFC",      TOPIC_PREFIX "/operators/login");

    // ── Reconnect stub button ─────────────────────────────────────────────────
    lv_obj_t *btn = lv_btn_create(s_screen);
    lv_obj_set_size(btn, CONTENT_W - SZ_GAP * 2, 52);
    lv_obj_set_style_bg_color(btn, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, reconnect_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Reconnect");
    lv_obj_set_style_text_font(btn_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(btn_lbl, COL_HI, 0);
    lv_obj_center(btn_lbl);
}

// ─── Refresh ──────────────────────────────────────────────────────────────────
void ui_mqtt_detail_refresh(void)
{
    if (!s_screen) return;

    lv_color_t col = g_mqtt_connected ? COL_OK : COL_ERR;

    if (s_status_dot)
        lv_obj_set_style_bg_color(s_status_dot, col, 0);
    if (s_status_lbl) {
        lv_label_set_text(s_status_lbl, g_mqtt_connected ? "CONNECTED" : "DISCONNECTED");
        lv_obj_set_style_text_color(s_status_lbl, col, 0);
    }

    char buf[32];

    if (s_msg_lbl) {
        snprintf(buf, sizeof(buf), "%d", g_mqtt_msg_count);
        lv_label_set_text(s_msg_lbl, buf);
    }
    if (s_ip_lbl)
        lv_label_set_text(s_ip_lbl, g_device_ip[0] ? g_device_ip : "-");
    if (s_rssi_lbl) {
        if (g_wifi_rssi != 0)
            snprintf(buf, sizeof(buf), "%d dBm", g_wifi_rssi);
        else
            snprintf(buf, sizeof(buf), "-");
        lv_label_set_text(s_rssi_lbl, buf);
    }
    if (s_wifi_lbl)
        lv_label_set_text(s_wifi_lbl, g_device_ip[0] ? "Connected" : "Disconnected");
}
