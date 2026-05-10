// ui_nfc.c — NFC screen: Physical mode (ring + scan) + Virtual mode (badge carousel).
#include "ui_nfc.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_home.h"
#include "data.h"
#include "../mqtt_task.h"
#include <stdio.h>
#include <string.h>

#define CARD_W  440
#define CARD_H  260
#define CAR_H   (CARD_H + 50)   // carousel container height (overflow for scaled cards)
#define RING_OUTER 150
#define RING_INNER  80

static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_phys_panel = NULL;
static lv_obj_t *s_virt_panel = NULL;
static lv_obj_t *s_mode_btns[2] = {NULL};

// Physical mode
static lv_obj_t *s_result_card = NULL;
static lv_obj_t *s_result_name = NULL;
static lv_obj_t *s_result_role = NULL;
static lv_obj_t *s_result_uid  = NULL;

// Virtual mode
static lv_obj_t *s_badge_cards[BADGE_COUNT] = {NULL};
static lv_obj_t *s_dots[BADGE_COUNT]         = {NULL};
static lv_obj_t *s_counter_lbl               = NULL;
static lv_obj_t *s_select_btn                = NULL;
static lv_obj_t *s_select_lbl                = NULL;
static int       s_carousel_idx              = 0;
static int16_t   s_swipe_start_x             = 0;

// ─── Mode toggle ───────────────────────────────────────────────────────────────
static void set_mode(int mode) {
    // 0 = physical, 1 = virtual
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
    if (s_phys_panel)
        mode == 0 ? lv_obj_clear_flag(s_phys_panel, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_add_flag(s_phys_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_virt_panel)
        mode == 1 ? lv_obj_clear_flag(s_virt_panel, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_add_flag(s_virt_panel, LV_OBJ_FLAG_HIDDEN);
}

static void phys_btn_cb(lv_event_t *e) { (void)e; set_mode(0); }
static void virt_btn_cb(lv_event_t *e) { (void)e; set_mode(1); }

// ─── Carousel update ──────────────────────────────────────────────────────────
static void update_carousel(void) {
    for (int i = 0; i < BADGE_COUNT; i++) {
        if (!s_badge_cards[i]) continue;
        int rel = i - s_carousel_idx;

        // Hide cards more than 1 away (only 3 cards visible at a time)
        if (rel < -1 || rel > 1) {
            lv_obj_add_flag(s_badge_cards[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_badge_cards[i], LV_OBJ_FLAG_HIDDEN);

            int scale, opa, x, y;
            switch (rel) {
                case  0: scale=256; opa=255; x=130;  y=25; break;
                case -1: scale=230; opa=220; x=-320; y=30; break;
                case  1: scale=230; opa=220; x=598;  y=30; break;
                default: scale=256; opa=255; x=180;  y=20; break;
            }
            lv_obj_set_pos(s_badge_cards[i], x, y);
            lv_obj_set_style_transform_scale(s_badge_cards[i], (int16_t)scale, 0);
            lv_obj_set_style_opa(s_badge_cards[i], (lv_opa_t)opa, 0);
        }

        // Dot indicators
        if (s_dots[i]) {
            bool active = (i == s_carousel_idx);
            lv_obj_set_width(s_dots[i], active ? 22 : 7);
            lv_obj_set_style_bg_color(s_dots[i],
                                      active ? COL_HI : COL_TX3, 0);
            lv_obj_set_style_radius(s_dots[i],
                                    active ? 4 : LV_RADIUS_CIRCLE, 0);
        }
    }

    if (s_counter_lbl) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d of %d", s_carousel_idx + 1, BADGE_COUNT);
        lv_label_set_text(s_counter_lbl, buf);
    }
}

static void prev_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_carousel_idx > 0) { s_carousel_idx--; update_carousel(); }
}
static void next_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_carousel_idx < BADGE_COUNT - 1) { s_carousel_idx++; update_carousel(); }
}

static void carousel_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    if (code == LV_EVENT_PRESSED) {
        s_swipe_start_x = (int16_t)pt.x;
    } else if (code == LV_EVENT_RELEASED) {
        int dx = pt.x - s_swipe_start_x;
        if (dx < -60 && s_carousel_idx < BADGE_COUNT - 1) {
            s_carousel_idx++;
            update_carousel();
        } else if (dx > 60 && s_carousel_idx > 0) {
            s_carousel_idx--;
            update_carousel();
        }
    }
}

