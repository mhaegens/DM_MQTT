#pragma once
#include "lvgl.h"

void ui_wifi_setup_create(lv_obj_t *parent);
void ui_wifi_setup_activate(void);
void ui_wifi_setup_set_ap_status(const char *msg);  // thread-safe via lv_async_call
