#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "ui/ui_main.h"
#include "ui/ui_screenshot.h"
#include "ui/data.h"
#include "config.h"
#include "config_loader.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "wifi_portal.h"
#include "mqtt_task.h"
#include "sim_task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "main";

// GPIO35 (BOOT button) polling: short press → back/home, long press → detail step
static void btn_task(void *arg) {
    (void)arg;
    gpio_set_direction(GPIO_BOOT_BTN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_BOOT_BTN, GPIO_PULLUP_ONLY);

    bool last_level = true;  // button is HIGH at rest (pull-up)
    TickType_t press_start = 0;

    while (1) {
        bool level = gpio_get_level(GPIO_BOOT_BTN);

        if (last_level && !level) {
            // Falling edge — button pressed
            press_start = xTaskGetTickCount();
        } else if (!last_level && level) {
            // Rising edge — button released
            uint32_t duration_ms = pdTICKS_TO_MS(xTaskGetTickCount() - press_start);
            if (duration_ms >= 20) {   // debounce
                bsp_display_lock(0);
                if (duration_ms < 600) {
                    ui_btn_short_press();
                } else {
                    ui_btn_long_press();
                }
                bsp_display_unlock();
            }
        }

        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void console_task(void *arg) {
    (void)arg;
    setvbuf(stdin, NULL, _IONBF, 0);
    while (1) {
        int ch = getchar();
        if (ch == EOF) clearerr(stdin);
        if (ch == 'S' || ch == 's') ui_screenshot_take();
        else if (ch == 'D' || ch == 'T' || ch == 'C') {
            bsp_display_lock(0);
            if (ch == 'D') ui_switch_tab(VIEW_DEBUG);
            else if (ch == 'T') ui_switch_tab(VIEW_TANKS);
            else ui_push_config_select();
            bsp_display_unlock();
        } else if (ch == 'R') mqtt_task_reconnect();
        else if (ch == 'P') {
            // Local USB diagnostic convenience: the same provisioning portal
            // as the on-screen Setup via Phone action. Never remotely exposed.
            wifi_manager_start_ap(); wifi_portal_start();
        } else if (ch == 'L') {
            int count = 0; ConfigTemplate *all = config_store_list_all(&count);
            printf("PROFILES %d\n", count);
            for (int i = 0; i < count; ++i)
                printf("PROFILE %s source=%s valid=%d\n", all[i].name, all[i].from_sd ? "SD" : "Flash", all[i].valid);
            free(all);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void on_wifi_ip(void) {
    static char previous_ip[16], previous_ssid[33];
    if (mqtt_is_connected() && !strcmp(previous_ip, g_device_ip) && !strcmp(previous_ssid, g_wifi_ssid)) return;
    snprintf(previous_ip, sizeof(previous_ip), "%s", g_device_ip);
    snprintf(previous_ssid, sizeof(previous_ssid), "%s", g_wifi_ssid);
    // MQTT client not yet started on first IP; start it; on subsequent reconnects call reconnect
    static bool s_mqtt_started = false;
    if (!s_mqtt_started) {
        s_mqtt_started = true;
        mqtt_task_start();
    } else {
        mqtt_task_reconnect();
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "SAP DM MQTT Simulator - ESP32-P4");

    // Network prerequisites — must come before WiFi and SD card init
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Allocate entity arrays in SPIRAM before any other init
    data_init_psram();

    // Load config from SD (or fall back to embedded defaults)
    config_store_init();   // shared storage owns the SD mount for this boot
    ConfigResult cr = config_load();
    ESP_LOGI(TAG, "Config: %s", config_result_str(cr));
    // The SD card is the home of the profile library — copy any bundled
    // config missing from the card onto it (card copies are never touched).
    config_store_seed_sd();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        // NOTE: in AVOID_TEAR mode the LVGL port renders into the panel's own
        // full-frame PSRAM framebuffers; this draw-buffer size is bypassed.
        .buffer_size   = BSP_LCD_H_RES * 50,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma    = true,
            .buff_spiram = false,  // draw buffer must stay in internal DMA-capable SRAM
            .sw_rotate   = false,
        }
    };
    cfg.lvgl_port_cfg.task_affinity = 1;   // pin LVGL render task to CPU 1, free CPU 0 for IDLE
    cfg.lvgl_port_cfg.timer_period_ms = 16;  // ~60fps — direct mode redraws only dirty regions,
                                             // so a render pass no longer needs a 33ms budget
    cfg.lvgl_port_cfg.task_stack = 20480;  // 20KB: deep LVGL object hierarchy in ui_machines_create needs > 7KB default
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();
    bsp_display_brightness_set(80);

    // Set root screen background before UI builds to prevent teal flash on first frame
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xF2F4FA), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
    bsp_display_unlock();

    bsp_display_lock(0);
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) {
        lv_timer_t *timer = lv_indev_get_read_timer(in);
        if (timer) lv_timer_set_period(timer, 10);
    }
    ui_main_init();
    bsp_display_unlock();

    // Start networking — mqtt starts only after WiFi gets an IP; reconnects on IP renewal
    wifi_manager_set_on_ip(on_wifi_ip);
    wifi_manager_init();
    sim_task_start();

    // 4 KB: btn_task runs full UI transitions (ui_switch_tab -> screen refresh,
    // deep flex relayout) under bsp_display_lock — 2 KB was borderline overflow.
    xTaskCreatePinnedToCore(btn_task, "btn", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(console_task, "con", 4096, NULL, 3, NULL, 0);

    // Memory diagnostics — log SPIRAM and internal SRAM state after all init
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Internal SRAM free: %u bytes", (unsigned)free_internal);
    if (free_internal < 100 * 1024) {
        ESP_LOGW(TAG, "Warning: internal SRAM below 100 KB threshold");
    }

    while(1) vTaskDelay(pdMS_TO_TICKS(1000));
}
