#include "ui_main.h"
#include "ui_theme.h"
#include "ui_home.h"
#include "ui_machines.h"
#include "ui_tanks.h"
#include "ui_debug.h"
#include "ui_nfc.h"
#include "ui_detail.h"
#include "data_model.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui";

lv_obj_t *g_shell_bar  = NULL;
lv_obj_t *g_tab_bar    = NULL;
lv_obj_t *g_content    = NULL;
lv_obj_t *g_screens[6] = {NULL};
ViewId    g_current_view = VIEW_HOME;
static ViewId s_return_tab = VIEW_HOME;

// ─── Tab bar icons (simple Unicode placeholders; replace with icon font) ─────
static const char *TAB_ICONS[]  = { LV_SYMBOL_HOME, LV_SYMBOL_SETTINGS,
                                    LV_SYMBOL_IMAGE, LV_SYMBOL_LIST,
                                    LV_SYMBOL_WIFI };
static const char *TAB_LABELS[] = { "Home", "Machines", "Tanks", "Debug", "NFC" };
static lv_obj_t   *s_tab_btns[5] = {NULL};

// ─── Shell bar ───────────────────────────────────────────────────────────────
static lv_obj_t *s_mqtt_pill = NULL;
static lv_obj_t *s_wifi_pill = NULL;

