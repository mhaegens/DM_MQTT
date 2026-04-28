// ui_machines.c — Phase 4: 2×2 machine card grid with sparklines, arc gauges,
// metric tiles, Start/Stop, Pulse buttons.
// Full implementation in Phase 4; stub compiles cleanly for Phase 1–3.
#include "ui_machines.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data_model.h"
#include "mqtt_task.h"
#include "esp_log.h"

static lv_obj_t *s_screen = NULL;

void ui_machines_create(lv_obj_t *parent) {
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    g_screens[VIEW_MACHINES] = s_screen;

    // TODO Phase 4: build 2×2 machine cards with sparklines, arc gauges,
    // metric tiles, Start/Stop + Pulse buttons per card.
    lv_obj_t *ph = lv_label_create(s_screen);
    lv_label_set_text(ph, "Machines — Phase 4");
    lv_obj_set_style_text_font(ph, FONT_SANS_MD, 0);
    lv_obj_set_style_text_color(ph, COL_TX2, 0);
    lv_obj_center(ph);
}

void ui_machines_refresh(void) { /* Phase 4 */ }
void ui_machines_pulse_first(void) {
    for (int i = 0; i < MACH_COUNT; i++) {
        if (g_machines[i].running) {
            g_machines[i].pulses++;
            mqtt_pub_machine_pulse(g_machines[i].id, g_machines[i].pulses);
            return;
        }
    }
}
