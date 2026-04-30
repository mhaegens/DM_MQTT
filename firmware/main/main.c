#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "ui/ui_main.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "SAP DM MQTT Simulator - ESP32-P4");

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size   = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma    = true,
            .buff_spiram = false,
            .sw_rotate   = false,
        }
    };
    cfg.lvgl_port_cfg.task_affinity = 1;   // pin LVGL render task to CPU 1, free CPU 0 for IDLE
    cfg.lvgl_port_cfg.timer_period_ms = 20; // ~50fps cap, reduce CPU pressure
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();
    bsp_display_brightness_set(80);

    bsp_display_lock(0);
    ui_main_init();
    bsp_display_unlock();

    while(1) vTaskDelay(pdMS_TO_TICKS(1000));
}
