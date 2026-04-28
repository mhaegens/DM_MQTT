#include "ui_home.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data_model.h"
#include "config.h"
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────────────────
//  ui_home.c — Home screen: 2×2 KPI grid + 3 connection status rows.
//  Refreshed every time the Home tab is activated and on mqtt/wifi events.
// ─────────────────────────────────────────────────────────────────────────────

static lv_obj_t *s_screen = NULL;
// KPI value labels (updated in ui_home_refresh)
static lv_obj_t *s_kpi_val[4]  = {NULL};
static lv_obj_t *s_kpi_meta[4] = {NULL};
// Status row value labels
static lv_obj_t *s_row_val[3]  = {NULL};

static lv_obj_t *make_kpi_card(lv_obj_t *parent, int col, int row,
                                const char *label, lv_color_t accent) {
    int w = (SZ_SCREEN - SZ_GAP * 3) / 2;
    int h = 140;
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, SZ_GAP + col * (w + SZ_GAP),
                         SZ_GAP + row * (h + SZ_GAP));
    lv_obj_set_style_bg_color(card, COL_BASE, 0);
    lv_obj_set_style_radius(card, SZ_RADIUS, 0);
    lv_obj_set_style_border_color(card, COL_BD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, SZ_CARD_PAD, 0);

    // Accent bar at top
    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_size(bar, w - SZ_CARD_PAD * 2, 4);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, accent, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);

    // Label
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(lbl, COL_TX3, 0);
    lv_obj_set_pos(lbl, 0, 14);

    return card;
}

void ui_home_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    g_screens[VIEW_HOME] = s_screen;

    static const char *labels[]  = {"Running", "Pulses Today", "MQTT Msgs", "WiFi RSSI"};
    static lv_color_t  accents[] = {0};  // filled below

    // KPI grid
    lv_color_t acc[4] = { COL_OK, COL_HI, COL_WARN, COL_TEAL };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *card = make_kpi_card(s_screen, i % 2, i / 2, labels[i], acc[i]);

        // Big value
        s_kpi_val[i] = lv_label_create(card);
        lv_obj_set_style_text_font(s_kpi_val[i], FONT_SANS_XL, 0);
        lv_obj_set_style_text_color(s_kpi_val[i], COL_TX, 0);
        lv_obj_set_pos(s_kpi_val[i], 0, 40);

        // Meta text
        s_kpi_meta[i] = lv_label_create(card);
        lv_obj_set_style_text_font(s_kpi_meta[i], FONT_SANS_XXS, 0);
        lv_obj_set_style_text_color(s_kpi_meta[i], COL_TX3, 0);
        lv_obj_set_pos(s_kpi_meta[i], 0, 90);
    }

    // Status rows (MQTT broker, device IP, active operator)
    static const char *row_labels[] = { "MQTT Broker", "Device IP", "Operator" };
    int card_top = SZ_GAP * 3 + 140 * 2;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(s_screen);
        lv_obj_set_size(row, SZ_SCREEN - SZ_GAP * 2, SZ_BTN_SM);
        lv_obj_set_pos(row, SZ_GAP, card_top + i * (SZ_BTN_SM + SZ_GAP));
        lv_obj_set_style_bg_color(row, COL_BASE, 0);
        lv_obj_set_style_radius(row, SZ_RADIUS_SM, 0);
        lv_obj_set_style_border_color(row, COL_BD, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_hor(row, SZ_CARD_PAD, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *key = lv_label_create(row);
        lv_label_set_text(key, row_labels[i]);
        lv_obj_set_style_text_font(key, FONT_SANS_XXS, 0);
        lv_obj_set_style_text_color(key, COL_TX2, 0);

        s_row_val[i] = lv_label_create(row);
        lv_obj_set_style_text_font(s_row_val[i], FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(s_row_val[i], COL_TX, 0);
    }

    ui_home_refresh();
}

void ui_home_refresh(void) {
    if (!s_screen) return;
    char buf[32];

    // KPI 0: Running machines
    int running = data_running_count();
    lv_label_set_text_fmt(s_kpi_val[0],  "%d / %d", running, MACH_COUNT);
    // Find stopped machine name for meta
    const char *stopped = NULL;
    for (int i = 0; i < MACH_COUNT; i++)
        if (!g_machines[i].running) { stopped = g_machines[i].name; break; }
    if (stopped)
        lv_label_set_text_fmt(s_kpi_meta[0], "%s stopped", stopped);
    else
        lv_label_set_text(s_kpi_meta[0], "All running");

    // KPI 1: Pulses today
    lv_label_set_text_fmt(s_kpi_val[1], "%d", data_total_pulses());
    lv_label_set_text(s_kpi_meta[1], "cumulative count");

    // KPI 2: MQTT message count
    lv_label_set_text_fmt(s_kpi_val[2], "%d", g_mqtt_msg_count);
    lv_label_set_text(s_kpi_meta[2], "since boot");

    // KPI 3: WiFi RSSI
    lv_label_set_text_fmt(s_kpi_val[3], "%d", g_wifi_rssi);
    lv_label_set_text(s_kpi_meta[3], "dBm");

    // Status rows
    lv_label_set_text_fmt(s_row_val[0], "%s : %d",
                          g_mqtt_broker, MQTT_BROKER_PORT);
    lv_label_set_text(s_row_val[1], g_device_ip);
    if (g_active_operator >= 0)
        lv_label_set_text(s_row_val[2], g_operators[g_active_operator].name);
    else
        lv_label_set_text(s_row_val[2], "—");
}
