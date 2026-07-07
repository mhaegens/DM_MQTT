#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "ui_theme.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ui_main.h — Top-level UI orchestration: shell bar, tab bar, screen
//  containers, navigation, and font extern declarations.
//
//  All UI screen modules include this header to access shared globals,
//  font macros, and navigation functions.
// ─────────────────────────────────────────────────────────────────────────────

// ─── View IDs ─────────────────────────────────────────────────────────────────
// ORDERING CONTRACT: tab screens (VIEW_HOME..VIEW_NFC) MUST stay before overlay
// screens (VIEW_NFC+1..). ui_btn_short_press() uses > VIEW_NFC to distinguish them.
typedef enum {
    VIEW_HOME     = 0,
    VIEW_MACHINES = 1,
    VIEW_TANKS    = 2,
    VIEW_DEBUG    = 3,
    VIEW_NFC      = 4,
    VIEW_DETAIL    = 5,   // not a tab; pushed over any tab view
    VIEW_TANK_OVW  = 6,   // not a tab; pushed over Tanks tab
    VIEW_WIFI_SETUP     = 7,  // not a tab; pushed over any tab via WiFi pill/card
    VIEW_MQTT_DETAIL    = 8,  // not a tab; pushed over any tab via MQTT pill
    VIEW_WIFI_PASSWORD  = 9,  // not a tab; pushed from wifi_setup Scan & Join list
    VIEW_CONFIG_SEL     = 10, // not a tab; pushed from Home config-template tile
    VIEW_COUNT          = 11,
} ViewId;

// ─── Global LVGL objects (for use by screen modules) ─────────────────────────
extern lv_obj_t *g_screens[VIEW_COUNT];   // one container per ViewId
extern lv_obj_t *g_shell_bar;
extern lv_obj_t *g_tab_bar;
extern lv_obj_t *g_crumb_bar;            // breadcrumb bar shown during detail view
extern lv_obj_t *g_content;              // parent container for all screen modules
extern lv_obj_t *g_shell_subtitle;       // subtitle label; updated by push/pop detail
extern ViewId    g_current_view;
extern ViewId    g_prev_view;            // saved by ui_push_detail, restored by pop

// ─── Init and navigation ──────────────────────────────────────────────────────
void ui_main_init(void);
void ui_switch_tab(ViewId v);
void ui_push_detail(const char *back_label, const char *metric_label);
void ui_pop_detail(void);
void ui_push_tank_ovw(int tank_idx);
void ui_push_wifi_setup(void);
void ui_push_wifi_password(const char *ssid);
void ui_push_mqtt_detail(void);
void ui_push_config_select(void);

// ─── Status update callbacks (called from MQTT/WiFi event handlers) ───────────
void ui_notify_mqtt_status(bool connected);
void ui_notify_wifi_status(bool connected, int rssi, const char *ip);

// ─── Hardware button callbacks ────────────────────────────────────────────────
void ui_btn_short_press(void);
void ui_btn_long_press(void);

// ─── Font externs and FONT_* macros ──────────────────────────────────────────
// Re-exported via ui_theme.h (included above). Any code that includes ui_main.h
// already has access to:
//   extern lv_font_t ibm_plex_sans_40 / _32 / _26 / _22 / _18 / _15
//   extern lv_font_t ibm_plex_mono_26 / _18 / _15
//   FONT_SANS_XL / FONT_SANS_LG / FONT_SANS_MD / FONT_SANS_SM / FONT_SANS_XS / FONT_SANS_XXS
//   FONT_MONO_MD / FONT_MONO_XS / FONT_MONO_XXS
