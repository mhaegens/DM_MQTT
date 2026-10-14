#include "ui_dispatch.h"
#include "ui_main.h"
#include "data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdlib.h>

typedef struct { lv_async_cb_t callback; void *payload; } UiCall;
static QueueHandle_t s_calls;

static void drain(lv_timer_t *timer)
{
    (void)timer;
    static int mqtt_state = -1, wifi_state = -1;
    if (mqtt_state != (int)g_mqtt_connected) {
        mqtt_state = g_mqtt_connected; ui_notify_mqtt_status(mqtt_state);
    }
    if (wifi_state != (int)g_wifi_connected) {
        wifi_state = g_wifi_connected; ui_notify_wifi_status(wifi_state, g_wifi_rssi, g_device_ip);
    }
    UiCall call;
    // Bound each pass so event storms cannot monopolize touch processing.
    for (int i = 0; i < 4 && xQueueReceive(s_calls, &call, 0) == pdTRUE; i++)
        call.callback(call.payload);
}

void ui_dispatch_init(void)
{
    s_calls = xQueueCreate(16, sizeof(UiCall));
    if (!s_calls) abort();
    if (!lv_timer_create(drain, 20, NULL)) abort();
}

lv_result_t ui_dispatch_call(lv_async_cb_t callback, void *payload)
{
    UiCall call = { callback, payload };
    return s_calls && callback && xQueueSend(s_calls, &call, 0) == pdTRUE
        ? LV_RESULT_OK : LV_RESULT_INVALID;
}
