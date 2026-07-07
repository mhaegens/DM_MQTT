#pragma once
#include "lvgl.h"
void ui_config_select_create(lv_obj_t *parent);
void ui_config_select_activate(void);   // called by ui_push_config_select — rescans templates
