// Live device diagnostics. All values come from actual runtime counters.
#include "ui_debug.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_dispatch.h"
#include "data.h"
#include "config.h"
#include "../mqtt_task.h"
#include "../wifi_manager.h"
#include "../config_store.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_LINES 4
static lv_obj_t *s_values[9], *s_log_labels[LOG_LINES], *s_latest, *s_storage;
static lv_timer_t *s_timer;
static atomic_bool s_storage_busy;
static uint32_t s_previous_count, s_previous_tick;
static bool s_seen_state, s_previous_wifi, s_previous_mqtt;
static char s_last_storage_error[96];

static void cached(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

void ui_debug_log(const char *type, const char *topic, const char *value)
{
    if (!s_log_labels[0]) return;
    for (int i = 0; i < LOG_LINES - 1; ++i) {
        lv_label_set_text(s_log_labels[i], lv_label_get_text(s_log_labels[i + 1]));
        lv_obj_set_style_text_color(s_log_labels[i], COL_TX3, 0);
    }
    char line[130];
    snprintf(line, sizeof(line), "%llus  %.8s  %.72s %.28s",
             (long long)(esp_timer_get_time() / 1000000), type, topic, value ? value : "");
    lv_label_set_text(s_log_labels[LOG_LINES - 1], line);
    lv_obj_set_style_text_color(s_log_labels[LOG_LINES - 1],
                               !strcmp(type, "ERR") ? COL_ERR : COL_TX2, 0);
}

void ui_debug_refresh(void)
{
    MqttStats mqtt; mqtt_get_stats(&mqtt);
    ConfigStorageStats storage; config_store_get_stats(&storage);
    char value[140];
    if (g_mqtt_connected && mqtt.rtt_ms >= 0) snprintf(value, sizeof(value), "%d ms\nBroker round trip", mqtt.rtt_ms);
    else snprintf(value, sizeof(value), "%s\n%s", g_mqtt_connected ? "Connected" : "Offline",
                  g_mqtt_connected ? "Waiting for probe" : "Open connection settings");
    cached(s_values[0], value);
    snprintf(value, sizeof(value), "%lu waiting\n%lu ms / max %lu ms", (unsigned long)mqtt.pending,
             (unsigned long)mqtt.queue_last_ms, (unsigned long)mqtt.queue_max_ms);
    cached(s_values[1], value);
    uint32_t tick = lv_tick_get(), count = g_mqtt_msg_count;
    if (!s_previous_tick) { s_previous_tick = tick; s_previous_count = count; }
    uint32_t elapsed = tick - s_previous_tick;
    if (elapsed >= 1000) {
        snprintf(value, sizeof(value), "%.1f messages/s\n%lu total", (count - s_previous_count) * 1000.0f / elapsed, (unsigned long)count);
        cached(s_values[2], value);
        s_previous_count = count; s_previous_tick = tick;
    }
    snprintf(value, sizeof(value), "%s\n%d dBm | %s", g_wifi_connected ? g_wifi_ssid : "Disconnected",
             g_wifi_rssi, g_wifi_connected ? g_device_ip : "No IP address");
    cached(s_values[3], value);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t minimum = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    snprintf(value, sizeof(value), "%u KB free\nLowest %u KB", (unsigned)(free_internal / 1024), (unsigned)(minimum / 1024));
    cached(s_values[4], value);
    snprintf(value, sizeof(value), "%u MB free\nLargest RAM block %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    cached(s_values[5], value);
    snprintf(value, sizeof(value), "%lu rejected / %lu failed\n%lu unchanged suppressed",
             (unsigned long)mqtt.rejected, (unsigned long)mqtt.failed, (unsigned long)mqtt.suppressed);
    cached(s_values[6], value);
    lv_obj_set_style_text_color(s_values[6], mqtt.rejected || mqtt.failed ? COL_ERR : COL_OK, 0);
    if (storage.sd_ready) snprintf(value, sizeof(value), "SD %.1f GB free\n%d saved profiles",
                                  storage.sd_free_bytes / 1073741824.0, storage.profiles);
    else snprintf(value, sizeof(value), "SD unavailable\n%d profiles in flash", storage.profiles);
    cached(s_values[7], value);
    snprintf(value, sizeof(value), "%d machines, %d tanks\n%d operators | %llu min uptime",
             g_machine_count, g_tank_count, g_badge_count, (long long)(esp_timer_get_time() / 60000000));
    cached(s_values[8], value);
    snprintf(value, sizeof(value), "Last publish: %.120s", mqtt.last_topic[0] ? mqtt.last_topic : "Waiting for telemetry");
    cached(s_latest, value);
    if (storage.last_error[0]) cached(s_storage, storage.last_error);
    else if (storage.invalid_profiles) {
        snprintf(value, sizeof(value), "%d saved files need attention. Open Profiles to inspect them.", storage.invalid_profiles);
        cached(s_storage, value);
    } else cached(s_storage, storage.sd_ready ? "Profiles persist on SD; a flash backup is kept when available." : "SD unavailable. Profiles remain saved in device flash.");
    if (!s_seen_state || s_previous_wifi != g_wifi_connected)
        ui_debug_log(g_wifi_connected ? "OK" : "ERR", g_wifi_connected ? "WiFi connected" : "WiFi disconnected", "");
    if (!s_seen_state || s_previous_mqtt != g_mqtt_connected)
        ui_debug_log(g_mqtt_connected ? "OK" : "ERR", g_mqtt_connected ? "MQTT connected" : "MQTT disconnected", "");
    if (strcmp(s_last_storage_error, storage.last_error)) {
        if (storage.last_error[0]) ui_debug_log("ERR", storage.last_error, "");
        snprintf(s_last_storage_error, sizeof(s_last_storage_error), "%s", storage.last_error);
    }
    s_seen_state = true; s_previous_wifi = g_wifi_connected; s_previous_mqtt = g_mqtt_connected;
}

static void timer_cb(lv_timer_t *timer) { (void)timer; if (g_current_view == VIEW_DEBUG) ui_debug_refresh(); }
static void mqtt_settings(lv_event_t *event) { (void)event; ui_push_mqtt_detail(); }
static void reconnect(lv_event_t *event) { (void)event; mqtt_task_reconnect(); ui_debug_log("INFO", "MQTT reconnect requested", ""); }
static void profiles(lv_event_t *event) { (void)event; ui_push_config_select(); }
static void storage_done(void *arg) { (void)arg; atomic_store(&s_storage_busy, false); if (g_current_view == VIEW_DEBUG) ui_debug_refresh(); }
static void storage_worker(void *arg)
{
    (void)arg;
    config_store_seed_sd();
    int count; ConfigTemplate *all = config_store_list_all(&count); free(all);
    while (ui_dispatch_call(storage_done, NULL) != LV_RESULT_OK) vTaskDelay(pdMS_TO_TICKS(20));
    vTaskDelete(NULL);
}
static void refresh_storage(lv_event_t *event)
{
    (void)event;
    if (atomic_exchange(&s_storage_busy, true)) return;
    cached(s_storage, "Checking SD card and saved profiles...");
    if (xTaskCreate(storage_worker, "storage_check", 6144, NULL, 3, NULL) != pdPASS) {
        atomic_store(&s_storage_busy, false); cached(s_storage, "Cannot start storage check");
    }
}

void ui_debug_create(lv_obj_t *parent)
{
    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_set_size(screen, CONTENT_W, CONTENT_H);
    lv_obj_set_style_bg_color(screen, COL_BG, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, SZ_GAP, 0);
    lv_obj_set_style_pad_row(screen, 8, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    g_screens[VIEW_DEBUG] = screen;
    lv_obj_t *grid = lv_obj_create(screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, CONTENT_W - 2 * SZ_GAP, 280);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    const char *titles[] = {"MQTT latency", "Publish queue", "Live traffic", "WiFi connection", "Internal memory", "PSRAM", "Delivery health", "Profile storage", "Active demo"};
    int width = (CONTENT_W - 4 * SZ_GAP) / 3;
    for (int i = 0; i < 9; ++i) {
        lv_obj_t *card = lv_obj_create(grid);
        lv_obj_set_size(card, width, 88);
        lv_obj_set_pos(card, (i % 3) * (width + SZ_GAP), (i / 3) * 96);
        lv_obj_set_style_bg_color(card, COL_BASE, 0);
        lv_obj_set_style_border_color(card, COL_BD, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, SZ_RADIUS_SM, 0);
        lv_obj_set_style_pad_all(card, 9, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *title = lv_label_create(card);
        lv_label_set_text(title, titles[i]);
        lv_obj_set_style_text_font(title, FONT_SANS_XXS, 0);
        lv_obj_set_style_text_color(title, COL_TX3, 0);
        s_values[i] = lv_label_create(card);
        lv_label_set_text(s_values[i], "Waiting...");
        lv_obj_set_width(s_values[i], width - 20);
        lv_label_set_long_mode(s_values[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(s_values[i], 0, 25);
        lv_obj_set_style_text_font(s_values[i], FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(s_values[i], COL_TX, 0);
    }
    s_latest = lv_label_create(screen);
    lv_obj_set_width(s_latest, LV_PCT(100));
    lv_label_set_long_mode(s_latest, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_latest, FONT_MONO_XXS, 0);
    s_storage = lv_label_create(screen);
    lv_obj_set_width(s_storage, LV_PCT(100));
    lv_label_set_long_mode(s_storage, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_storage, FONT_SANS_XXS, 0);
    lv_obj_set_style_text_color(s_storage, COL_TX2, 0);
    lv_obj_t *history = lv_obj_create(screen);
    lv_obj_set_size(history, LV_PCT(100), 100);
    lv_obj_set_style_bg_color(history, COL_BASE, 0);
    lv_obj_set_style_border_width(history, 0, 0);
    lv_obj_set_style_pad_all(history, 8, 0);
    lv_obj_set_style_pad_row(history, 2, 0);
    lv_obj_set_flex_flow(history, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(history, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < LOG_LINES; ++i) {
        s_log_labels[i] = lv_label_create(history);
        lv_label_set_text(s_log_labels[i], "");
        lv_obj_set_style_text_font(s_log_labels[i], FONT_MONO_XXS, 0);
        lv_obj_set_style_text_color(s_log_labels[i], COL_TX3, 0);
        lv_obj_set_width(s_log_labels[i], LV_PCT(100));
        lv_label_set_long_mode(s_log_labels[i], LV_LABEL_LONG_DOT);
    }
    lv_obj_t *actions = lv_obj_create(screen);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), 44);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 8, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    const char *labels[] = {"Connection", "Reconnect", "Profiles", "Check storage"};
    lv_event_cb_t callbacks[] = {mqtt_settings, reconnect, profiles, refresh_storage};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *button = lv_btn_create(actions);
        lv_obj_set_height(button, 44); lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_bg_color(button, COL_HI_LT, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, callbacks[i], LV_EVENT_CLICKED, NULL);
        lv_obj_t *label = lv_label_create(button); lv_label_set_text(label, labels[i]);
        lv_obj_set_style_text_font(label, FONT_SANS_XXS, 0);
        lv_obj_set_style_text_color(label, COL_HI, 0); lv_obj_center(label);
    }
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
    lv_timer_pause(s_timer);
}

void ui_debug_activate(void) { s_previous_tick = 0; ui_debug_refresh(); lv_timer_resume(s_timer); }
void ui_debug_deactivate(void) { lv_timer_pause(s_timer); }
