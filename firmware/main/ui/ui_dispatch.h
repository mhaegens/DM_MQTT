#pragma once
#include "lvgl.h"

// Only the LVGL task executes callbacks. Producers never touch LVGL's timers.
void ui_dispatch_init(void);
lv_result_t ui_dispatch_call(lv_async_cb_t callback, void *payload);
