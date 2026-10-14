// ui_mqtt_detail.c — MQTT connection detail overlay (static config + live runtime)
#include "ui_mqtt_detail.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "config.h"
#include "data.h"
#include "../mqtt_task.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_screen          = NULL;
static lv_obj_t *s_status_dot      = NULL;
static lv_obj_t *s_status_lbl      = NULL;
static lv_obj_t *s_msg_lbl         = NULL;
static lv_obj_t *s_ip_lbl          = NULL;
static lv_obj_t *s_rssi_lbl        = NULL;
static lv_obj_t *s_wifi_lbl        = NULL;
static lv_obj_t *s_last_topic_lbl  = NULL;
static lv_obj_t *s_queue_lbl;
static lv_obj_t *s_broker_uri_lbl, *s_protocol_lbl, *s_tls_lbl;
static lv_obj_t *s_reconnect_btn   = NULL;

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

// ─── Reconnect callback ───────────────────────────────────────────────────────
static void reconnect_cb(lv_event_t *e)
{
    (void)e;
    mqtt_task_reconnect();
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_current_view == VIEW_MQTT_DETAIL) ui_mqtt_detail_refresh();
}
static void label_cached(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
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
    s_broker_uri_lbl = make_info_row(broker_card, "Broker", MQTT_BROKER_URI);
    char port[24];
    if (MQTT_BROKER_PORT) snprintf(port, sizeof(port), "%d", MQTT_BROKER_PORT);
    else snprintf(port, sizeof(port), "From URI");
    make_info_row(broker_card, "Port", port);
    bool wss = !strncmp(MQTT_BROKER_URI, "wss://", 6);
    bool ws = !strncmp(MQTT_BROKER_URI, "ws://", 5);
    bool tls = wss || !strncmp(MQTT_BROKER_URI, "mqtts://", 8);
    s_protocol_lbl = make_info_row(broker_card, "Protocol", wss ? "MQTT / WSS" : ws ? "MQTT / WebSocket" : "MQTT / TCP");
    s_tls_lbl = make_info_row(broker_card, "TLS", tls ? "Enabled (CA verified)" : "Off");

    make_info_row(broker_card, "Client ID", MQTT_CLIENT_ID);
    char keepalive[24]; snprintf(keepalive, sizeof(keepalive), "%d s", MQTT_KEEPALIVE_S);
    make_info_row(broker_card, "Keepalive", keepalive);

    // ── Runtime Status ────────────────────────────────────────────────────────
    make_section_label(s_screen, "RUNTIME STATUS");

    lv_obj_t *runtime_card = make_card(s_screen);
    s_msg_lbl         = make_info_row(runtime_card, "Messages",   "0");
    s_ip_lbl          = make_info_row(runtime_card, "Device IP",  "-");
    s_rssi_lbl        = make_info_row(runtime_card, "RSSI",       "-");
    s_wifi_lbl        = make_info_row(runtime_card, "WiFi",       "-");
    s_last_topic_lbl  = make_info_row(runtime_card, "Last topic", "-");
    s_queue_lbl = make_info_row(runtime_card, "TX queue", "-");

    // ── Topics ────────────────────────────────────────────────────────────────
    make_section_label(s_screen, "TOPICS");

    lv_obj_t *topics_card = make_card(s_screen);
    make_info_row(topics_card, "Prefix",   TOPIC_PREFIX "/");
    make_info_row(topics_card, "Machines", TOPIC_PREFIX "/machines/{id}/{key}");
    make_info_row(topics_card, "Tanks",    TOPIC_PREFIX "/tanks/{id}/{key}");
    make_info_row(topics_card, "NFC",      TOPIC_PREFIX "/operators/login");

    // ── Reconnect button ──────────────────────────────────────────────────────
    s_reconnect_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_reconnect_btn, CONTENT_W - SZ_GAP * 2, 52);
    lv_obj_set_style_bg_color(s_reconnect_btn, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_reconnect_btn, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(s_reconnect_btn, COL_BG, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(s_reconnect_btn, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_reconnect_btn, COL_BD, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(s_reconnect_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_reconnect_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(s_reconnect_btn, 0, 0);
    lv_obj_add_event_cb(s_reconnect_btn, reconnect_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(s_reconnect_btn);
    lv_label_set_text(btn_lbl, "Reconnect");
    lv_obj_set_style_text_font(btn_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(btn_lbl, COL_HI, 0);
    lv_obj_set_style_text_color(btn_lbl, COL_TX3, LV_STATE_DISABLED);
    lv_obj_center(btn_lbl);
    lv_timer_create(refresh_timer_cb, UI_REFRESH_MS, NULL);
}

// ─── Refresh ──────────────────────────────────────────────────────────────────
void ui_mqtt_detail_refresh(void)
{
    MqttStats route; mqtt_get_stats(&route);
    bool wss = !strncmp(route.broker_uri, "wss://", 6), ws = !strncmp(route.broker_uri, "ws://", 5);
    bool tls = wss || !strncmp(route.broker_uri, "mqtts://", 8);
    label_cached(s_broker_uri_lbl, route.broker_uri);
    label_cached(s_protocol_lbl, wss ? "MQTT / WSS" : ws ? "MQTT / WebSocket" : "MQTT / TCP");
    label_cached(s_tls_lbl, tls ? "Enabled (CA verified)" : "Trusted LAN");
    if (!s_screen) return;

    lv_color_t col = g_mqtt_connected ? COL_OK : COL_ERR;

    static int previous_state = -1;
    if (previous_state != (int)g_mqtt_connected) {
        previous_state = g_mqtt_connected;
        if (s_status_dot) lv_obj_set_style_bg_color(s_status_dot, col, 0);
        if (s_status_lbl) {
            label_cached(s_status_lbl, g_mqtt_connected ? "CONNECTED" : "DISCONNECTED");
            lv_obj_set_style_text_color(s_status_lbl, col, 0);
        }
    }

    char buf[128];

    if (s_msg_lbl) {
        snprintf(buf, sizeof(buf), "%d", g_mqtt_msg_count);
        label_cached(s_msg_lbl, buf);
    }
    if (s_ip_lbl)
        label_cached(s_ip_lbl, g_device_ip[0] ? g_device_ip : "-");
    if (s_rssi_lbl) {
        if (g_wifi_rssi != 0)
            snprintf(buf, sizeof(buf), "%d dBm", g_wifi_rssi);
        else
            snprintf(buf, sizeof(buf), "-");
        label_cached(s_rssi_lbl, buf);
    }
    if (s_wifi_lbl)
        label_cached(s_wifi_lbl, g_wifi_connected ? "Connected" : "Disconnected");

    MqttStats stats;
    mqtt_get_stats(&stats);
    if (s_last_topic_lbl) label_cached(s_last_topic_lbl, stats.last_topic[0] ? stats.last_topic : "-");
    if (s_queue_lbl) {
        snprintf(buf, sizeof(buf), "%lu pending / %lu ms wait / %lu rejected / %lu failed",
                 (unsigned long)stats.pending, (unsigned long)stats.queue_last_ms,
                 (unsigned long)stats.rejected, (unsigned long)stats.failed);
        label_cached(s_queue_lbl, buf);
    }

    if (s_reconnect_btn) {
        if (g_mqtt_connected)
            lv_obj_add_state(s_reconnect_btn, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(s_reconnect_btn, LV_STATE_DISABLED);
    }
}
