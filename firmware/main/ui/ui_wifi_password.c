// ui_wifi_password.c - WiFi password entry screen (VIEW_WIFI_PASSWORD = 9).
#include "ui_wifi_password.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_screen      = NULL;
static lv_obj_t *s_title_lbl   = NULL;
static lv_obj_t *s_pwd_ta      = NULL;
static lv_obj_t *s_kbd         = NULL;
static lv_obj_t *s_connect_btn = NULL;
static lv_obj_t *s_connect_lbl = NULL;
static lv_obj_t *s_show_btn    = NULL;
static bool      s_pwd_visible  = false;

static char s_pending_ssid[33] = {0};

static void connect_result_cb(bool success, const char *ip)
{
    (void)ip;
    if (success) {
        ui_pop_detail();
    } else {
        lv_label_set_text(s_connect_lbl, "Failed - retry");
        lv_obj_clear_state(s_connect_btn, LV_STATE_DISABLED);
    }
}

static void show_pwd_cb(lv_event_t *e)
{
    (void)e;
    s_pwd_visible = !s_pwd_visible;
    lv_textarea_set_password_mode(s_pwd_ta, !s_pwd_visible);
    lv_obj_t *lbl = lv_obj_get_child(s_show_btn, 0);
    if (lbl) lv_label_set_text(lbl, s_pwd_visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void connect_btn_cb(lv_event_t *e)
{
    (void)e;
    const char *pwd = lv_textarea_get_text(s_pwd_ta);
    lv_label_set_text(s_connect_lbl, "Connecting...");
    lv_obj_add_state(s_connect_btn, LV_STATE_DISABLED);
    wifi_manager_connect(s_pending_ssid, pwd, connect_result_cb);
}

void ui_wifi_password_create(lv_obj_t *parent)
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
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_screens[VIEW_WIFI_PASSWORD] = s_screen;

    s_title_lbl = lv_label_create(s_screen);
    lv_label_set_text(s_title_lbl, "");
    lv_obj_set_style_text_font(s_title_lbl, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(s_title_lbl, COL_TX2, 0);

    s_pwd_ta = lv_textarea_create(s_screen);
    lv_obj_set_size(s_pwd_ta, CONTENT_W - SZ_GAP * 2, 52);
    lv_textarea_set_password_mode(s_pwd_ta, true);
    lv_textarea_set_placeholder_text(s_pwd_ta, "Password");
    lv_textarea_set_one_line(s_pwd_ta, true);
    lv_obj_set_style_text_font(s_pwd_ta, FONT_SANS_XS, 0);
    lv_obj_set_style_border_color(s_pwd_ta, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(s_pwd_ta, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(s_pwd_ta, 0, 0);

    // Show/hide toggle sits below the textarea
    s_show_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_show_btn, CONTENT_W - SZ_GAP * 2, 40);
    lv_obj_set_style_bg_color(s_show_btn, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_show_btn, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_show_btn, COL_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_show_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_show_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(s_show_btn, 0, 0);
    lv_obj_add_event_cb(s_show_btn, show_pwd_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *show_lbl = lv_label_create(s_show_btn);
    lv_label_set_text(show_lbl, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(show_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(show_lbl, COL_TX2, 0);
    lv_obj_center(show_lbl);

    s_kbd = lv_keyboard_create(s_screen);
    lv_obj_set_size(s_kbd, CONTENT_W - SZ_GAP * 2, 280);
    lv_keyboard_set_textarea(s_kbd, s_pwd_ta);
    lv_keyboard_set_mode(s_kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_bg_color(s_kbd, COL_BASE2, 0);
    lv_obj_set_style_radius(s_kbd, SZ_RADIUS_SM, 0);
    lv_obj_set_style_border_width(s_kbd, 0, 0);
    lv_obj_set_style_shadow_width(s_kbd, 0, 0);

    s_connect_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_connect_btn, CONTENT_W - SZ_GAP * 2, 52);
    lv_obj_set_style_bg_color(s_connect_btn, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_connect_btn, COL_HI_DK, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(s_connect_btn, COL_BASE3, LV_STATE_DISABLED);
    lv_obj_set_style_radius(s_connect_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(s_connect_btn, 0, 0);
    lv_obj_set_style_border_width(s_connect_btn, 0, 0);
    lv_obj_add_event_cb(s_connect_btn, connect_btn_cb, LV_EVENT_CLICKED, NULL);

    s_connect_lbl = lv_label_create(s_connect_btn);
    lv_label_set_text(s_connect_lbl, "Connect");
    lv_obj_set_style_text_font(s_connect_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(s_connect_lbl, COL_BASE, 0);
    lv_obj_center(s_connect_lbl);
}

void ui_wifi_password_activate(const char *ssid)
{
    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    lv_label_set_text(s_title_lbl, ssid);

    char btn_txt[56];
    snprintf(btn_txt, sizeof(btn_txt), "Connect to %.42s", ssid);
    lv_label_set_text(s_connect_lbl, btn_txt);

    lv_textarea_set_text(s_pwd_ta, "");
    lv_obj_clear_state(s_connect_btn, LV_STATE_DISABLED);

    // Reset to password-hidden state
    s_pwd_visible = false;
    lv_textarea_set_password_mode(s_pwd_ta, true);
    lv_obj_t *show_lbl = lv_obj_get_child(s_show_btn, 0);
    if (show_lbl) lv_label_set_text(show_lbl, LV_SYMBOL_EYE_OPEN);
}
