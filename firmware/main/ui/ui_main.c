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
#include "data.h"
#include "esp_log.h"
#include "ui_wifi_setup.h"
#include "ui_mqtt_detail.h"
#include "ui_wifi_password.h"
#include <string.h>
#include <stdio.h>

// ─── Forward declarations for screen modules ──────────────────────────────────
void ui_home_create(lv_obj_t *parent);
void ui_home_refresh(void);
void ui_machines_create(lv_obj_t *parent);
void ui_machines_refresh(void);
void ui_machines_activate(void);
void ui_config_select_create(lv_obj_t *parent);
void ui_config_select_activate(void);
void ui_tanks_create(lv_obj_t *parent);
void ui_tanks_activate(void);
void ui_tanks_deactivate(void);
void ui_debug_create(lv_obj_t *parent);
void ui_debug_refresh(void);
void ui_debug_activate(void);
void ui_debug_deactivate(void);
void ui_nfc_create(lv_obj_t *parent);
void ui_detail_create(lv_obj_t *parent);
void ui_detail_step(int direction);
void ui_tank_overview_create(lv_obj_t *parent);
void ui_tank_overview_activate(int tank_idx);
void ui_tank_overview_deactivate(void);

static const char *TAG = "ui";

// ─── Public globals ───────────────────────────────────────────────────────────
lv_obj_t *g_screens[VIEW_COUNT] = {NULL};
lv_obj_t *g_shell_bar           = NULL;
lv_obj_t *g_tab_bar             = NULL;
lv_obj_t *g_crumb_bar           = NULL;
lv_obj_t *g_content             = NULL;
lv_obj_t *g_shell_subtitle      = NULL;
ViewId    g_current_view        = VIEW_HOME;
ViewId    g_prev_view           = VIEW_HOME;

static lv_obj_t *s_crumb_label  = NULL;
static int       s_tank_ovw_idx = 0;
static char      s_tank_ovw_crumb[80] = "";

// ─── Tab metadata ─────────────────────────────────────────────────────────────
static const char *TAB_ICONS[]  = {
    LV_SYMBOL_HOME,
    LV_SYMBOL_SETTINGS,
    LV_SYMBOL_IMAGE,     // Tanks placeholder (no tank symbol in built-in set)
    LV_SYMBOL_LIST,
    LV_SYMBOL_CALL,   // People tab (person/badge icon closest available)
};
static const char *TAB_LABELS[] = { "Home", "Machines", "Tanks", "Debug", "People" };