static void build_shell_bar(void) {
    g_shell_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_shell_bar, SZ_SCREEN, SZ_SHELL);
    lv_obj_set_pos(g_shell_bar, 0, 0);
    lv_obj_set_style_bg_color(g_shell_bar, COL_BASE, 0);
    lv_obj_set_style_border_color(g_shell_bar, COL_HI, 0);
    lv_obj_set_style_border_width(g_shell_bar, 0, 0);
    lv_obj_set_style_border_side(g_shell_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(g_shell_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(g_shell_bar, 0, 0);
    lv_obj_set_style_pad_all(g_shell_bar, 0, 0);

    // SAP badge
    lv_obj_t *badge = lv_obj_create(g_shell_bar);
    lv_obj_set_size(badge, SZ_BADGE_SAP, SZ_BADGE_SAP);
    lv_obj_set_pos(badge, 10, (SZ_SHELL - SZ_BADGE_SAP) / 2);
    lv_obj_set_style_bg_color(badge, COL_HI, 0);
    lv_obj_set_style_radius(badge, 6, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_t *sap_lbl = lv_label_create(badge);
    lv_label_set_text(sap_lbl, "SAP");
    lv_obj_set_style_text_color(sap_lbl, COL_BASE, 0);
    lv_obj_set_style_text_font(sap_lbl, FONT_SANS_XXS, 0);
    lv_obj_center(sap_lbl);

    // Title
    lv_obj_t *title = lv_label_create(g_shell_bar);
    lv_label_set_text(title, "Shop Floor Simulator");
    lv_obj_set_style_text_font(title, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(title, COL_TX, 0);
    lv_obj_set_pos(title, 64, 8);

    lv_obj_t *sub = lv_label_create(g_shell_bar);
    lv_label_set_text_fmt(sub, "SAP Digital Manufacturing \xc2\xb7 %s", PLANT_NAME);
    lv_obj_set_style_text_font(sub, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(sub, COL_TX3, 0);
    lv_obj_set_pos(sub, 64, 32);

    // MQTT status pill
    s_mqtt_pill = lv_label_create(g_shell_bar);
    lv_label_set_text(s_mqtt_pill, LV_SYMBOL_WIFI " MQTT");
    lv_obj_set_style_text_font(s_mqtt_pill, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(s_mqtt_pill, COL_TX3, 0);
    lv_obj_align(s_mqtt_pill, LV_ALIGN_RIGHT_MID, -90, 0);

    // WiFi status pill
    s_wifi_pill = lv_label_create(g_shell_bar);
    lv_label_set_text(s_wifi_pill, LV_SYMBOL_WIFI " WiFi");
    lv_obj_set_style_text_font(s_wifi_pill, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(s_wifi_pill, COL_TX3, 0);
    lv_obj_align(s_wifi_pill, LV_ALIGN_RIGHT_MID, -10, 0);
}

// ─── Tab bar ─────────────────────────────────────────────────────────────────
static void tab_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_switch_tab((ViewId)idx);
}

static void build_tab_bar(void) {
    g_tab_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_tab_bar, SZ_SCREEN, SZ_TAB);
    lv_obj_set_pos(g_tab_bar, 0, SZ_SCREEN - SZ_TAB);
    lv_obj_set_style_bg_color(g_tab_bar, COL_BASE, 0);
    lv_obj_set_style_border_width(g_tab_bar, 0, 0);
    lv_obj_set_style_border_side(g_tab_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(g_tab_bar, COL_BD, 0);
    lv_obj_set_style_border_width(g_tab_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(g_tab_bar, 0, 0);
    lv_obj_set_style_pad_all(g_tab_bar, 0, 0);
    lv_obj_set_flex_flow(g_tab_bar, LV_FLEX_FLOW_ROW);

    int btn_w = SZ_SCREEN / 5;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_btn_create(g_tab_bar);
        lv_obj_set_size(btn, btn_w, SZ_TAB);
        lv_obj_set_style_bg_color(btn, COL_BASE, 0);
        lv_obj_set_style_bg_color(btn, COL_HI_LT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, tab_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_tab_btns[i] = btn;

        lv_obj_t *col = lv_obj_create(btn);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_center(col);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_t *icon = lv_label_create(col);
        lv_label_set_text(icon, TAB_ICONS[i]);
        lv_obj_set_style_text_font(icon, FONT_SANS_MD, 0);
        lv_obj_set_style_text_color(icon, COL_TX3, 0);

        lv_obj_t *lbl = lv_label_create(col);
        lv_label_set_text(lbl, TAB_LABELS[i]);
        lv_obj_set_style_text_font(lbl, FONT_SANS_XXS, 0);
        lv_obj_set_style_text_color(lbl, COL_TX3, 0);
    }
}

static void tab_highlight(ViewId v) {
    for (int i = 0; i < 5; i++) {
        if (!s_tab_btns[i]) continue;
        lv_color_t col = (i == (int)v) ? COL_HI : COL_TX3;
        // Tint icon and label of active tab blue
        lv_obj_t *col_obj = lv_obj_get_child(s_tab_btns[i], 0);
        if (col_obj) {
            lv_obj_t *icon = lv_obj_get_child(col_obj, 0);
            lv_obj_t *lbl  = lv_obj_get_child(col_obj, 1);
            if (icon) lv_obj_set_style_text_color(icon, col, 0);
            if (lbl)  lv_obj_set_style_text_color(lbl,  col, 0);
        }
        // Blue active indicator bar at top of tab
        lv_obj_set_style_border_side(s_tab_btns[i], LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(s_tab_btns[i],
            i == (int)v ? COL_HI : COL_BASE, 0);
        lv_obj_set_style_border_width(s_tab_btns[i], 3, 0);
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────
void ui_main_init(void) {
    ESP_LOGI(TAG, "building UI");
    lv_obj_set_style_bg_color(lv_scr_act(), COL_BG, 0);

    build_shell_bar();
    build_tab_bar();

    // Content area — sits between shell and tab bars
    g_content = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_content, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(g_content, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(g_content, COL_BG, 0);
    lv_obj_set_style_border_width(g_content, 0, 0);
    lv_obj_set_style_radius(g_content, 0, 0);
    lv_obj_set_style_pad_all(g_content, 0, 0);

    // Build all screens (they start hidden)
    ui_home_create(g_content);
    ui_machines_create(g_content);
    ui_tanks_create(g_content);
    ui_debug_create(g_content);
    ui_nfc_create(g_content);
    ui_detail_create(g_content);

    ui_switch_tab(VIEW_HOME);
    ESP_LOGI(TAG, "UI ready");
}

void ui_switch_tab(ViewId view) {
    if (view > VIEW_NFC) return;   // VIEW_DETAIL is not a tab

    // Hide all, show selected
    for (int i = 0; i <= VIEW_DETAIL; i++) {
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (g_screens[view]) lv_obj_clear_flag(g_screens[view], LV_OBJ_FLAG_HIDDEN);

    g_current_view = view;
    s_return_tab   = view;
    tab_highlight(view);

    // Notify screens of activation
    if (view == VIEW_TANKS)    ui_tanks_activate();
    if (view == VIEW_MACHINES) ui_machines_refresh();
    if (view == VIEW_HOME)     ui_home_refresh();
    if (view == VIEW_DEBUG)    ui_debug_refresh();
}

void ui_push_detail(void) {
    s_return_tab = g_current_view;
    for (int i = 0; i <= VIEW_DETAIL; i++) {
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (g_screens[VIEW_DETAIL])
        lv_obj_clear_flag(g_screens[VIEW_DETAIL], LV_OBJ_FLAG_HIDDEN);
    g_current_view = VIEW_DETAIL;
}

void ui_pop_detail(void) {
    ui_switch_tab(s_return_tab);
}

void ui_notify_mqtt_status(bool connected) {
    if (!s_mqtt_pill) return;
    lv_obj_set_style_text_color(s_mqtt_pill,
        connected ? COL_OK : COL_TX3, 0);
}

void ui_notify_wifi_status(bool connected, int rssi, const char *ip) {
    if (!s_wifi_pill) return;
    lv_obj_set_style_text_color(s_wifi_pill,
        connected ? COL_HI : COL_TX3, 0);
    g_wifi_rssi = rssi;
    if (ip) strncpy(g_device_ip, ip, sizeof(g_device_ip) - 1);
}

void ui_btn_short_press(void) {
    if (g_current_view == VIEW_DETAIL) ui_detail_step(-1);
    else {
        // Trigger pulse on first running machine
        extern void ui_machines_pulse_first(void);
        ui_machines_pulse_first();
    }
}

void ui_btn_long_press(void) {
    if (g_current_view == VIEW_DETAIL) ui_detail_step(+1);
}
