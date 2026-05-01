#include "mqtt_task.h"
#include "data_model.h"
#include "config.h"
#include "ui/ui_main.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include <stdio.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────────────────────
//  mqtt_task.c — esp_mqtt client with WSS transport to Cloudflare Tunnel.
//  No TLS certificate pinning in initial version (Cloudflare public cert).
// ─────────────────────────────────────────────────────────────────────────────

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client = NULL;

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to %s", MQTT_BROKER_URI);
        g_mqtt_connected = true;
        g_mqtt_msg_count = 0;
        ui_notify_mqtt_status(true);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected — will retry");
        g_mqtt_connected = false;
        ui_notify_mqtt_status(false);
        break;
    case MQTT_EVENT_PUBLISHED:
        g_mqtt_msg_count++;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "mqtt error: type=%d",
                 ev->error_handle->error_type);
        break;
    default:
        break;
    }
}

void mqtt_task_start(void) {
    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address = {
                .uri = MQTT_BROKER_URI,
            },
        },
        .credentials = {
            .client_id = MQTT_CLIENT_ID,
        },
        .session = {
            .keepalive = MQTT_KEEPALIVE_S,
            .disable_clean_session = false,
        },
        .network = {
            .reconnect_timeout_ms = 5000,
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "mqtt client started → %s", MQTT_BROKER_URI);
}

void mqtt_task_stop(void) {
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}

bool mqtt_is_connected(void) {
    return g_mqtt_connected;
}

// ─── Publish helpers ─────────────────────────────────────────────────────────

void mqtt_pub_machine_status(const char *id, bool running) {
    if (!s_client) return;
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/machines/%s/status", TOPIC_PREFIX, id);
    esp_mqtt_client_publish(s_client, topic,
                            running ? "RUNNING" : "STOPPED", 0, 0, 0);
}

void mqtt_pub_machine_pulse(const char *id, int count) {
    if (!s_client) return;
    char topic[64], payload[16];
    snprintf(topic,   sizeof(topic),   "%s/machines/%s/pulse",  TOPIC_PREFIX, id);
    snprintf(payload, sizeof(payload), "%d", count);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

void mqtt_pub_machine_metric(const char *id, const char *key, float value) {
    if (!s_client) return;
    char topic[80], payload[16];
    snprintf(topic,   sizeof(topic),   "%s/machines/%s/%s", TOPIC_PREFIX, id, key);
    snprintf(payload, sizeof(payload), "%.1f", value);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

void mqtt_pub_tank_level(const char *id, float pct) {
    if (!s_client) return;
    char topic[64], payload[16];
    snprintf(topic,   sizeof(topic),   "%s/tanks/%s/level", TOPIC_PREFIX, id);
    snprintf(payload, sizeof(payload), "%.1f", pct);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

void mqtt_pub_tank_metric(const char *id, const char *key, float value) {
    if (!s_client) return;
    char topic[80], payload[16];
    snprintf(topic,   sizeof(topic),   "%s/tanks/%s/%s", TOPIC_PREFIX, id, key);
    snprintf(payload, sizeof(payload), "%.1f", value);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

void mqtt_pub_operator(const char *uid, const char *name,
                       const char *role, const char *plant) {
    if (!s_client) return;
    char topic[64], payload[256];
    snprintf(topic, sizeof(topic), "%s/operators/login", TOPIC_PREFIX);
    snprintf(payload, sizeof(payload),
             "{\"uid\":\"%s\",\"name\":\"%s\",\"role\":\"%s\","
             "\"plant\":\"%s\",\"ts\":%lld}",
             uid, name, role, plant,
             (long long)esp_timer_get_time() / 1000000LL);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}