// Per-tab button + active-indicator bar (static; accessed by ui_switch_tab)
static lv_obj_t *s_tab_btns[5]       = {NULL};
static lv_obj_t *s_tab_indicators[5] = {NULL};  // 144×3 top bar, shown when active
static lv_obj_t *s_tab_cols[5]       = {NULL};  // inner icon+label column per tab

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
static void wifi_pill_cb(lv_event_t *e) { (void)e; ui_push_wifi_setup(); }
static void mqtt_pill_cb(lv_event_t *e) { (void)e; ui_push_mqtt_detail(); }

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

    // ── Left group: SAP logo + title column ──────────────────────────────────
    lv_obj_t *left_group = lv_obj_create(g_shell_bar);
    lv_obj_set_size(left_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_group, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(left_group, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(left_group, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(left_group, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(left_group, 0, LV_PART_MAIN);
    lv_obj_remove_flag(left_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(left_group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_group, 10, LV_PART_MAIN);
    lv_obj_set_flex_grow(left_group, 1);   // expands to push status pills right

    // SAP geometric logo image (68×44px RGB565)
    extern const lv_image_dsc_t sap_logo;
    lv_obj_t *logo = lv_image_create(left_group);
    lv_image_set_src(logo, &sap_logo);
    lv_obj_remove_flag(logo, LV_OBJ_FLAG_SCROLLABLE);

    // Title + subtitle column
    lv_obj_t *title_col = lv_obj_create(left_group);
    lv_obj_set_size(title_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_col, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(title_col, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(title_col, 0, LV_PART_MAIN);
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
                          "SAP Digital Manufacturing - %s",
                          g_plant_name[0] ? g_plant_name : PLANT_NAME);
    lv_obj_set_style_text_font(g_shell_subtitle, FONT_MONO_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_shell_subtitle, COL_TX2, LV_PART_MAIN);

    // ── Right group: MQTT pill + WiFi pill ────────────────────────────────────
    lv_obj_t *right_group = lv_obj_create(g_shell_bar);
    lv_obj_set_size(right_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_group, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(right_group, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(right_group, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(right_group, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(right_group, 0, LV_PART_MAIN);
    lv_obj_remove_flag(right_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(right_group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_group, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_group, 8, LV_PART_MAIN);

    lv_obj_t *s_mqtt_pill = create_status_pill(right_group, "MQTT",
                       COL_OK, &s_mqtt_dot, &s_mqtt_lbl);
    lv_obj_add_flag(s_mqtt_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_mqtt_pill, mqtt_pill_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *s_wifi_pill = create_status_pill(right_group, "WiFi",
                       COL_OK, &s_wifi_dot, &s_wifi_lbl);
    lv_obj_add_flag(s_wifi_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wifi_pill, wifi_pill_cb, LV_EVENT_CLICKED, NULL);
}

// ─── Crumb bar ────────────────────────────────────────────────────────────────
static void crumb_back_cb(lv_event_t *e)
{
    (void)e;
    ui_pop_detail();
}

static void build_crumb_bar(void)
{
    g_crumb_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_crumb_bar, SZ_SCREEN, SZ_CRUMB);
    lv_obj_set_pos(g_crumb_bar, 0, SZ_SHELL);
    lv_obj_set_style_bg_color(g_crumb_bar, COL_BASE2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_crumb_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_crumb_bar, COL_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_crumb_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(g_crumb_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_radius(g_crumb_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(g_crumb_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(g_crumb_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(g_crumb_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_crumb_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(g_crumb_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_crumb_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_crumb_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(g_crumb_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_crumb_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_crumb_bar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_crumb_bar, 8, LV_PART_MAIN);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(g_crumb_bar);
    lv_obj_set_size(back_btn, LV_SIZE_CONTENT, SZ_CRUMB - 8);
    lv_obj_set_style_bg_color(back_btn, COL_HI_LT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0xC5DCFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, SZ_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(back_btn, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(back_btn, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, crumb_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "< Back");
    lv_obj_set_style_text_font(back_lbl, FONT_SANS_XS, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_lbl, COL_HI, LV_PART_MAIN);
    lv_obj_center(back_lbl);

    // Separator dot
    lv_obj_t *sep = lv_label_create(g_crumb_bar);
    lv_label_set_text(sep, "/");
    lv_obj_set_style_text_font(sep, FONT_SANS_XS, LV_PART_MAIN);
    lv_obj_set_style_text_color(sep, COL_TX3, LV_PART_MAIN);

    // Breadcrumb current-item label
    s_crumb_label = lv_label_create(g_crumb_bar);
    lv_label_set_text(s_crumb_label, "");
    lv_obj_set_style_text_font(s_crumb_label, FONT_SANS_XS, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_crumb_label, COL_TX2, LV_PART_MAIN);

    lv_obj_add_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_set_style_pad_column(g_tab_bar, 0, LV_PART_MAIN);
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
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
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
        s_tab_cols[i] = col;

        lv_obj_t *icon_lbl;
        if (i == VIEW_TANKS) {
            extern const lv_image_dsc_t tank_icon;
            lv_obj_t *img = lv_image_create(col);
            lv_image_set_src(img, &tank_icon);
            lv_obj_set_style_image_recolor(img, COL_TX3, 0);
            lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
            icon_lbl = img;
        } else {
            icon_lbl = lv_label_create(col);
            lv_label_set_text(icon_lbl, TAB_ICONS[i]);
            lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
            lv_obj_set_style_text_color(icon_lbl, COL_TX3, LV_PART_MAIN);
        }

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

        // Recolor icon + label inside the inner column
        lv_obj_t *col_obj = s_tab_cols[i];
        if (col_obj) {
            lv_obj_t *icon = lv_obj_get_child(col_obj, 0);
            lv_obj_t *lbl  = lv_obj_get_child(col_obj, 1);
            if (icon) {
                if (lv_obj_has_class(icon, &lv_image_class)) {
                    lv_obj_set_style_image_recolor(icon, col, 0);
                    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
                } else {
                    lv_obj_set_style_text_color(icon, col, LV_PART_MAIN);
                }
            }
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
    build_crumb_bar();

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

    // Build all screen modules (they create g_screens[VIEW_*] internally)
    ui_home_create(g_content);
    ui_machines_create(g_content);
    ui_tanks_create(g_content);
    ui_debug_create(g_content);
    ui_nfc_create(g_content);
    ui_detail_create(g_content);
    ui_tank_overview_create(g_content);
    ui_wifi_setup_create(g_content);
    ui_mqtt_detail_create(g_content);
    ui_wifi_password_create(g_content);
    ui_config_select_create(g_content);

    // Activate home tab as the initial view
    ui_switch_tab(VIEW_HOME);
    ESP_LOGI(TAG, "UI ready");
}

void ui_switch_tab(ViewId v)
{
    if ((int)v > VIEW_NFC) return;   // VIEW_DETAIL and VIEW_TANK_OVW not reachable via tab bar

    // Leaving the detail view by any route releases its sim edit-hold
    g_edit_machine = g_edit_tank = g_edit_metric = -1;

    // Deactivate tanks timer if leaving tanks
    if (g_current_view == VIEW_TANKS) ui_tanks_deactivate();
    if (g_current_view == VIEW_DEBUG) ui_debug_deactivate();

    // Hide all screens
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    // Show the target screen
    if (g_screens[v]) lv_obj_clear_flag(g_screens[v], LV_OBJ_FLAG_HIDDEN);

    g_current_view = v;
    g_prev_view    = v;
    tab_highlight(v);

    // Screen-specific activation callbacks
    if (v == VIEW_HOME)     ui_home_refresh();
    if (v == VIEW_MACHINES) { ui_machines_activate(); ui_machines_refresh(); }
    if (v == VIEW_TANKS)    ui_tanks_activate();
    if (v == VIEW_DEBUG)    ui_debug_activate();
}

void ui_push_detail(const char *back_label, const char *metric_label)
{
    if (g_current_view == VIEW_DETAIL) return;
    g_prev_view = g_current_view;

    // Hide all screens
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Show detail overlay
    if (g_screens[VIEW_DETAIL])
        lv_obj_clear_flag(g_screens[VIEW_DETAIL], LV_OBJ_FLAG_HIDDEN);

    // Show breadcrumb bar with current location
    if (g_crumb_bar) lv_obj_clear_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
    if (s_crumb_label) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s / %s", back_label, metric_label);
        lv_label_set_text(s_crumb_label, buf);
    }

    g_current_view = VIEW_DETAIL;
}

void ui_pop_detail(void)
{
    // Any pop out of the detail view releases its sim edit-hold
    if (g_current_view == VIEW_DETAIL)
        g_edit_machine = g_edit_tank = g_edit_metric = -1;

    if (g_current_view == VIEW_WIFI_PASSWORD) {
        for (int i = 0; i < VIEW_COUNT; i++)
            if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_screens[VIEW_WIFI_SETUP], LV_OBJ_FLAG_HIDDEN);
        if (s_crumb_label) lv_label_set_text(s_crumb_label, "WiFi Setup");
        if (g_crumb_bar)   lv_obj_clear_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
        g_current_view = VIEW_WIFI_SETUP;
        return;
    }
    if (g_current_view == VIEW_TANK_OVW) {
        ui_tank_overview_deactivate();
    }
    if (g_current_view == VIEW_WIFI_SETUP) {
        if (g_crumb_bar) lv_obj_add_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
        ui_switch_tab(g_prev_view);
        return;
    }
    if (g_prev_view == VIEW_TANK_OVW) {
        // Return from Detail → Tank Overview (not all the way to Tanks)
        for (int i = 0; i < VIEW_COUNT; i++) {
            if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(g_screens[VIEW_TANK_OVW], LV_OBJ_FLAG_HIDDEN);
        if (s_crumb_label) lv_label_set_text(s_crumb_label, s_tank_ovw_crumb);
        g_prev_view    = VIEW_TANKS;
        g_current_view = VIEW_TANK_OVW;
        ui_tank_overview_activate(s_tank_ovw_idx);  // refresh metric tiles with current values
    } else {
        // Standard: hide crumb bar, return to tab view
        if (g_crumb_bar) lv_obj_add_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
        ui_switch_tab(g_prev_view);
    }
}

void ui_push_tank_ovw(int tank_idx)
{
    if (g_current_view == VIEW_DETAIL) return;
    ui_tank_overview_deactivate();   // stop any prior animation
    g_prev_view = g_current_view;
    s_tank_ovw_idx = tank_idx;
    snprintf(s_tank_ovw_crumb, sizeof(s_tank_ovw_crumb),
             "Tanks / %s", g_tanks[tank_idx].name);

    for (int i = 0; i < VIEW_COUNT; i++) {
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (g_screens[VIEW_TANK_OVW])
        lv_obj_clear_flag(g_screens[VIEW_TANK_OVW], LV_OBJ_FLAG_HIDDEN);
    if (g_crumb_bar) lv_obj_clear_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
    if (s_crumb_label) lv_label_set_text(s_crumb_label, s_tank_ovw_crumb);

    g_current_view = VIEW_TANK_OVW;
    ui_tank_overview_activate(tank_idx);
}

void ui_push_wifi_setup(void)
{
    if (g_current_view == VIEW_WIFI_SETUP) return;
    g_prev_view = g_current_view;
    for (int i = 0; i < VIEW_COUNT; i++)
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_screens[VIEW_WIFI_SETUP], LV_OBJ_FLAG_HIDDEN);
    if (s_crumb_label) lv_label_set_text(s_crumb_label, "WiFi Setup");
    if (g_crumb_bar)   lv_obj_clear_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
    g_current_view = VIEW_WIFI_SETUP;
    ui_wifi_setup_activate();
}

void ui_push_mqtt_detail(void)
{
    if (g_current_view == VIEW_MQTT_DETAIL) return;
    g_prev_view = g_current_view;
    for (int i = 0; i < VIEW_COUNT; i++)
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_screens[VIEW_MQTT_DETAIL], LV_OBJ_FLAG_HIDDEN);
    if (s_crumb_label) lv_label_set_text(s_crumb_label, "MQTT Connection");
    if (g_crumb_bar)   lv_obj_clear_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
    g_current_view = VIEW_MQTT_DETAIL;
    ui_mqtt_detail_refresh();
}

void ui_push_config_select(void)
{
    if (g_current_view == VIEW_CONFIG_SEL) return;
    g_prev_view = g_current_view;
    for (int i = 0; i < VIEW_COUNT; i++)
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_screens[VIEW_CONFIG_SEL], LV_OBJ_FLAG_HIDDEN);
    if (s_crumb_label) lv_label_set_text(s_crumb_label, "Configuration");
    if (g_crumb_bar)   lv_obj_clear_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
    g_current_view = VIEW_CONFIG_SEL;
    ui_config_select_activate();
}

void ui_push_wifi_password(const char *ssid)
{
    if (g_current_view == VIEW_WIFI_PASSWORD) return;
    g_prev_view = g_current_view;
    for (int i = 0; i < VIEW_COUNT; i++)
        if (g_screens[i]) lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_screens[VIEW_WIFI_PASSWORD], LV_OBJ_FLAG_HIDDEN);
    if (s_crumb_label) lv_label_set_text(s_crumb_label, ssid);
    if (g_crumb_bar)   lv_obj_clear_flag(g_crumb_bar, LV_OBJ_FLAG_HIDDEN);
    g_current_view = VIEW_WIFI_PASSWORD;
    ui_wifi_password_activate(ssid);
}

// ─── Status pill helpers ──────────────────────────────────────────────────────
static void set_pill_state(lv_obj_t *dot, lv_obj_t *lbl,
                           bool connected,
                           const char *ok_text, const char *err_text)
{
    if (!dot || !lbl) return;
    if (connected) {
        lv_obj_set_style_bg_color(dot, COL_OK, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_OK, LV_PART_MAIN);
        lv_label_set_text(lbl, ok_text);
    } else {
        lv_obj_set_style_bg_color(dot, COL_ERR, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_ERR, LV_PART_MAIN);
        lv_label_set_text(lbl, err_text);
    }
}

void ui_notify_mqtt_status(bool connected)
{
    set_pill_state(s_mqtt_dot, s_mqtt_lbl, connected, "MQTT", "Offline");
}

void ui_notify_wifi_status(bool connected, int rssi, const char *ip)
{
    set_pill_state(s_wifi_dot, s_wifi_lbl, connected, "WiFi", "No WiFi");
    g_wifi_connected = connected;
    g_wifi_rssi = rssi;
    if (connected && ip) {
        strncpy(g_device_ip, ip, sizeof(g_device_ip) - 1);
    } else if (!connected) {
        strncpy(g_device_ip, "0.0.0.0", sizeof(g_device_ip) - 1);
    }
}

// ─── Hardware button callbacks ────────────────────────────────────────────────
void ui_btn_short_press(void)
{
    if (g_current_view > VIEW_NFC)      // any overlay screen
        ui_pop_detail();
    else if (g_current_view != VIEW_HOME)
        ui_switch_tab(VIEW_HOME);
}

void ui_btn_long_press(void)
{
    if (g_current_view == VIEW_DETAIL) ui_detail_step(+1);
}
