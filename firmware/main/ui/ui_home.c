#include "ui_home.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data.h"
#include "config.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static void wifi_kpi_cb(lv_event_t *e)  { (void)e; ui_push_wifi_setup(); }
static void mqtt_kpi_cb(lv_event_t *e)  { (void)e; ui_push_mqtt_detail(); }

// ─────────────────────────────────────────────────────────────────────────────
//  ui_home.c - Home screen: 2×2 KPI grid + status rows.
//  Refreshed by a 1500ms LVGL timer (live data), tab activation, and events.
// ─────────────────────────────────────────────────────────────────────────────

static lv_obj_t *s_screen          = NULL;
static lv_obj_t *s_kpi_val[4]     = {NULL};
static lv_obj_t *s_kpi_meta[4]    = {NULL};
static lv_obj_t *s_operator_label = NULL;
static lv_obj_t *s_ip_label       = NULL;
static lv_obj_t *s_mqtt_val_lbl   = NULL;  // MQTT broker row value text
static lv_obj_t *s_mqtt_badge_lbl = NULL;  // MQTT row badge text
static lv_obj_t *s_mqtt_badge     = NULL;  // MQTT row badge pill
static lv_obj_t *s_uptime_lbl     = NULL;  // uptime label in KPI meta[1]

// ─── KPI card ────────────────────────────────────────────────────────────────
static lv_obj_t *make_kpi_card(lv_obj_t *grid, int idx,
                           const char *label_text,
                           const char *val_text,
                           const char *meta_text,
                           lv_color_t  accent)
{
    int grid_col = idx % 2;
    int grid_row = idx / 2;

    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_height(card, 140);
    lv_obj_set_style_bg_color(card, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, SZ_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COL_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_top(card, SZ_CARD_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(card, SZ_CARD_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(card, SZ_CARD_PAD, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, grid_col, 1,
                               LV_GRID_ALIGN_STRETCH, grid_row, 1);

    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_size(bar, LV_PCT(100), 4);
    lv_obj_set_pos(bar, 0, -SZ_CARD_PAD);
    lv_obj_set_style_bg_color(bar, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(card);
    lv_obj_set_pos(lbl, 0, 10);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, FONT_MONO_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, COL_TX3, LV_PART_MAIN);

    s_kpi_val[idx] = lv_label_create(card);
    lv_obj_set_pos(s_kpi_val[idx], 0, 34);
    lv_label_set_text(s_kpi_val[idx], val_text);
    lv_obj_set_style_text_font(s_kpi_val[idx], FONT_MONO_MD, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_kpi_val[idx], accent, LV_PART_MAIN);

    s_kpi_meta[idx] = lv_label_create(card);
    lv_obj_set_pos(s_kpi_meta[idx], 0, 78);
    lv_label_set_text(s_kpi_meta[idx], meta_text);
    lv_obj_set_style_text_font(s_kpi_meta[idx], FONT_SANS_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_kpi_meta[idx], COL_TX3, LV_PART_MAIN);
    return card;
}

// ─── Badge pill ───────────────────────────────────────────────────────────────
static lv_obj_t *make_badge(lv_obj_t *parent,
                             const char *text,
                             lv_color_t  bg,
                             lv_color_t  fg,
                             lv_color_t  border,
                             lv_obj_t  **out_pill,
                             lv_obj_t  **out_lbl)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(pill, border, LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(pill, SZ_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(pill, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(pill, 12, LV_PART_MAIN);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(pill);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FONT_MONO_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, fg, LV_PART_MAIN);
    lv_obj_center(lbl);

    if (out_pill) *out_pill = pill;
    if (out_lbl)  *out_lbl  = lbl;
    return pill;
}

// ─── Status row ───────────────────────────────────────────────────────────────
// Returns the value label (and optionally the badge pill/label for dynamic rows).
static lv_obj_t *make_status_row(lv_obj_t *col,
                                  const char *key_text,
                                  const char *val_text,
                                  lv_color_t  badge_bg,
                                  lv_color_t  badge_fg,
                                  lv_color_t  badge_border,
                                  const char *badge_text,
                                  lv_obj_t  **out_badge_pill,
                                  lv_obj_t  **out_badge_lbl)
{
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_set_size(row, LV_PCT(100), 64);
    lv_obj_set_style_bg_color(row, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, SZ_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, COL_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 10, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                               LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                                LV_FLEX_ALIGN_START,
                                LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(left, 2, LV_PART_MAIN);

    lv_obj_t *key = lv_label_create(left);
    lv_label_set_text(key, key_text);
    lv_obj_set_style_text_font(key, FONT_MONO_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(key, COL_TX3, LV_PART_MAIN);

    lv_obj_t *val = lv_label_create(left);
    lv_label_set_text(val, val_text);
    lv_obj_set_style_text_font(val, FONT_MONO_XS, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, COL_TX, LV_PART_MAIN);

    make_badge(row, badge_text, badge_bg, badge_fg, badge_border,
               out_badge_pill, out_badge_lbl);

    return val;
}

// ─── Periodic refresh timer ───────────────────────────────────────────────────
static void home_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (g_current_view != VIEW_HOME) return;  // don't format/update a hidden screen
    ui_home_refresh();
}

// ─── ui_home_create ───────────────────────────────────────────────────────────
void ui_home_create(lv_obj_t *parent)
{
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_screen, 14, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    g_screens[VIEW_HOME] = s_screen;

    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_CENTER,
                                    LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_screen, 12, LV_PART_MAIN);

    // ── 2×2 KPI grid ─────────────────────────────────────────────────────────
    lv_obj_t *grid = lv_obj_create(s_screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                    LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT,
                                    LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_column(grid, SZ_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, SZ_GAP, LV_PART_MAIN);

    static const struct { const char *label, *val, *meta; } kpi[4] = {
        { "RUNNING",      "0 / 0", "Loading..."       },
        { "PULSES TODAY", "0",     "00:00:00"          },
        { "MQTT MSGS",    "0",     "since boot"        },
        { "WIFI RSSI",    "--",    "dBm"               },
    };
    lv_color_t acc[4] = { COL_HI, COL_HI, COL_WARN, COL_TEAL };

    lv_obj_t *cards[4];
    for (int i = 0; i < 4; i++) {
        cards[i] = make_kpi_card(grid, i, kpi[i].label, kpi[i].val, kpi[i].meta, acc[i]);
    }
    // s_kpi_meta[1] will hold the uptime counter
    s_uptime_lbl = s_kpi_meta[1];

    lv_obj_add_flag(cards[2], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cards[2], mqtt_kpi_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(cards[3], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cards[3], wifi_kpi_cb, LV_EVENT_CLICKED, NULL);

    // ── Status rows column ────────────────────────────────────────────────────
    lv_obj_t *rows_col = lv_obj_create(s_screen);
    lv_obj_remove_style_all(rows_col);
    lv_obj_set_size(rows_col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(rows_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(rows_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(rows_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rows_col, LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(rows_col, 8, LV_PART_MAIN);

    // Row 0: MQTT Broker (dynamic connected/disconnected badge)
    s_mqtt_val_lbl = make_status_row(rows_col,
                    "MQTT BROKER",
                    MQTT_BROKER_URI,
                    COL_OK_LT, COL_OK, lv_color_hex(0x9ADDB8),
                    "CONNECTED",
                    &s_mqtt_badge, &s_mqtt_badge_lbl);
    lv_obj_t *mqtt_broker_row = lv_obj_get_child(rows_col, 0);
    lv_obj_add_flag(mqtt_broker_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mqtt_broker_row, mqtt_kpi_cb, LV_EVENT_CLICKED, NULL);

    // Row 1: WiFi / Device IP (shows SSID · IP when connected)
    s_ip_label = make_status_row(rows_col,
                    "WIFI",
                    g_wifi_connected ? g_wifi_ssid : "Not connected",
                    COL_HI_LT, COL_HI, lv_color_hex(0xC5DCFF),
                    "DHCP",
                    NULL, NULL);
    lv_obj_t *wifi_row = lv_obj_get_child(rows_col, 1);
    lv_obj_add_flag(wifi_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_row, wifi_kpi_cb, LV_EVENT_CLICKED, NULL);

    // Row 2: Active Operator
    s_operator_label = make_status_row(rows_col,
                                       "OPERATOR",
                                       "-",
                                       COL_OK_LT, COL_OK, lv_color_hex(0x9ADDB8),
                                       "ACTIVE",
                                       NULL, NULL);

    // Periodic timer: refresh all live data (only while Home is the active tab)
    lv_timer_create(home_timer_cb, UI_REFRESH_MS, NULL);

    ui_home_refresh();
}

// ─── ui_home_refresh ─────────────────────────────────────────────────────────
void ui_home_refresh(void)
{
    if (!s_screen) return;

    // Aggregate machine stats under the lock, render after release
    int  running = 0, total_pulses = 0;
    char stopped_name[32] = "";
    DATA_LOCK();
    for (int i = 0; i < g_machine_count; i++) {
        if (g_machines[i].running) running++;
        else if (!stopped_name[0])
            strncpy(stopped_name, g_machines[i].name, sizeof(stopped_name) - 1);
        total_pulses += g_machines[i].pulses;
    }
    int machine_count = g_machine_count;
    DATA_UNLOCK();

    // KPI 0: Running / Total machines
    lv_label_set_text_fmt(s_kpi_val[0], "%d / %d", running, machine_count);
    if (stopped_name[0])
        lv_label_set_text_fmt(s_kpi_meta[0], "%s stopped", stopped_name);
    else
        lv_label_set_text(s_kpi_meta[0], "All running");

    // KPI 1: Pulses today; meta = uptime HH:MM:SS
    lv_label_set_text_fmt(s_kpi_val[1], "%d", total_pulses);

    if (s_uptime_lbl) {
        int64_t secs = esp_timer_get_time() / 1000000LL;
        int h = (int)(secs / 3600);
        int m = (int)((secs % 3600) / 60);
        int s = (int)(secs % 60);
        lv_label_set_text_fmt(s_uptime_lbl, "%02d:%02d:%02d", h, m, s);
    }

    // KPI 2: MQTT message count
    lv_label_set_text_fmt(s_kpi_val[2], "%d", g_mqtt_msg_count);

    // KPI 3: WiFi RSSI (g_wifi_rssi is maintained by the wifi_manager event handler)
    if (g_wifi_connected)
        lv_label_set_text_fmt(s_kpi_val[3], "%d", g_wifi_rssi);
    else
        lv_label_set_text(s_kpi_val[3], "--");

    // MQTT Broker row: update connected/disconnected badge
    if (s_mqtt_badge && s_mqtt_badge_lbl) {
        if (g_mqtt_connected) {
            lv_obj_set_style_bg_color(s_mqtt_badge, COL_OK_LT, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_mqtt_badge, lv_color_hex(0x9ADDB8), LV_PART_MAIN);
            lv_label_set_text(s_mqtt_badge_lbl, "CONNECTED");
            lv_obj_set_style_text_color(s_mqtt_badge_lbl, COL_OK, LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(s_mqtt_badge, lv_color_hex(0xFFF0F0), LV_PART_MAIN);
            lv_obj_set_style_border_color(s_mqtt_badge, lv_color_hex(0xFFB3B3), LV_PART_MAIN);
            lv_label_set_text(s_mqtt_badge_lbl, "OFFLINE");
            lv_obj_set_style_text_color(s_mqtt_badge_lbl, COL_ERR, LV_PART_MAIN);
        }
    }

    // WiFi row: SSID · IP when connected, "Not connected" when not
    if (s_ip_label) {
        if (g_wifi_connected && g_wifi_ssid[0]) {
            char wifi_val[64];
            snprintf(wifi_val, sizeof(wifi_val), "%s  \xc2\xb7  %s", g_wifi_ssid, g_device_ip);
            lv_label_set_text(s_ip_label, wifi_val);
        } else {
            lv_label_set_text(s_ip_label, "Not connected");
        }
    }

    // Operator row: name + role
    if (s_operator_label) {
        if (g_active_badge >= 0 && g_active_badge < g_badge_count) {
            char op_val[160];
            snprintf(op_val, sizeof(op_val), "%s  -  %s",
                     g_badges[g_active_badge].name,
                     g_badges[g_active_badge].role);
            lv_label_set_text(s_operator_label, op_val);
        } else {
            lv_label_set_text(s_operator_label, "No operator scanned");
        }
    }
}
