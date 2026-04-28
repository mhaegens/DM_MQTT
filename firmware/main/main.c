#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lvgl.h"

// BSP — download from Waveshare wiki and place in firmware/components/waveshare_bsp/
// The BSP exposes bsp_display_start() which inits the MIPI-DSI panel and GT911 touch.
#include "bsp/waveshare_p4.h"

#include "config.h"
#include "data_model.h"
#include "mqtt_task.h"
#include "sim_task.h"
#include "ui/ui_main.h"
#include "hw/buttons.h"
#include "hw/audio.h"

static const char *TAG = "main";

// ─── Button callback (runs in button task context) ───────────────────────────
static void on_button(BtnEvent ev) {
    // Post to LVGL task via lv_async_call to stay thread-safe
    if (ev == BTN_EVENT_SHORT_PRESS)
        lv_async_call((lv_async_cb_t)ui_btn_short_press, NULL);
    else
        lv_async_call((lv_async_cb_t)ui_btn_long_press,  NULL);
}

// ─── LVGL task — runs on Core 0 alongside the display driver ─────────────────
static void lvgl_task(void *arg) {
    ESP_LOGI(TAG, "LVGL task started on Core %d", xPortGetCoreID());
    while (1) {
        // bsp_display_lock/unlock guards LVGL internals from other tasks
        bsp_display_lock(0);
        lv_timer_handler();
        bsp_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(5));   // ~200 Hz tick; LVGL uses actual elapsed time
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "SAP DM MQTT Simulator — ESP32-P4 booting");

    // ── NVS (required by WiFi) ────────────────────────────────────────────────
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ── Event loop + netif (required by WiFi + MQTT) ──────────────────────────
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    // ── BSP: init MIPI-DSI display, GT911 touch, LVGL 9 ─────────────────────
    // bsp_display_start() allocates PSRAM frame buffers and registers the
    // flush callback for the MIPI-DSI panel. Touch events are wired to LVGL
    // input device automatically by the BSP.
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = {
            .task_priority = 4,
            .task_stack    = 6144,
            .task_affinity = 0,   // Core 0
            .timer_period_ms = 5,
        }
    };
    ESP_ERROR_CHECK(bsp_display_start_with_config(&disp_cfg));
    ESP_LOGI(TAG, "display + touch initialised (720x720 MIPI-DSI)");

    // ── Data model ────────────────────────────────────────────────────────────
    data_model_init();

    // ── UI ────────────────────────────────────────────────────────────────────
    bsp_display_lock(0);
    ui_main_init();
    bsp_display_unlock();

    // ── Hardware button ───────────────────────────────────────────────────────
    buttons_init(on_button);

    // ── Audio stub (Phase 9) ──────────────────────────────────────────────────
    // audio_init() would call buttons_deinit() first to release GPIO9.
    // Left disabled until Phase 9.
    // audio_init();

    // ── WiFi — credentials from NVS (written by captive portal, Phase 8)
    //          fall back to compile-time defaults if NVS empty ────────────────
    // wifi_prov_init() handles both cases; implemented in Phase 8.
    // For Phase 3: use compile-time credentials directly.
    // TODO Phase 8: replace with wifi_prov_init()
    extern void wifi_simple_init(void);
    wifi_simple_init();

    // ── MQTT ─────────────────────────────────────────────────────────────────
    mqtt_task_start();

    // ── Simulation ───────────────────────────────────────────────────────────
    sim_task_start();

    ESP_LOGI(TAG, "all tasks started");

    // ── LVGL periodic handler (if not handled by BSP internally) ─────────────
    // Some BSP versions start their own LVGL task; check bsp_display_start docs.
    // If needed, uncomment:
    // xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 4, NULL, 0);

    // main task idles — all work is in FreeRTOS tasks
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
