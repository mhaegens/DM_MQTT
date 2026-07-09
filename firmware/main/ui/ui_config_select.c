// ui_config_select.c — On-device configuration template switcher.
// Mirrors the captive portal's Config tab: lists the *.json templates found
// on SPIFFS + SD card, marks the one whose content matches the active config,
// and lets the operator activate another one (persist to both stores, reboot).
#include "ui_config_select.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data.h"
#include "../config_store.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cfg_sel";

static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_hint_lbl   = NULL;
static lv_obj_t *s_list       = NULL;
static lv_obj_t *s_status_lbl = NULL;
static lv_obj_t *s_apply_btn  = NULL;
static lv_obj_t *s_apply_lbl  = NULL;

static ConfigTemplate s_templates[CFG_TPL_MAX];
static int            s_tpl_count = 0;
static lv_obj_t      *s_rows[CFG_TPL_MAX] = {NULL};
static int            s_sel = -1;

// ─── Selection / apply ────────────────────────────────────────────────────────

static void set_status(const char *text, lv_color_t color)
{
    if (!s_status_lbl) return;
    lv_label_set_text(s_status_lbl, text);
    lv_obj_set_style_text_color(s_status_lbl, color, 0);
}

static void style_row_selected(lv_obj_t *row, bool selected)
{
    lv_obj_set_style_border_color(row, selected ? COL_HI : COL_BD, 0);
    lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
    lv_obj_set_style_bg_color(row, selected ? COL_HI_LT : COL_BASE, 0);
}

