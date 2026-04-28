#include "buttons.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "btn";
#define LONG_PRESS_MS 400

static QueueHandle_t s_evt_queue = NULL;
static btn_callback_t s_cb = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg) {
    uint32_t ts = xTaskGetTickCountFromISR();
    xQueueSendFromISR(s_evt_queue, &ts, NULL);
}

static void btn_task(void *arg) {
    uint32_t press_tick = 0;
    bool pressed = false;
    while (1) {
        uint32_t tick;
        if (xQueueReceive(s_evt_queue, &tick, portMAX_DELAY)) {
            int level = gpio_get_level(GPIO_BOOT_BTN);
            if (level == 0 && !pressed) {
                // Button pressed (active low)
                press_tick = tick;
                pressed = true;
            } else if (level == 1 && pressed) {
                // Button released
                uint32_t held_ms = (tick - press_tick) * portTICK_PERIOD_MS;
                pressed = false;
                if (s_cb) {
                    s_cb(held_ms >= LONG_PRESS_MS
                         ? BTN_EVENT_LONG_PRESS
                         : BTN_EVENT_SHORT_PRESS);
                }
            }
        }
    }
}

void buttons_init(btn_callback_t cb) {
    s_cb = cb;
    s_evt_queue = xQueueCreate(4, sizeof(uint32_t));

    gpio_config_t io = {
        .intr_type    = GPIO_INTR_ANYEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << GPIO_BOOT_BTN),
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_BOOT_BTN, gpio_isr_handler, NULL);

    xTaskCreatePinnedToCore(btn_task, "btn", 2048, NULL, 10, NULL, 1);
    ESP_LOGI(TAG, "BOOT button on GPIO%d initialised", GPIO_BOOT_BTN);
}

void buttons_deinit(void) {
    gpio_isr_handler_remove(GPIO_BOOT_BTN);
    ESP_LOGW(TAG, "button handler removed (GPIO9 released for audio)");
}
