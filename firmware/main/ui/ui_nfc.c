// ui_nfc.c — NFC screen: ring animation + operator carousel.
// Full implementation in Phase 7; compiles as stub for earlier phases.
#include "ui_nfc.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data_model.h"
#include "mqtt_task.h"

static lv_obj_t *s_screen = NULL;

void ui_nfc_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    g_screens[VIEW_NFC] = s_screen;

    // TODO Phase 7: animated NFC ring, mode toggle (physical/virtual),
    // virtual badge carousel with swipe gestures, Activate Badge button.
    lv_obj_t *ph = lv_label_create(s_screen);
    lv_label_set_text(ph, "NFC — Phase 7");
    lv_obj_set_style_text_font(ph, FONT_SANS_MD, 0);
    lv_obj_set_style_text_color(ph, COL_TX2, 0);
    lv_obj_center(ph);
}

void ui_nfc_on_badge_scanned(int operator_idx) {
    if (operator_idx < 0 || operator_idx >= OP_COUNT) return;
    g_active_operator = operator_idx;
    Operator *op = &g_operators[operator_idx];
    mqtt_pub_operator(op->uid, op->name, op->role, op->plant);
}
