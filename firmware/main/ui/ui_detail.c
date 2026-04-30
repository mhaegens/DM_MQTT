// ui_detail.c — Metric detail view: breadcrumb + visualization + step/slider input.
// Full implementation in Phase 6; compiles as stub for earlier phases.
#include "ui_detail.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data_model.h"
#include "mqtt_task.h"

static lv_obj_t *s_screen = NULL;
static float     s_step   = 1.0f;   // current metric step size

void ui_detail_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    g_screens[VIEW_DETAIL] = s_screen;

    // TODO Phase 6: breadcrumb bar, header card (metric/entity/topic/value),
    // visualization card (sparkline/arc/thermometer/pH/tank/bullet),
    // set-value section (step input or slider), random generator section.
    lv_obj_t *ph = lv_label_create(s_screen);
    lv_label_set_text(ph, "Detail view — Phase 6");
    lv_obj_set_style_text_font(ph, FONT_SANS_MD, 0);
    lv_obj_set_style_text_color(ph, COL_TX2, 0);
    lv_obj_center(ph);
}

void ui_detail_open_tank_level(int tank_idx)        { ui_push_detail("Tanks", "Level"); (void)tank_idx; }
void ui_detail_open_tank_metric(int tank, int midx)  { ui_push_detail("Tanks", "Metric"); (void)tank; (void)midx; }
void ui_detail_open_machine_metric(int mach, int midx){ ui_push_detail("Machines", "Metric"); (void)mach; (void)midx; }

void ui_detail_step(int direction) {
    // Phase 6: apply step change to active metric + publish to MQTT
    (void)direction;
}
