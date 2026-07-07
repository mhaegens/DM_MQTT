#pragma once
#include "lvgl.h"
void ui_machines_create(lv_obj_t *parent);
void ui_machines_refresh(void);
void ui_machines_activate(void);         // called on view enter — resets card visibility
