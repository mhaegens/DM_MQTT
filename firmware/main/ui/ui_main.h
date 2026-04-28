#pragma once
#include <stdbool.h>
#include "lvgl.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ui_main.h — Top-level UI orchestration: LVGL init, screen management,
//  shell bar, tab bar, screen transitions.
// ─────────────────────────────────────────────────────────────────────────────

typedef enum {
    VIEW_HOME     = 0,
    VIEW_MACHINES = 1,
    VIEW_TANKS    = 2,
    VIEW_DEBUG    = 3,
    VIEW_NFC      = 4,
    VIEW_DETAIL   = 5,   // not a tab; pushed over any tab view
} ViewId;

void ui_main_init(void);
void ui_switch_tab(ViewId view);
void ui_push_detail(void);   // push detail overlay, save return tab
void ui_pop_detail(void);    // return to previous tab

// Called from MQTT and WiFi event handlers (thread-safe: posts to LVGL queue)
void ui_notify_mqtt_status(bool connected);
void ui_notify_wifi_status(bool connected, int rssi, const char *ip);

// Called from button handler
void ui_btn_short_press(void);
void ui_btn_long_press(void);

// Global LVGL objects accessible to screen modules
extern lv_obj_t *g_shell_bar;
extern lv_obj_t *g_tab_bar;
extern lv_obj_t *g_content;      // parent for all screen content
extern lv_obj_t *g_screens[6];   // one lv_obj per ViewId

// Currently active view
extern ViewId g_current_view;
