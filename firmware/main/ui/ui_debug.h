#pragma once
#include "lvgl.h"
void ui_debug_create(lv_obj_t *parent);
void ui_debug_refresh(void);
void ui_debug_log(const char *type, const char *topic, const char *value);
