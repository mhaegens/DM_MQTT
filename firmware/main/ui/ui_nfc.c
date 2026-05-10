// ui_nfc.c — People tab: scrollable operator list + Simulate Scan button.
#include "ui_nfc.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_home.h"
#include "data.h"
#include "../mqtt_task.h"
#include <stdio.h>
#include <string.h>

#define LIST_CARD_H  110
#define LIST_GAP      10
#define AVATAR_SZ     48

static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_list         = NULL;
static lv_obj_t *s_scan_btn     = NULL;
static lv_obj_t *s_scan_lbl     = NULL;
static lv_obj_t *s_counter_lbl  = NULL;
static lv_obj_t *s_card_rows[MAX_BADGES] = {NULL};

static int s_scans_today = 0;

// ─── Highlight the active card ────────────────────────────────────────────────
static void highlight_cards(void) {
    for (int i = 0; i < g_badge_count; i++) {
        if (!s_card_rows[i]) continue;
        bool active = (i == g_active_badge);
        lv_obj_set_style_bg_color(s_card_rows[i],
                                  active ? COL_HI_LT : COL_BASE, 0);
        lv_obj_set_style_border_color(s_card_rows[i],
                                      active ? COL_HI : COL_BD, 0);
        lv_obj_set_style_border_width(s_card_rows[i], active ? 2 : 1, 0);
    }
}

// ─── Restore button label after flash ─────────────────────────────────────────
static void restore_scan_cb(lv_timer_t *t) {
    lv_timer_delete(t);
    if (s_scan_btn) lv_obj_set_style_bg_color(s_scan_btn, COL_HI, 0);
    if (s_scan_lbl) lv_label_set_text(s_scan_lbl, "Simulate Scan");
}

// ─── Simulate Scan button: advance to next operator ───────────────────────────
static void scan_btn_cb(lv_event_t *e) {
    (void)e;
    if (g_badge_count <= 0) return;

    g_active_badge = (g_active_badge + 1) % g_badge_count;
    s_scans_today++;

    highlight_cards(  );

    if (s_scan_btn) lv_obj_set_style_bg_color(s_scan_btn, COL_OK, 0);
    if (s_scan_lbl) lv_label_set_text(s_scan_lbl, "Scanned!");
    lv_timer_create(restore_scan_cb, 1200, NULL);

    if (s_counter_lbl) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Scans today: %d", s_scans_today);
        lv_label_set_text(s_counter_lbl, buf);
    }

    Badge *b = &g_badges[g_active_badge];
    mqtt_pub_operator(b->uid, b->name, b->role, b->plant);
    ui_home_refresh();
}