static void row_tap_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_tpl_count) return;

    for (int i = 0; i < s_tpl_count; i++)
        if (s_rows[i]) style_row_selected(s_rows[i], i == idx);
    s_sel = idx;

    ConfigTemplate *t = &s_templates[idx];
    if (t->active) {
        set_status("This template is already active.", COL_TX3);
        if (s_apply_btn) lv_obj_add_flag(s_apply_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    set_status("", COL_TX3);
    if (s_apply_btn && s_apply_lbl) {
        char buf[80];
        snprintf(buf, sizeof(buf), "Apply \"%s\" & Reboot",
                 t->plant[0] ? t->plant : t->name);
        lv_label_set_text(s_apply_lbl, buf);
        lv_obj_clear_flag(s_apply_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void reboot_timer_cb(lv_timer_t *t)
{
    (void)t;
    esp_restart();
}

static void apply_cb(lv_event_t *e)
{
    (void)e;
    if (s_sel < 0 || s_sel >= s_tpl_count) return;
    ConfigTemplate *t = &s_templates[s_sel];

    const char *err = config_store_activate(t->name, t->from_sd);
    if (err) {
        ESP_LOGW(TAG, "activate %s failed: %s", t->name, err);
        set_status(err, COL_ERR);
        return;
    }

    ESP_LOGI(TAG, "template %s activated — rebooting", t->name);
    set_status("Saved - rebooting...", COL_OK);
    lv_obj_clear_flag(s_apply_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_apply_btn, COL_BASE3, 0);
    lv_obj_set_style_text_color(s_apply_lbl, COL_TX3, 0);

    // Give the frame a moment to reach the panel before restarting
    lv_timer_t *timer = lv_timer_create(reboot_timer_cb, 900, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

// ─── Row builder ──────────────────────────────────────────────────────────────

static void build_row(int idx)
{
    ConfigTemplate *t = &s_templates[idx];

    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_set_size(row, LV_PCT(100), 64);
    lv_obj_set_style_bg_color(row, COL_BASE, 0);
    lv_obj_set_style_bg_color(row, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, COL_BD, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, SZ_CARD_PAD, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_add_event_cb(row, row_tap_cb, LV_EVENT_SHORT_CLICKED,
                        (void *)(intptr_t)idx);
    s_rows[idx] = row;

    // File icon
    lv_obj_t *icon = lv_label_create(row);
    lv_label_set_text(icon, LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(icon, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(icon, t->active ? COL_OK : COL_TX2, 0);

    // Title + subtitle column
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col, 2, 0);

    lv_obj_t *title = lv_label_create(col);
    lv_label_set_text(title, t->plant[0] ? t->plant : t->name);
    lv_obj_set_style_text_font(title, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(title, COL_TX, 0);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    char sub[96];
    snprintf(sub, sizeof(sub), "%s  ·  %d machines  ·  %d tanks%s",
             t->name, t->machines, t->tanks, t->from_sd ? "  ·  SD" : "");
    lv_obj_t *subtitle = lv_label_create(col);
    lv_label_set_text(subtitle, sub);
    lv_obj_set_style_text_font(subtitle, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(subtitle, COL_TX3, 0);
    lv_obj_set_width(subtitle, LV_PCT(100));
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);

    // "ACTIVE" pill on the right
    if (t->active) {
        lv_obj_t *pill = lv_obj_create(row);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(pill, COL_OK_LT, 0);
        lv_obj_set_style_border_color(pill, lv_color_hex(COL_OK_BD), 0);
        lv_obj_set_style_border_width(pill, 1, 0);
        lv_obj_set_style_radius(pill, SZ_RADIUS_SM, 0);
        lv_obj_set_style_pad_ver(pill, 4, 0);
        lv_obj_set_style_pad_hor(pill, 10, 0);
        lv_obj_set_style_shadow_width(pill, 0, 0);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *pl = lv_label_create(pill);
        lv_label_set_text(pl, "ACTIVE");
        lv_obj_set_style_text_font(pl, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(pl, COL_OK, 0);
        lv_obj_center(pl);
    }
}

// ─── Create ───────────────────────────────────────────────────────────────────

void ui_config_select_create(lv_obj_t *parent)
{
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, CONTENT_W, DETAIL_H);
    lv_obj_set_pos(s_screen, 0, SZ_CRUMB);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, SZ_GAP, 0);
    lv_obj_set_style_pad_gap(s_screen, SZ_GAP, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_screens[VIEW_CONFIG_SEL] = s_screen;

    // Hint card
    lv_obj_t *hint = lv_obj_create(s_screen);
    lv_obj_set_size(hint, CONTENT_W - SZ_GAP * 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(hint, COL_BASE2, 0);
    lv_obj_set_style_border_color(hint, COL_BD, 0);
    lv_obj_set_style_border_width(hint, 1, 0);
    lv_obj_set_style_radius(hint, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_all(hint, SZ_CARD_PAD, 0);
    lv_obj_set_style_shadow_width(hint, 0, 0);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_SCROLLABLE);

    s_hint_lbl = lv_label_create(hint);
    lv_label_set_text(s_hint_lbl, "");
    lv_obj_set_style_text_font(s_hint_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(s_hint_lbl, COL_TX2, 0);
    lv_obj_set_width(s_hint_lbl, LV_PCT(100));
    lv_label_set_long_mode(s_hint_lbl, LV_LABEL_LONG_WRAP);

    // Template list — fills the space between hint and apply button
    s_list = lv_obj_create(s_screen);
    lv_obj_set_width(s_list, CONTENT_W - SZ_GAP * 2);
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_color(s_list, COL_BASE, 0);
    lv_obj_set_style_border_color(s_list, COL_BD, 0);
    lv_obj_set_style_border_width(s_list, 1, 0);
    lv_obj_set_style_radius(s_list, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(s_list, SZ_CARD_PAD, 0);
    lv_obj_set_style_pad_gap(s_list, 8, 0);
    lv_obj_set_style_shadow_width(s_list, 0, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Status line
    s_status_lbl = lv_label_create(s_screen);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_set_style_text_font(s_status_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(s_status_lbl, COL_TX3, 0);
    lv_obj_set_width(s_status_lbl, CONTENT_W - SZ_GAP * 2);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);

    // Apply button (hidden until a non-active template is selected)
    s_apply_btn = lv_obj_create(s_screen);
    lv_obj_set_size(s_apply_btn, CONTENT_W - SZ_GAP * 2, 56);
    lv_obj_set_style_bg_color(s_apply_btn, COL_HI, 0);
    lv_obj_set_style_bg_color(s_apply_btn, COL_HI_DK, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_apply_btn, 0, 0);
    lv_obj_set_style_radius(s_apply_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_all(s_apply_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_apply_btn, 0, 0);
    lv_obj_clear_flag(s_apply_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_apply_btn, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_apply_btn, apply_cb, LV_EVENT_SHORT_CLICKED, NULL);

    s_apply_lbl = lv_label_create(s_apply_btn);
    lv_label_set_text(s_apply_lbl, "Apply & Reboot");
    lv_obj_set_style_text_font(s_apply_lbl, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(s_apply_lbl, COL_BASE, 0);
    lv_obj_center(s_apply_lbl);

    ESP_LOGI(TAG, "config select screen created");
}

// ─── Activate: rescan templates and rebuild the list ─────────────────────────

void ui_config_select_activate(void)
{
    if (!s_screen || !s_list) return;

    lv_obj_clean(s_list);
    memset(s_rows, 0, sizeof(s_rows));
    s_sel = -1;
    if (s_apply_btn) {
        lv_obj_add_flag(s_apply_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_apply_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(s_apply_btn, COL_HI, 0);
        lv_obj_set_style_text_color(s_apply_lbl, COL_BASE, 0);
    }
    set_status("", COL_TX3);

    if (s_hint_lbl)
        lv_label_set_text_fmt(s_hint_lbl,
            "Active config: %s. Profiles live on the SD card (flash when "
            "no card is inserted) - applying one reboots the device.",
            g_plant_name[0] ? g_plant_name : "unknown");

    // Blocking file IO (SPIFFS + optional SD mount) — acceptable one-shot cost
    // on screen entry; the list is small and read once per visit.
    s_tpl_count = config_store_list(s_templates, CFG_TPL_MAX);

    if (s_tpl_count == 0) {
        lv_obj_t *placeholder = lv_label_create(s_list);
        lv_label_set_text(placeholder,
            "No config templates found.\n"
            "Add *.json files via the captive portal\n"
            "or on an SD card.");
        lv_obj_set_style_text_font(placeholder, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(placeholder, COL_TX3, 0);
        lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (int i = 0; i < s_tpl_count; i++)
        build_row(i);
}
