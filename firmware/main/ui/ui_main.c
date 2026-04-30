// ui_main.c — Shell bar, tab bar, screen containers, and navigation.
//
// Provides the persistent chrome (top shell + bottom tab bar) that appears on
// all screens, six content containers (VIEW_HOME … VIEW_DETAIL), and the
// three navigation functions: ui_switch_tab / ui_push_detail / ui_pop_detail.
//
// All screen modules (ui_home, ui_machines, …) receive g_content as their
// parent and register themselves in g_screens[VIEW_*].

#include "ui_main.h"
#include "ui_theme.h"
#include "config.h"
#include "data_model.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

// ─── Forward declarations for screen modules ──────────────────────────────────
void ui_home_create(lv_obj_t *parent);
void ui_home_refresh(void);
void ui_machines_create(lv_obj_t *parent);
void ui_machines_refresh(void);
void ui_machines_pulse_first(void);
void ui_tanks_create(lv_obj_t *parent);
void ui_tanks_activate(void);
void ui_debug_create(lv_obj_t *parent);
void ui_debug_refresh(void);
void ui_nfc_create(lv_obj_t *parent);
void ui_detail_create(lv_obj_t *parent);
void ui_detail_step(int direction);

static const char *TAG = "ui";

// ─── Public globals ───────────────────────────────────────────────────────────
lv_obj_t *g_screens[VIEW_COUNT] = {NULL};
lv_obj_t *g_shell_bar           = NULL;
lv_obj_t *g_tab_bar             = NULL;
lv_obj_t *g_content             = NULL;
lv_obj_t *g_shell_subtitle      = NULL;
ViewId    g_current_view        = VIEW_HOME;
ViewId    g_prev_view           = VIEW_HOME;

// ─── Tab metadata ─────────────────────────────────────────────────────────────
static const char *TAB_ICONS[]  = {
    LV_SYMBOL_HOME,
    LV_SYMBOL_SETTINGS,
    LV_SYMBOL_IMAGE,     // Tanks placeholder (no tank symbol in built-in set)
    LV_SYMBOL_LIST,
    LV_SYMBOL_WIFI,
};
static const char *TAB_LABELS[] = { "Home", "Machines", "Tanks", "Debug", "NFC" };

// Per-tab button + active-indicator bar (static; accessed by ui_switch_tab)
static lv_obj_t *s_tab_btns[5]       = {NULL};
static lv_obj_t *s_tab_indicators[5] = {NULL};  // 144×3 top bar, shown when active

// Status pills (color updated by notify functions)
static lv_obj_t *s_mqtt_dot  = NULL;
static lv_obj_t *s_mqtt_lbl  = NULL;
static lv_obj_t *s_wifi_dot  = NULL;
static lv_obj_t *s_wifi_lbl  = NULL;