// ─── Build one operator card ──────────────────────────────────────────────────
static lv_obj_t *build_operator_card(lv_obj_t *parent, int idx)
{
    Badge *b = &g_badges[idx];

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(card, LIST_CARD_H, 0);
    lv_obj_set_style_bg_color(card, COL_BASE, 0);
    lv_obj_set_style_border_color(card, COL_BD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_gap(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 14, 0);

    // Avatar circle (colored, role initial)
    lv_obj_t *avatar = lv_obj_create(card);
    lv_obj_set_size(avatar, AVATAR_SZ, AVATAR_SZ);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(b->avatar_color), 0);
    lv_obj_set_style_border_width(avatar, 0, 0);
    lv_obj_set_style_pad_all(avatar, 0, 0);
    lv_obj_set_style_shadow_width(avatar, 0, 0);
    lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

    // Role initial(s)
    char init[3] = {0};
    if (b->name[0]) init[0] = b->name[0];
    for (int i = 1; b->name[i]; i++) {
        if (b->name[i-1] == ' ' && b->name[i] != ' ') {
            init[1] = b->name[i];
            break;
        }
    }
    lv_obj_t *init_lbl = lv_label_create(avatar);
    lv_label_set_text(init_lbl, init);
    lv_obj_set_style_text_font(init_lbl, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(init_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(init_lbl);

    // Info column
    lv_obj_t *info = lv_obj_create(card);
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(info, 4, 0);

    // Name
    lv_obj_t *name_lbl = lv_label_create(info);
    lv_label_set_text(name_lbl, b->name);
    lv_obj_set_style_text_font(name_lbl, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(name_lbl, COL_TX, 0);

    // Role pill
    lv_obj_t *role_row = lv_obj_create(info);
    lv_obj_remove_style_all(role_row);
    lv_obj_set_size(role_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(role_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(role_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(role_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(role_row, 6, 0);

    lv_obj_t *role_pill = lv_obj_create(role_row);
    lv_obj_set_size(role_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(role_pill, COL_HI_LT, 0);
    lv_obj_set_style_border_color(role_pill, lv_color_hex(COL_HI_MID), 0);
    lv_obj_set_style_border_width(role_pill, 1, 0);
    lv_obj_set_style_radius(role_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(role_pill, 8, 0);
    lv_obj_set_style_pad_ver(role_pill, 3, 0);
    lv_obj_set_style_shadow_width(role_pill, 0, 0);
    lv_obj_clear_flag(role_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *role_lbl = lv_label_create(role_pill);
    lv_label_set_text(role_lbl, b->role);
    lv_obj_set_style_text_font(role_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(role_lbl, COL_HI, 0);
    lv_obj_center(role_lbl);

    // Dept + plant row
    char meta[80];
    snprintf(meta, sizeof(meta), "%s  \xc2\xb7  %s", b->dept, b->plant);
    lv_obj_t *meta_lbl = lv_label_create(info);
    lv_label_set_text(meta_lbl, meta);
    lv_obj_set_style_text_font(meta_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(meta_lbl, COL_TX3, 0);

    // Cert chips row
    if (b->cert_count > 0) {
        lv_obj_t *certs_row = lv_obj_create(info);
        lv_obj_remove_style_all(certs_row);
        lv_obj_set_size(certs_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(certs_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(certs_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(certs_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(certs_row, 5, 0);

        for (int ci = 0; ci < b->cert_count && ci < MAX_CERTS; ci++) {
            lv_obj_t *chip = lv_obj_create(certs_row);
            lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(chip, COL_BASE2, 0);
            lv_obj_set_style_border_color(chip, COL_BD, 0);
            lv_obj_set_style_border_width(chip, 1, 0);
            lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_pad_hor(chip, 7, 0);
            lv_obj_set_style_pad_ver(chip, 2, 0);
            lv_obj_set_style_shadow_width(chip, 0, 0);
            lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *cl = lv_label_create(chip);
            lv_label_set_text(cl, b->certs[ci]);
            lv_obj_set_style_text_font(cl, FONT_MONO_XXS, 0);
            lv_obj_set_style_text_color(cl, COL_TX2, 0);
        }
    }

    return card;
}

// ─── Create ───────────────────────────────────────────────────────────────────
void ui_nfc_create(lv_obj_t *parent)
{
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, CONTENT_W, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
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
    g_screens[VIEW_NFC] = s_screen;

    // ── Scrollable operator list ──────────────────────────────────────────────
    // Reserve bottom space for the fixed button row (button 60 + gap*2)
    int list_h = CONTENT_H - SZ_GAP * 2 - 60 - SZ_GAP - 28 - SZ_GAP;
    s_list = lv_obj_create(s_screen);
    lv_obj_set_size(s_list, CONTENT_W - SZ_GAP * 2, list_h);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_gap(s_list, LIST_GAP, 0);
    lv_obj_set_style_shadow_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < g_badge_count; i++) {
        s_card_rows[i] = build_operator_card(s_list, i);
    }

    // ── Scans-today counter label ─────────────────────────────────────────────
    s_counter_lbl = lv_label_create(s_screen);
    lv_label_set_text(s_counter_lbl, "Scans today: 0");
    lv_obj_set_style_text_font(s_counter_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(s_counter_lbl, COL_TX3, 0);

    // ── Simulate Scan button (full-width, always visible) ─────────────────────
    s_scan_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_scan_btn, CONTENT_W - SZ_GAP * 2, 60);
    lv_obj_set_style_bg_color(s_scan_btn, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_scan_btn, COL_HI_DK, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_scan_btn, 0, 0);
    lv_obj_set_style_radius(s_scan_btn, SZ_RADIUS, 0);
    lv_obj_set_style_shadow_width(s_scan_btn, 0, 0);
    lv_obj_add_event_cb(s_scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

    s_scan_lbl = lv_label_create(s_scan_btn);
    lv_label_set_text(s_scan_lbl, "Simulate Scan");
    lv_obj_set_style_text_font(s_scan_lbl, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(s_scan_lbl, COL_BASE, 0);
    lv_obj_center(s_scan_lbl);

    // Highlight initial active card
    highlight_cards();
}

void ui_nfc_on_badge_scanned(int badge_idx) {
    if (badge_idx < 0 || badge_idx >= g_badge_count) return;
    g_active_badge = badge_idx;
    highlight_cards();
    ui_home_refresh();
}
