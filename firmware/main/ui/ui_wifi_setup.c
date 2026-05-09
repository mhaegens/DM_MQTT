// ui_wifi_setup.c — WiFi setup overlay: Scan & Join mode + Setup via Phone mode.
// UI only — no WiFi backend calls yet. Buttons stub-log to console.
#include "ui_wifi_setup.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "config.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "wifi_setup";

static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_scan_panel = NULL;
static lv_obj_t *s_ap_panel   = NULL;
static lv_obj_t *s_mode_btns[2] = {NULL, NULL};
static lv_obj_t *s_scan_status_lbl = NULL;
static lv_obj_t *s_ap_status_lbl   = NULL;

// ─── Mode toggle ─────────────────────────────────────────────────────────────
static void set_mode(int mode)
{
    for (int i = 0; i < 2; i++) {
        if (!s_mode_btns[i]) continue;
        bool active = (i == mode);
        lv_obj_set_style_bg_color(s_mode_btns[i],
                                  active ? COL_HI : COL_BASE2, LV_PART_MAIN);
        lv_obj_t *lbl = lv_obj_get_child(s_mode_btns[i], 0);
        if (lbl)
            lv_obj_set_style_text_color(lbl,
                                        active ? COL_BASE : COL_TX2, LV_PART_MAIN);
    }
    if (s_scan_panel)
        mode == 0 ? lv_obj_clear_flag(s_scan_panel, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_add_flag(s_scan_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_ap_panel)
        mode == 1 ? lv_obj_clear_flag(s_ap_panel, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_add_flag(s_ap_panel, LV_OBJ_FLAG_HIDDEN);
}

static void scan_mode_cb(lv_event_t *e) { (void)e; set_mode(0); }
static void ap_mode_cb(lv_event_t *e)   { (void)e; set_mode(1); }

// ─── Stub action callbacks ────────────────────────────────────────────────────
static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "scan stub — backend not wired yet");
    if (s_scan_status_lbl)
        lv_label_set_text(s_scan_status_lbl, "Scanning… (backend not connected)");
}

static void hotspot_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "softAP stub — backend not wired yet");
    if (s_ap_status_lbl)
        lv_label_set_text(s_ap_status_lbl, "Hotspot inactive (backend not connected)");
}

// ─── Helper: standard outline action button ───────────────────────────────────
static lv_obj_t *make_action_btn(lv_obj_t *parent, const char *label_text,
                                  lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, CONTENT_W - SZ_GAP * 2, 52);
    lv_obj_set_style_bg_color(btn, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(lbl, COL_HI, 0);
    lv_obj_center(lbl);
    return btn;
}

// ─── Helper: key/value info row ───────────────────────────────────────────────
static void make_info_row(lv_obj_t *parent, const char *key, const char *val)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, LV_PART_MAIN);

    lv_obj_t *key_lbl = lv_label_create(row);
    lv_label_set_text(key_lbl, key);
    lv_obj_set_style_text_font(key_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(key_lbl, COL_TX3, 0);
    lv_obj_set_width(key_lbl, 60);

    lv_obj_t *val_lbl = lv_label_create(row);
    lv_label_set_text(val_lbl, val);
    lv_obj_set_style_text_font(val_lbl, FONT_MONO_XS, 0);
    lv_obj_set_style_text_color(val_lbl, COL_HI, 0);
}

// ─── Helper: instruction step label ──────────────────────────────────────────
static void make_step(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(lbl, COL_TX, 0);
    lv_obj_set_width(lbl, CONTENT_W - SZ_GAP * 4);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
}