// ─── Helper: create a status pill ─────────────────────────────────────────────
// Returns the colored-dot object so callers can update its bg color.
static lv_obj_t *create_status_pill(lv_obj_t *parent,
                                    const char *label_text,
                                    lv_color_t dot_color,
                                    lv_obj_t **out_dot,
                                    lv_obj_t **out_lbl)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_bg_color(pill, COL_OK_LT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(pill, lv_color_hex(0x9ADDB8), LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_left(pill, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(pill, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(pill, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(pill, 0, LV_PART_MAIN);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(pill, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 5, LV_PART_MAIN);

    // Colored dot
    lv_obj_t *dot = lv_obj_create(pill);
    lv_obj_set_size(dot, SZ_DOT, SZ_DOT);
    lv_obj_set_style_bg_color(dot, dot_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    // Text label
    lv_obj_t *lbl = lv_label_create(pill);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, FONT_SANS_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, COL_OK, LV_PART_MAIN);

    if (out_dot) *out_dot = dot;
    if (out_lbl) *out_lbl = lbl;
    return pill;
}

// ─── Shell bar ────────────────────────────────────────────────────────────────
static void build_shell_bar(void)
{
    g_shell_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_shell_bar, SZ_SCREEN, SZ_SHELL);
    lv_obj_set_pos(g_shell_bar, 0, 0);
    lv_obj_set_style_bg_color(g_shell_bar, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_shell_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_shell_bar, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_shell_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_side(g_shell_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_radius(g_shell_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(g_shell_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(g_shell_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(g_shell_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_shell_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_shell_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(g_shell_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_shell_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_shell_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_shell_bar, 10, LV_PART_MAIN);

    // ── Left group: SAP badge + title column ──────────────────────────────────
    lv_obj_t *left_group = lv_obj_create(g_shell_bar);
    lv_obj_set_size(left_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_group, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(left_group, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(left_group, 0, LV_PART_MAIN);
    lv_obj_remove_flag(left_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(left_group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_group, 10, LV_PART_MAIN);
    lv_obj_set_flex_grow(left_group, 1);   // expands to push status pills right

    // SAP badge square
    lv_obj_t *badge = lv_obj_create(left_group);
    lv_obj_set_size(badge, SZ_BADGE_SAP, SZ_BADGE_SAP);
    lv_obj_set_style_bg_color(badge, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(badge, SZ_RADIUS_SM, LV_PART_MAIN);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sap_lbl = lv_label_create(badge);
    lv_label_set_text(sap_lbl, "SAP");
    lv_obj_set_style_text_font(sap_lbl, FONT_MONO_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(sap_lbl, COL_BASE, LV_PART_MAIN);
    lv_obj_center(sap_lbl);

    // Title + subtitle column
    lv_obj_t *title_col = lv_obj_create(left_group);
    lv_obj_set_size(title_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(title_col, 0, LV_PART_MAIN);
    lv_obj_remove_flag(title_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(title_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(title_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(title_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(title_col, 2, LV_PART_MAIN);

    lv_obj_t *title_lbl = lv_label_create(title_col);
    lv_label_set_text(title_lbl, "Shop Floor Simulator");
    lv_obj_set_style_text_font(title_lbl, FONT_SANS_SM, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_lbl, COL_TX, LV_PART_MAIN);

    g_shell_subtitle = lv_label_create(title_col);
    lv_label_set_text_fmt(g_shell_subtitle,
                          "SAP Digital Manufacturing \xc2\xb7 %s", PLANT_NAME);
    lv_obj_set_style_text_font(g_shell_subtitle, FONT_MONO_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_shell_subtitle, COL_TX2, LV_PART_MAIN);

    // ── Right group: MQTT pill + WiFi pill ────────────────────────────────────
    lv_obj_t *right_group = lv_obj_create(g_shell_bar);
    lv_obj_set_size(right_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_group, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(right_group, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(right_group, 0, LV_PART_MAIN);
    lv_obj_remove_flag(right_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(right_group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_group, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_group, 8, LV_PART_MAIN);

    create_status_pill(right_group, "MQTT",
                       COL_OK, &s_mqtt_dot, &s_mqtt_lbl);
    create_status_pill(right_group, "WiFi",
                       COL_OK, &s_wifi_dot, &s_wifi_lbl);
}

// ─── Tab bar ──────────────────────────────────────────────────────────────────
static void tab_btn_cb(lv_event_t *e)
{
    ViewId v = (ViewId)(intptr_t)lv_event_get_user_data(e);
    ui_switch_tab(v);
}

static void build_tab_bar(void)
{
    g_tab_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_tab_bar, SZ_SCREEN, SZ_TAB);
    lv_obj_set_pos(g_tab_bar, 0, SZ_SCREEN - SZ_TAB);
    lv_obj_set_style_bg_color(g_tab_bar, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_tab_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_tab_bar, COL_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_tab_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(g_tab_bar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_radius(g_tab_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_tab_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_tab_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(g_tab_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_tab_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    int btn_w = SZ_SCREEN / 5;   // 144px each

    for (int i = 0; i < 5; i++) {
        // Button container — full tab bar height, fixed width
        lv_obj_t *btn = lv_obj_create(g_tab_bar);
        lv_obj_set_size(btn, btn_w, SZ_TAB);
        lv_obj_set_style_bg_color(btn, COL_BASE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, COL_HI_LT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, tab_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_tab_btns[i] = btn;

        // Active indicator: 144×3 bar at top of button, hidden by default
        lv_obj_t *bar = lv_obj_create(btn);
        lv_obj_set_size(bar, btn_w, 3);
        lv_obj_set_pos(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, COL_HI, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        s_tab_indicators[i] = bar;

        // Inner column: icon + label, centered inside button
        lv_obj_t *col = lv_obj_create(btn);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_center(col);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(col, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 2, LV_PART_MAIN);

        lv_obj_t *icon_lbl = lv_label_create(col);
        lv_label_set_text(icon_lbl, TAB_ICONS[i]);
        lv_obj_set_style_text_font(icon_lbl, FONT_SANS_MD, LV_PART_MAIN);
        lv_obj_set_style_text_color(icon_lbl, COL_TX3, LV_PART_MAIN);

        lv_obj_t *text_lbl = lv_label_create(col);
        lv_label_set_text(text_lbl, TAB_LABELS[i]);
        lv_obj_set_style_text_font(text_lbl, FONT_SANS_XXS, LV_PART_MAIN);
        lv_obj_set_style_text_color(text_lbl, COL_TX3, LV_PART_MAIN);
    }
}

// ─── Update tab highlight state ───────────────────────────────────────────────
static void tab_highlight(ViewId v)
{
    for (int i = 0; i < 5; i++) {
        if (!s_tab_btns[i]) continue;

        bool active = (i == (int)v);
        lv_color_t col = active ? COL_HI : COL_TX3;

        // Show/hide indicator bar
        if (s_tab_indicators[i]) {
            if (active)
                lv_obj_clear_flag(s_tab_indicators[i], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(s_tab_indicators[i], LV_OBJ_FLAG_HIDDEN);
        }

        // Recolor icon + label inside the inner column (child 1, after indicator bar)
        lv_obj_t *col_obj = lv_obj_get_child(s_tab_btns[i], 1);
        if (col_obj) {
            lv_obj_t *icon = lv_obj_get_child(col_obj, 0);
            lv_obj_t *lbl  = lv_obj_get_child(col_obj, 1);
            if (icon) lv_obj_set_style_text_color(icon, col, LV_PART_MAIN);
            if (lbl)  lv_obj_set_style_text_color(lbl,  col, LV_PART_MAIN);
        }
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void ui_main_init(void)
{
    ESP_LOGI(TAG, "building UI");

    // Root screen background
    lv_obj_set_style_bg_color(lv_scr_act(), COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    build_shell_bar();

    // Content area — sits between shell bar and tab bar
    g_content = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_content, CONTENT_W, CONTENT_H);
    lv_obj_set_pos(g_content, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(g_content, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_content, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);

    build_tab_bar();

    // Build all six screen modules (they create g_screens[VIEW_*] internally)
    ui_home_create(g_content);
    ui_machines_create(g_content);
    ui_tanks_create(g_content);
    ui_debug_create(g_content);
    ui_nfc_create(g_content);
    ui_detail_create(g_content);

    // Activate home tab as the initial view
    ui_switch_tab(VIEW_HOME);
    ESP_LOGI(TAG, "UI ready");
}

void ui_switch_tab(ViewId v)
{
    if (v > VIEW_NFC) return;   // VIEW_DETAIL is not reachable via tab bar

    // Hide all screens
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    // Show the target screen
    if (g_screens[v]) lv_obj_clear_flag(g_screens[v], LV_OBJ_FLAG_HIDDEN);

    g_current_view = v;
    g_prev_view    = v;   // used as fallback for pop_detail
    tab_highlight(v);

    // Screen-specific activation callbacks
    if (v == VIEW_HOME)     ui_home_refresh();
    if (v == VIEW_MACHINES) ui_machines_refresh();
    if (v == VIEW_TANKS)    ui_tanks_activate();
    if (v == VIEW_DEBUG)    ui_debug_refresh();
}

void ui_push_detail(const char *back_label, const char *metric_label)
{
    g_prev_view = g_current_view;

    // Hide all screens and the tab bar
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (g_tab_bar) lv_obj_add_flag(g_tab_bar, LV_OBJ_FLAG_HIDDEN);

    // Show detail overlay
    if (g_screens[VIEW_DETAIL])
        lv_obj_clear_flag(g_screens[VIEW_DETAIL], LV_OBJ_FLAG_HIDDEN);

    // Update shell subtitle to breadcrumb
    if (g_shell_subtitle) {
        char buf[64];
        snprintf(buf, sizeof(buf), "\xe2\x86\x90 %s / %s", back_label, metric_label);
        lv_label_set_text(g_shell_subtitle, buf);
    }

    g_current_view = VIEW_DETAIL;
}

void ui_pop_detail(void)
{
    // Restore tab bar
    if (g_tab_bar) lv_obj_clear_flag(g_tab_bar, LV_OBJ_FLAG_HIDDEN);

    // Restore shell subtitle
    if (g_shell_subtitle) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "SAP Digital Manufacturing \xc2\xb7 %s", PLANT_NAME);
        lv_label_set_text(g_shell_subtitle, buf);
    }

    ui_switch_tab(g_prev_view);
}

// ─── Status pill helpers ──────────────────────────────────────────────────────
static void set_pill_connected(lv_obj_t *dot, lv_obj_t *lbl, bool connected)
{
    if (!dot || !lbl) return;
    if (connected) {
        lv_obj_set_style_bg_color(dot, COL_OK, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_OK, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(dot, COL_TX3, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_TX3, LV_PART_MAIN);
    }
}

void ui_notify_mqtt_status(bool connected)
{
    set_pill_connected(s_mqtt_dot, s_mqtt_lbl, connected);
}

void ui_notify_wifi_status(bool connected, int rssi, const char *ip)
{
    set_pill_connected(s_wifi_dot, s_wifi_lbl, connected);
    g_wifi_rssi = rssi;
    if (ip) strncpy(g_device_ip, ip, sizeof(g_device_ip) - 1);
}

// ─── Hardware button callbacks ────────────────────────────────────────────────
void ui_btn_short_press(void)
{
    if (g_current_view == VIEW_DETAIL) ui_detail_step(-1);
    else                               ui_machines_pulse_first();
}

void ui_btn_long_press(void)
{
    if (g_current_view == VIEW_DETAIL) ui_detail_step(+1);
}
