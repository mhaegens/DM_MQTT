#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "ui/ui_main.h"
#include "ui/ui_screenshot.h"
#include "config.h"

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
    while (1) {
        int ch = getchar();
        if (ch == 'S' || ch == 's') {
            ui_screenshot_take();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

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

    xTaskCreatePinnedToCore(btn_task, "btn", 2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(console_task, "con", 8192, NULL, 3, NULL, 0);

    while(1) vTaskDelay(pdMS_TO_TICKS(1000));
}