// ─── Create ───────────────────────────────────────────────────────────────────
void ui_wifi_setup_create(lv_obj_t *parent)
{
    // ── Screen container ─────────────────────────────────────────────────────
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
    g_screens[VIEW_WIFI_SETUP] = s_screen;

    // ── Mode toggle bar ───────────────────────────────────────────────────────
    lv_obj_t *toggle = lv_obj_create(s_screen);
    lv_obj_set_size(toggle, CONTENT_W - SZ_GAP * 2, 48);
    lv_obj_set_style_bg_color(toggle, COL_BASE2, 0);
    lv_obj_set_style_border_color(toggle, COL_BD, 0);
    lv_obj_set_style_border_width(toggle, 1, 0);
    lv_obj_set_style_radius(toggle, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_all(toggle, 0, 0);
    lv_obj_set_style_shadow_width(toggle, 0, 0);
    lv_obj_clear_flag(toggle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(toggle, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toggle, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const char *mode_labels[] = {"Scan & Join", "Setup via Phone"};
    lv_event_cb_t mode_cbs[]  = {scan_mode_cb, ap_mode_cb};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *mbtn = lv_btn_create(toggle);
        lv_obj_set_flex_grow(mbtn, 1);
        lv_obj_set_height(mbtn, 48);
        lv_obj_set_style_radius(mbtn, SZ_RADIUS_SM, 0);
        lv_obj_set_style_border_width(mbtn, 0, 0);
        lv_obj_set_style_shadow_width(mbtn, 0, 0);
        lv_obj_add_event_cb(mbtn, mode_cbs[i], LV_EVENT_CLICKED, NULL);
        lv_obj_t *mlbl = lv_label_create(mbtn);
        lv_label_set_text(mlbl, mode_labels[i]);
        lv_obj_set_style_text_font(mlbl, FONT_SANS_XXS, 0);
        lv_obj_center(mlbl);
        s_mode_btns[i] = mbtn;
    }

    // ── Scan panel ────────────────────────────────────────────────────────────
    s_scan_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_scan_panel, CONTENT_W - SZ_GAP * 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_scan_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_scan_panel, 0, 0);
    lv_obj_set_style_pad_all(s_scan_panel, 0, 0);
    lv_obj_set_style_pad_gap(s_scan_panel, SZ_GAP, 0);
    lv_obj_set_style_shadow_width(s_scan_panel, 0, 0);
    lv_obj_clear_flag(s_scan_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_scan_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_scan_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_action_btn(s_scan_panel, "Scan Networks", scan_btn_cb);

    // Scan list container
    lv_obj_t *scan_list = lv_obj_create(s_scan_panel);
    lv_obj_set_size(scan_list, CONTENT_W - SZ_GAP * 2, 300);
    lv_obj_set_style_bg_color(scan_list, COL_BASE, 0);
    lv_obj_set_style_border_color(scan_list, COL_BD, 0);
    lv_obj_set_style_border_width(scan_list, 1, 0);
    lv_obj_set_style_radius(scan_list, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(scan_list, SZ_CARD_PAD, 0);
    lv_obj_set_style_shadow_width(scan_list, 0, 0);
    lv_obj_add_flag(scan_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scan_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scan_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(scan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scan_list, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *placeholder = lv_label_create(scan_list);
    lv_label_set_text(placeholder, "Tap \"Scan Networks\" to\ndiscover nearby networks");
    lv_obj_set_style_text_font(placeholder, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(placeholder, COL_TX3, 0);
    lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);

    s_scan_status_lbl = lv_label_create(s_scan_panel);
    lv_label_set_text(s_scan_status_lbl, "");
    lv_obj_set_style_text_font(s_scan_status_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(s_scan_status_lbl, COL_TX3, 0);

    // ── AP panel ──────────────────────────────────────────────────────────────
    s_ap_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_ap_panel, CONTENT_W - SZ_GAP * 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_ap_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ap_panel, 0, 0);
    lv_obj_set_style_pad_all(s_ap_panel, 0, 0);
    lv_obj_set_style_pad_gap(s_ap_panel, SZ_GAP, 0);
    lv_obj_set_style_shadow_width(s_ap_panel, 0, 0);
    lv_obj_clear_flag(s_ap_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_ap_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ap_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_ap_panel, LV_OBJ_FLAG_HIDDEN);

    make_action_btn(s_ap_panel, "Start Hotspot", hotspot_btn_cb);

    // ── QR code ───────────────────────────────────────────────────────────────
    // WiFi QR format: WIFI:T:<auth>;S:<ssid>;; (no password = open AP)
    char wifi_qr[64];
    snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:nopass;S:%s;;", PROV_AP_SSID);

    lv_obj_t *qr_card = lv_obj_create(s_ap_panel);
    lv_obj_set_size(qr_card, CONTENT_W - SZ_GAP * 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(qr_card, COL_BASE, 0);
    lv_obj_set_style_border_color(qr_card, COL_BD, 0);
    lv_obj_set_style_border_width(qr_card, 1, 0);
    lv_obj_set_style_radius(qr_card, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(qr_card, SZ_CARD_PAD, 0);
    lv_obj_set_style_pad_gap(qr_card, 8, 0);
    lv_obj_set_style_shadow_width(qr_card, 0, 0);
    lv_obj_clear_flag(qr_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(qr_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(qr_card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *qr = lv_qrcode_create(qr_card);
    lv_qrcode_set_size(qr, 180);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
    lv_qrcode_set_quiet_zone(qr, true);
    lv_qrcode_set_data(qr, wifi_qr);

    lv_obj_t *qr_caption = lv_label_create(qr_card);
    lv_label_set_text_fmt(qr_caption, "Scan to join  %s", PROV_AP_SSID);
    lv_obj_set_style_text_font(qr_caption, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(qr_caption, COL_TX2, 0);
    lv_obj_set_style_text_align(qr_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *info_card = lv_obj_create(s_ap_panel);
    lv_obj_set_size(info_card, CONTENT_W - SZ_GAP * 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(info_card, COL_BASE, 0);
    lv_obj_set_style_border_color(info_card, COL_BD, 0);
    lv_obj_set_style_border_width(info_card, 1, 0);
    lv_obj_set_style_radius(info_card, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(info_card, SZ_CARD_PAD + 4, 0);
    lv_obj_set_style_pad_gap(info_card, 10, 0);
    lv_obj_set_style_shadow_width(info_card, 0, 0);
    lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(info_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    make_info_row(info_card, "SSID:", PROV_AP_SSID);
    make_info_row(info_card, "URL:", PROV_PORTAL_URL);

    // Divider line
    lv_obj_t *divider = lv_obj_create(info_card);
    lv_obj_set_size(divider, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(divider, COL_BD, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    // Instructions
    make_step(info_card, "1.  Connect your phone to the\n    \"" PROV_AP_SSID "\" WiFi network");
    make_step(info_card, "2.  Open a browser and go to:\n    " PROV_PORTAL_URL);
    make_step(info_card, "3.  Enter your WiFi network name\n    and password, then tap Submit");

    s_ap_status_lbl = lv_label_create(s_ap_panel);
    lv_label_set_text(s_ap_status_lbl, "Hotspot inactive");
    lv_obj_set_style_text_font(s_ap_status_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(s_ap_status_lbl, COL_TX3, 0);

    // Activate scan mode by default
    set_mode(0);
}
