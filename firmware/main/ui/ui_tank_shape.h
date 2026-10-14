#pragma once
#include "lvgl.h"

// Shared, allocation-free vessel drawing for the overview and the tank grid.
void ui_tank_shape_draw(lv_layer_t *layer, const lv_area_t *area, float level, lv_color_t fill);
int32_t ui_tank_shape_level_y(int32_t height, float level);
int32_t ui_tank_shape_join_y(int32_t height);
int32_t ui_tank_shape_right_at(int32_t width, int32_t height, float level);
