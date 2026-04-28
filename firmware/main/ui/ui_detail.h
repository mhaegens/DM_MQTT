#pragma once
#include "lvgl.h"
void ui_detail_create(lv_obj_t *parent);
void ui_detail_open_tank_level(int tank_idx);
void ui_detail_open_tank_metric(int tank_idx, int metric_idx);
void ui_detail_open_machine_metric(int machine_idx, int metric_idx);
void ui_detail_step(int direction);  // +1 increment, -1 decrement (BOOT button)