// ─── Select operator ──────────────────────────────────────────────────────────
static void restore_select_cb(lv_timer_t *t) {
    lv_timer_delete(t);
    if (s_select_btn) lv_obj_set_style_bg_color(s_select_btn, COL_HI, 0);
    if (s_select_lbl) lv_label_set_text(s_select_lbl, "Select Operator");
}

static void select_btn_cb(lv_event_t *e) {
    (void)e;
    g_active_badge = s_carousel_idx;
    if (s_select_btn) lv_obj_set_style_bg_color(s_select_btn, COL_OK, 0);
    if (s_select_lbl) lv_label_set_text(s_select_lbl, "Operator Selected");
    lv_timer_create(restore_select_cb, 1500, NULL);
    Badge *b = &g_badges[g_active_badge];
    mqtt_pub_operator(b->uid, b->name, b->role, b->plant);
    ui_home_refresh();
}

// ─── Physical mode: Simulate Tap ─────────────────────────────────────────────
static void sim_tap_cb(lv_event_t *e) {
    (void)e;
    if (g_badge_count <= 0) return;
    int next = (g_active_badge + 1) % g_badge_count;
    g_active_badge = next;
    Badge *b = &g_badges[g_active_badge];

    if (s_result_name) lv_label_set_text(s_result_name, b->name);
    if (s_result_role) lv_label_set_text(s_result_role, b->role);
    if (s_result_uid)  lv_label_set_text(s_result_uid,  b->uid);

    if (s_result_card) {
        lv_obj_clear_flag(s_result_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_fade_in(s_result_card, 300, 0);
    }
    mqtt_pub_operator(b->uid, b->name, b->role, b->plant);
    ui_home_refresh();
}

// ─── Build badge card for carousel ───────────────────────────────────────────
static lv_obj_t *build_badge_card(lv_obj_t *parent, int idx)
{
    Badge *b = &g_badges[idx];

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_add_flag(card, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_bg_color(card, lv_color_hex(b->avatar_color), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_gap(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_outline_width(card, 0, 0);
    lv_obj_set_style_transform_pivot_x(card, CARD_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(card, CARD_H / 2, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Top row: plant | id
    lv_obj_t *top = lv_obj_create(card);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *plant_lbl = lv_label_create(top);
    lv_label_set_text(plant_lbl, b->plant);
    lv_obj_set_style_text_font(plant_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(plant_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(plant_lbl, LV_OPA_70, 0);

    lv_obj_t *dept_lbl_top = lv_label_create(top);
    lv_label_set_text(dept_lbl_top, b->dept);
    lv_obj_set_style_text_font(dept_lbl_top, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(dept_lbl_top, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(dept_lbl_top, LV_OPA_70, 0);

    // Middle: avatar + name + role
    lv_obj_t *mid = lv_obj_create(card);
    lv_obj_remove_style_all(mid);
    lv_obj_set_size(mid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(mid, 4, 0);

    lv_obj_t *avatar = lv_obj_create(mid);
    lv_obj_set_size(avatar, 44, 44);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_20, 0);
    lv_obj_set_style_border_color(avatar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(avatar, 2, 0);
    lv_obj_set_style_border_opa(avatar, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(avatar, 0, 0);
    lv_obj_set_style_shadow_width(avatar, 0, 0);
    lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

    // Initials
    char init[3] = {0};
    init[0] = b->name[0];
    for (int i = 1; b->name[i]; i++) {
        if (b->name[i-1] == ' ' && b->name[i] && b->name[i] != ' ') {
            init[1] = b->name[i];
            break;
        }
    }
    lv_obj_t *init_lbl = lv_label_create(avatar);
    lv_label_set_text(init_lbl, init);
    lv_obj_set_style_text_font(init_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(init_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(init_lbl);

    lv_obj_t *name_lbl = lv_label_create(mid);
    lv_label_set_text(name_lbl, b->name);
    lv_obj_set_style_text_font(name_lbl, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *role_pill = lv_obj_create(mid);
    lv_obj_set_size(role_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(role_pill, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(role_pill, LV_OPA_20, 0);
    lv_obj_set_style_border_width(role_pill, 0, 0);
    lv_obj_set_style_radius(role_pill, SZ_RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(role_pill, 10, 0);
    lv_obj_set_style_pad_ver(role_pill, 4, 0);
    lv_obj_set_style_shadow_width(role_pill, 0, 0);
    lv_obj_clear_flag(role_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *role_lbl = lv_label_create(role_pill);
    lv_label_set_text(role_lbl, b->role);
    lv_obj_set_style_text_font(role_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(role_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(role_lbl);

    // Bottom: cert/skill pills (fallback to uid if no certs)
    lv_obj_t *bot = lv_obj_create(card);
    lv_obj_remove_style_all(bot);
    lv_obj_set_size(bot, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(bot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bot, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(bot, 6, 0);

    if (b->cert_count > 0) {
        for (int ci = 0; ci < b->cert_count && ci < MAX_CERTS; ci++) {
            lv_obj_t *pill = lv_obj_create(bot);
            lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(pill, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);
            lv_obj_set_style_border_width(pill, 0, 0);
            lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_pad_hor(pill, 8, 0);
            lv_obj_set_style_pad_ver(pill, 3, 0);
            lv_obj_set_style_shadow_width(pill, 0, 0);
            lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *cert_lbl = lv_label_create(pill);
            lv_label_set_text(cert_lbl, b->certs[ci]);
            lv_obj_set_style_text_font(cert_lbl, FONT_MONO_XXS, 0);
            lv_obj_set_style_text_color(cert_lbl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_opa(cert_lbl, LV_OPA_90, 0);
        }
    } else {
        lv_obj_t *uid_lbl = lv_label_create(bot);
        lv_label_set_text(uid_lbl, b->uid);
        lv_obj_set_style_text_font(uid_lbl, FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(uid_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_opa(uid_lbl, LV_OPA_50, 0);
    }

    return card;
}

// ─── Make ghost circle button ─────────────────────────────────────────────────
static lv_obj_t *make_circle_btn(lv_obj_t *parent, const char *symbol,
                                  lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 52, 52);
    lv_obj_set_style_bg_color(btn, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, COL_BD_STR, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(lbl, COL_TX, 0);
    lv_obj_center(lbl);
    return btn;
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

    const char *mode_labels[] = {"Physical", "Virtual"};
    lv_event_cb_t mode_cbs[]  = {phys_btn_cb, virt_btn_cb};
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

    // ── Physical mode panel ───────────────────────────────────────────────────
    s_phys_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_phys_panel, CONTENT_W - SZ_GAP * 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_phys_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_phys_panel, 0, 0);
    lv_obj_set_style_pad_all(s_phys_panel, 0, 0);
    lv_obj_set_style_pad_gap(s_phys_panel, 12, 0);
    lv_obj_set_style_shadow_width(s_phys_panel, 0, 0);
    lv_obj_clear_flag(s_phys_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_phys_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_phys_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // NFC ring container
    lv_obj_t *ring_cont = lv_obj_create(s_phys_panel);
    lv_obj_remove_style_all(ring_cont);
    lv_obj_set_size(ring_cont, RING_OUTER + 10, RING_OUTER + 10);
    lv_obj_clear_flag(ring_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Outer ring
    lv_obj_t *outer_ring = lv_obj_create(ring_cont);
    lv_obj_set_size(outer_ring, RING_OUTER, RING_OUTER);
    lv_obj_center(outer_ring);
    lv_obj_set_style_bg_opa(outer_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(outer_ring, COL_HI, 0);
    lv_obj_set_style_border_width(outer_ring, 3, 0);
    lv_obj_set_style_radius(outer_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(outer_ring, 0, 0);
    lv_obj_set_style_shadow_width(outer_ring, 0, 0);
    lv_obj_clear_flag(outer_ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Spinner
    lv_obj_t *spin = lv_spinner_create(ring_cont);
    lv_obj_set_size(spin, 120, 120);
    lv_obj_center(spin);
    lv_obj_set_style_arc_color(spin, COL_HI, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spin, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(spin, LV_OPA_TRANSP, LV_PART_MAIN);

    // Inner ring
    lv_obj_t *inner_ring = lv_obj_create(ring_cont);
    lv_obj_set_size(inner_ring, RING_INNER, RING_INNER);
    lv_obj_center(inner_ring);
    lv_obj_set_style_bg_opa(inner_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(inner_ring, COL_HI, 0);
    lv_obj_set_style_border_width(inner_ring, 2, 0);
    lv_obj_set_style_radius(inner_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(inner_ring, 0, 0);
    lv_obj_set_style_shadow_width(inner_ring, 0, 0);
    lv_obj_clear_flag(inner_ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *nfc_lbl = lv_label_create(ring_cont);
    lv_label_set_text(nfc_lbl, "NFC");
    lv_obj_set_style_text_font(nfc_lbl, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(nfc_lbl, COL_HI, 0);
    lv_obj_center(nfc_lbl);

    // Instruction text
    lv_obj_t *inst1 = lv_label_create(s_phys_panel);
    lv_label_set_text(inst1, "Tap Badge to Identify Operator");
    lv_obj_set_style_text_font(inst1, FONT_SANS_XS, 0);
    lv_obj_set_style_text_color(inst1, COL_TX, 0);
    lv_obj_set_style_text_align(inst1, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *inst2 = lv_label_create(s_phys_panel);
    lv_label_set_text(inst2, "MIFARE Classic - Hold within 5 cm");
    lv_obj_set_style_text_font(inst2, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(inst2, COL_TX3, 0);
    lv_obj_set_style_text_align(inst2, LV_TEXT_ALIGN_CENTER, 0);

    // Simulate Tap button
    lv_obj_t *sim_btn = lv_btn_create(s_phys_panel);
    lv_obj_set_size(sim_btn, CONTENT_W - SZ_GAP * 2, 52);
    lv_obj_set_style_bg_color(sim_btn, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sim_btn, COL_HI_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(sim_btn, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_border_width(sim_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(sim_btn, SZ_RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(sim_btn, 0, 0);
    lv_obj_add_event_cb(sim_btn, sim_tap_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sim_lbl = lv_label_create(sim_btn);
    lv_label_set_text(sim_lbl, "Simulate Tap");
    lv_obj_set_style_text_font(sim_lbl, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(sim_lbl, COL_HI, 0);
    lv_obj_center(sim_lbl);

    // Scan result card (hidden by default)
    s_result_card = lv_obj_create(s_phys_panel);
    lv_obj_set_size(s_result_card, CONTENT_W - SZ_GAP * 2, 120);
    lv_obj_set_style_bg_color(s_result_card, COL_BASE, 0);
    lv_obj_set_style_border_color(s_result_card, COL_BD, 0);
    lv_obj_set_style_border_width(s_result_card, 1, 0);
    lv_obj_set_style_radius(s_result_card, SZ_RADIUS, 0);
    lv_obj_set_style_pad_all(s_result_card, 14, 0);
    lv_obj_set_style_pad_gap(s_result_card, 4, 0);
    lv_obj_set_style_shadow_width(s_result_card, 12, 0);
    lv_obj_set_style_shadow_opa(s_result_card, LV_OPA_20, 0);
    lv_obj_set_style_outline_width(s_result_card, 0, 0);
    lv_obj_clear_flag(s_result_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_result_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_result_card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_result_card, 4, 0);
    lv_obj_add_flag(s_result_card, LV_OBJ_FLAG_HIDDEN);

    s_result_name = lv_label_create(s_result_card);
    lv_label_set_text(s_result_name, "—");
    lv_obj_set_style_text_font(s_result_name, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(s_result_name, COL_TX, 0);

    s_result_role = lv_label_create(s_result_card);
    lv_label_set_text(s_result_role, "—");
    lv_obj_set_style_text_font(s_result_role, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(s_result_role, COL_TX2, 0);

    s_result_uid = lv_label_create(s_result_card);
    lv_label_set_text(s_result_uid, "—");
    lv_obj_set_style_text_font(s_result_uid, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(s_result_uid, COL_TX3, 0);

    // ── Virtual mode panel ────────────────────────────────────────────────────
    s_virt_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_virt_panel, CONTENT_W - SZ_GAP * 2, 506);
    lv_obj_set_style_bg_opa(s_virt_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_virt_panel, 0, 0);
    lv_obj_set_style_pad_all(s_virt_panel, 0, 0);
    lv_obj_set_style_pad_gap(s_virt_panel, 8, 0);
    lv_obj_set_style_shadow_width(s_virt_panel, 0, 0);
    lv_obj_clear_flag(s_virt_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_virt_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_virt_panel, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_virt_panel, LV_OBJ_FLAG_HIDDEN);

    // Carousel container (fixed size, no layout — manual card positioning)
    lv_obj_t *carousel = lv_obj_create(s_virt_panel);
    lv_obj_set_size(carousel, CONTENT_W - SZ_GAP * 2, CAR_H);
    lv_obj_set_style_bg_opa(carousel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(carousel, 0, 0);
    lv_obj_set_style_pad_all(carousel, 0, 0);
    lv_obj_set_style_shadow_width(carousel, 0, 0);
    lv_obj_set_style_clip_corner(carousel, false, 0);
    lv_obj_clear_flag(carousel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(carousel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(carousel, carousel_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(carousel, carousel_event_cb, LV_EVENT_RELEASED, NULL);

    // Build badge cards
    for (int i = 0; i < BADGE_COUNT; i++) {
        s_badge_cards[i] = build_badge_card(carousel, i);
    }

    // Prev / Next arrows (inside carousel, absolute positioned)
    lv_obj_t *prev = make_circle_btn(carousel, LV_SYMBOL_LEFT, prev_btn_cb);
    lv_obj_add_flag(prev, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(prev, 8, CAR_H / 2 - 26);

    lv_obj_t *next = make_circle_btn(carousel, LV_SYMBOL_RIGHT, next_btn_cb);
    lv_obj_add_flag(next, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(next, CONTENT_W - SZ_GAP * 2 - 8 - 52, CAR_H / 2 - 26);

    // Dots row
    lv_obj_t *dots_row = lv_obj_create(s_virt_panel);
    lv_obj_remove_style_all(dots_row);
    lv_obj_set_size(dots_row, LV_SIZE_CONTENT, 16);
    lv_obj_clear_flag(dots_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(dots_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(dots_row, 6, 0);

    for (int i = 0; i < BADGE_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(dots_row);
        lv_obj_set_size(dot, 7, 7);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, COL_TX3, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_style_shadow_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        s_dots[i] = dot;
    }

    // Counter label
    s_counter_lbl = lv_label_create(s_virt_panel);
    lv_label_set_text(s_counter_lbl, "1 of 5");
    lv_obj_set_style_text_font(s_counter_lbl, FONT_MONO_XXS, 0);
    lv_obj_set_style_text_color(s_counter_lbl, COL_TX3, 0);

    // Select Operator button
    s_select_btn = lv_btn_create(s_virt_panel);
    lv_obj_set_size(s_select_btn, CONTENT_W - SZ_GAP * 2, 60);
    lv_obj_set_style_bg_color(s_select_btn, COL_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_select_btn, COL_HI_DK, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_select_btn, 0, 0);
    lv_obj_set_style_radius(s_select_btn, SZ_RADIUS, 0);
    lv_obj_set_style_shadow_width(s_select_btn, 0, 0);
    lv_obj_add_event_cb(s_select_btn, select_btn_cb, LV_EVENT_CLICKED, NULL);

    s_select_lbl = lv_label_create(s_select_btn);
    lv_label_set_text(s_select_lbl, "Select Operator");
    lv_obj_set_style_text_font(s_select_lbl, FONT_SANS_SM, 0);
    lv_obj_set_style_text_color(s_select_lbl, COL_BASE, 0);
    lv_obj_center(s_select_lbl);

    // Initial state
    set_mode(0);
    update_carousel();
}

void ui_nfc_on_badge_scanned(int badge_idx) {
    if (badge_idx < 0 || badge_idx >= BADGE_COUNT) return;
    g_active_badge = badge_idx;
    Badge *b = &g_badges[badge_idx];
    if (s_result_name) lv_label_set_text(s_result_name, b->name);
    if (s_result_role) lv_label_set_text(s_result_role, b->role);
    if (s_result_uid)  lv_label_set_text(s_result_uid,  b->uid);
    if (s_result_card) {
        lv_obj_clear_flag(s_result_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_fade_in(s_result_card, 300, 0);
    }
    ui_home_refresh();
}
