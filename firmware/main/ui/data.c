#include "data.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "data";

// ─── Runtime globals ──────────────────────────────────────────────────────────
Machine *g_machines = NULL;
Tank    *g_tanks    = NULL;
Badge   *g_badges   = NULL;

SemaphoreHandle_t g_data_mutex = NULL;

int     g_machine_count = 0;
int     g_tank_count    = 0;
int     g_badge_count   = 0;
int     g_active_badge  = -1;
int     g_log_sec       = 0;
bool    g_mqtt_connected = false;
int     g_mqtt_msg_count = 0;
bool    g_wifi_connected = false;
char    g_wifi_ssid[33]  = "";
int     g_wifi_rssi      = -99;
char    g_device_ip[16]  = "0.0.0.0";
bool    g_tank_drag_active[MAX_TANKS] = {false};
char    g_last_pub_topic[128] = "";

// ─── data_init_psram ─────────────────────────────────────────────────────────
void data_init_psram(void) {
    g_data_mutex = xSemaphoreCreateMutex();
    // No abort needed — if this fails everything breaks anyway

    g_machines = heap_caps_malloc(MAX_MACHINES * sizeof(Machine), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_machines) {
        ESP_LOGE(TAG, "FATAL: g_machines SPIRAM alloc failed (%u bytes)", (unsigned)(MAX_MACHINES * sizeof(Machine)));
        abort();
    }
    memset(g_machines, 0, MAX_MACHINES * sizeof(Machine));
    ESP_LOGI(TAG, "g_machines @ %p (%u bytes, SPIRAM)", (void *)g_machines, (unsigned)(MAX_MACHINES * sizeof(Machine)));

    g_tanks = heap_caps_malloc(MAX_TANKS * sizeof(Tank), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_tanks) {
        ESP_LOGE(TAG, "FATAL: g_tanks SPIRAM alloc failed (%u bytes)", (unsigned)(MAX_TANKS * sizeof(Tank)));
        abort();
    }
    memset(g_tanks, 0, MAX_TANKS * sizeof(Tank));
    ESP_LOGI(TAG, "g_tanks @ %p (%u bytes, SPIRAM)", (void *)g_tanks, (unsigned)(MAX_TANKS * sizeof(Tank)));

    g_badges = heap_caps_malloc(MAX_BADGES * sizeof(Badge), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_badges) {
        ESP_LOGE(TAG, "FATAL: g_badges SPIRAM alloc failed (%u bytes)", (unsigned)(MAX_BADGES * sizeof(Badge)));
        abort();
    }
    memset(g_badges, 0, MAX_BADGES * sizeof(Badge));
    ESP_LOGI(TAG, "g_badges @ %p (%u bytes, SPIRAM)", (void *)g_badges, (unsigned)(MAX_BADGES * sizeof(Badge)));
}

// ─── metric_push ─────────────────────────────────────────────────────────────
void metric_push(Metric *m, float value) {
    m->value = value;
    if (m->history_len < HISTORY_LEN) {
        m->history[m->history_len++] = value;
    } else {
        memmove(m->history, m->history + 1, (HISTORY_LEN - 1) * sizeof(float));
        m->history[HISTORY_LEN - 1] = value;
    }
}
