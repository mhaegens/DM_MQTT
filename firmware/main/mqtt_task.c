#include "mqtt_task.h"
#include "mqtt_tx_buffer.h"
#include "ui/data.h"
#include "ui/ui_dispatch.h"
#include "ui/ui_main.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "mqtt_client.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client;
static TaskHandle_t s_tx_task;
static SemaphoreHandle_t s_client_mutex;
static MqttTxBuffer *s_tx;
static portMUX_TYPE s_tx_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_reconnect_timer;
static atomic_int s_disconnect_count;
static atomic_bool s_reconnect_requested;
static atomic_uint s_generation;
static atomic_bool s_snapshot_requested;
#define SNAPSHOT_TOPIC TOPIC_PREFIX "/device/request_snapshot"
static char s_probe_token[32];
static uint32_t s_probe_sent_ms;
static int s_rtt_ms = -1;
#define LATENCY_TOPIC TOPIC_PREFIX "/device/latency/" MQTT_CLIENT_ID

static uint32_t s_failed, s_queue_max_ms, s_queue_last_ms;
static const uint32_t BACKOFF_MS[] = { 1000, 2000, 5000, 10000, 30000 };

static bool pub_options(const char *topic, const char *payload,
                        bool metric, int qos, bool retain)
{
    if (!s_tx || !s_tx_task) return false;
    MqttTxMessage m = { .created_ms = (uint32_t)(esp_timer_get_time() / 1000),
                        .qos = qos, .retain = retain };
    int t = snprintf(m.topic, sizeof(m.topic), "%s", topic);
    int p = snprintf(m.payload, sizeof(m.payload), "%s", payload);
    portENTER_CRITICAL(&s_tx_lock);
    bool ok = t >= 0 && t < (int)sizeof(m.topic) && p >= 0 && p < (int)sizeof(m.payload);
    uint32_t suppressed_before = s_tx->suppressed;
    if (ok) ok = mqtt_tx_push(s_tx, &m, metric);
    else s_tx->rejected++;
    bool wake_sender = ok && s_tx->suppressed == suppressed_before;
    portEXIT_CRITICAL(&s_tx_lock);
    if (wake_sender) xTaskNotifyGive(s_tx_task);
    return ok;
}

static void pub(const char *topic, const char *payload)
{
    pub_options(topic, payload, true, 0, false);
}

void mqtt_get_stats(MqttStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    portENTER_CRITICAL(&s_tx_lock);
    if (s_tx) {
        stats->pending = mqtt_tx_pending(s_tx);
        stats->suppressed = s_tx->suppressed;
        stats->coalesced = s_tx->coalesced;
        stats->rejected = s_tx->rejected;
    }
    stats->rtt_ms = s_rtt_ms;
    stats->failed = s_failed;
    stats->queue_last_ms = s_queue_last_ms;
    stats->queue_max_ms = s_queue_max_ms;
    snprintf(stats->last_topic, sizeof(stats->last_topic), "%s", g_last_pub_topic);
    snprintf(stats->broker_uri, sizeof(stats->broker_uri), "%s", MQTT_BROKER_URI);
    stats->using_lan = false;
    portEXIT_CRITICAL(&s_tx_lock);
}

unsigned mqtt_connection_generation(void)
{
    // Called by the 100 ms simulation observer. Coalesce browser requests,
    // and bound full-state retransmissions without retaining stale profiles.
    static uint32_t last_snapshot_ms;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (atomic_load(&s_snapshot_requested) && now - last_snapshot_ms >= 500 &&
        atomic_exchange(&s_snapshot_requested, false)) {
        portENTER_CRITICAL(&s_tx_lock);
        mqtt_tx_clear_metrics(s_tx);
        portEXIT_CRITICAL(&s_tx_lock);
        atomic_fetch_add(&s_generation, 1);
        last_snapshot_ms = now;
    }
    return atomic_load(&s_generation);
}

static void status_dispatch(void *p)
{
    (void)p;
    ui_notify_mqtt_status(g_mqtt_connected);
}

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    atomic_store(&s_reconnect_requested, true);
    if (s_tx_task) xTaskNotifyGive(s_tx_task);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg; (void)base;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_disconnect_count = 0;
        if (s_reconnect_timer) esp_timer_stop(s_reconnect_timer);
        // A fresh snapshot will replace telemetry from before the outage.
        portENTER_CRITICAL(&s_tx_lock);
        mqtt_tx_clear_metrics(s_tx);
        portEXIT_CRITICAL(&s_tx_lock);
        atomic_fetch_add(&s_generation, 1);
        esp_mqtt_client_subscribe(s_client, LATENCY_TOPIC, 0);
        esp_mqtt_client_subscribe(s_client, SNAPSHOT_TOPIC, 0);
        g_mqtt_connected = true;
        pub_options(STATUS_TOPIC, "online", false, 1, true);
        ui_dispatch_call(status_dispatch, NULL);
        ESP_LOGI(TAG, "connected to %s", MQTT_BROKER_URI);
        break;
    case MQTT_EVENT_DISCONNECTED: {
        g_mqtt_connected = false;
        portENTER_CRITICAL(&s_tx_lock);
        s_rtt_ms = -1;
        portEXIT_CRITICAL(&s_tx_lock);
        ui_dispatch_call(status_dispatch, NULL);
        unsigned last = sizeof(BACKOFF_MS) / sizeof(BACKOFF_MS[0]) - 1;
        unsigned i = s_disconnect_count < (int)last ? (unsigned)s_disconnect_count++ : last;
        if (s_reconnect_timer) {
            esp_timer_stop(s_reconnect_timer);
            esp_timer_start_once(s_reconnect_timer, (uint64_t)BACKOFF_MS[i] * 1000);
        }
        ESP_LOGW(TAG, "disconnected; retry in %lu ms", (unsigned long)BACKOFF_MS[i]);
        break;
    }
    case MQTT_EVENT_PUBLISHED:
        if (s_tx_task) xTaskNotifyGive(s_tx_task);
        break;
    case MQTT_EVENT_DATA: {
        esp_mqtt_event_handle_t ev = event_data;
        if (ev->topic && ev->topic_len == (int)strlen(SNAPSHOT_TOPIC) &&
            !memcmp(ev->topic, SNAPSHOT_TOPIC, ev->topic_len) &&
            ev->current_data_offset == 0 && ev->data_len == 5 &&
            ev->total_data_len == 5 && !memcmp(ev->data, "state", 5))
            atomic_store(&s_snapshot_requested, true);
        if (ev->topic_len == (int)strlen(LATENCY_TOPIC) && ev->topic &&
            !memcmp(ev->topic, LATENCY_TOPIC, ev->topic_len) &&
            ev->current_data_offset == 0 && ev->data_len == ev->total_data_len) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            portENTER_CRITICAL(&s_tx_lock);
            if (s_probe_token[0] && ev->data_len == (int)strlen(s_probe_token) &&
                !memcmp(ev->data, s_probe_token, ev->data_len)) {
                s_rtt_ms = (int)(now - s_probe_sent_ms);
                s_probe_token[0] = 0;
            }
            portEXIT_CRITICAL(&s_tx_lock);
        }
        break;
    }
    case MQTT_EVENT_ERROR: {
        esp_mqtt_event_handle_t ev = event_data;
        if (ev->error_handle) ESP_LOGE(TAG, "error type=%d", ev->error_handle->error_type);
        break;
    }
    default: break;
    }
}

static void tx_task(void *arg)
{
    (void)arg;
    uint32_t last_diagnostics = 0;
    bool retry_pending = false;
    MqttTxMessage retry;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        xSemaphoreTake(s_client_mutex, portMAX_DELAY);
        if (s_client && atomic_exchange(&s_reconnect_requested, false)) {
            if (g_mqtt_connected) {
                esp_mqtt_client_disconnect(s_client);
                atomic_store(&s_reconnect_requested, true);
            } else {
                esp_mqtt_client_reconnect(s_client);
            }
        }
        MqttTxMessage m;
        bool backpressure = false;
        for (int n = 0; s_client && g_mqtt_connected && n < 16; n++) {
            portENTER_CRITICAL(&s_tx_lock);
            bool pending = retry_pending || mqtt_tx_pop(s_tx, &m);
            portEXIT_CRITICAL(&s_tx_lock);
            if (!pending) break;
            if (retry_pending) { m = retry; retry_pending = false; }
            uint32_t wait_ms = (uint32_t)(esp_timer_get_time() / 1000) - m.created_ms;
            // Only this worker may wait for socket/TLS/client locks. Touch and
            // simulation producers copy into fixed storage and return immediately.
            int id = esp_mqtt_client_publish(s_client, m.topic, m.payload, 0, m.qos, m.retain);
            if (id == -2) {
                // QoS 1 outbox is full. Keep this FIFO event until a PUBACK
                // frees space; never turn backpressure into an event drop.
                retry = m; retry_pending = true; backpressure = true;
                break;
            }
            portENTER_CRITICAL(&s_tx_lock);
            mqtt_tx_finish(s_tx, &m, id >= 0);
            s_queue_last_ms = wait_ms;
            if (wait_ms > s_queue_max_ms) s_queue_max_ms = wait_ms;
            if (id < 0) s_failed++;
            else {
                snprintf(g_last_pub_topic, sizeof(g_last_pub_topic), "%s", m.topic);
                g_mqtt_msg_count++;
            }
            portEXIT_CRITICAL(&s_tx_lock);
        }
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (s_client && g_mqtt_connected && now - last_diagnostics >= 10000) {
            MqttStats stats; mqtt_get_stats(&stats);
            char payload[352], probe[32];
            snprintf(payload, sizeof(payload),
                     "{\"rtt_ms\":%d,\"pending\":%lu,\"suppressed\":%lu,\"coalesced\":%lu,\"rejected\":%lu,\"failed\":%lu,\"queue_ms\":%lu,\"queue_max_ms\":%lu,\"internal_free\":%u,\"route\":\"%s\"}",
                     stats.rtt_ms, (unsigned long)stats.pending, (unsigned long)stats.suppressed, (unsigned long)stats.coalesced,
                     (unsigned long)stats.rejected, (unsigned long)stats.failed,
                     (unsigned long)stats.queue_last_ms, (unsigned long)stats.queue_max_ms,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), "Public");
            esp_mqtt_client_publish(s_client, TOPIC_PREFIX "/device/performance", payload, 0, 0, 0);
            snprintf(probe, sizeof(probe), "%lld", esp_timer_get_time());
            portENTER_CRITICAL(&s_tx_lock);
            snprintf(s_probe_token, sizeof(s_probe_token), "%s", probe);
            s_probe_sent_ms = (uint32_t)(esp_timer_get_time() / 1000);
            s_rtt_ms = -1;
            portEXIT_CRITICAL(&s_tx_lock);
            esp_mqtt_client_publish(s_client, LATENCY_TOPIC, probe, 0, 0, 0);
            last_diagnostics = now;
        }
        xSemaphoreGive(s_client_mutex);
        // Keep draining, but yield even under continuous slider/network traffic.
        portENTER_CRITICAL(&s_tx_lock);
        bool more = mqtt_tx_pending(s_tx) != 0;
        portEXIT_CRITICAL(&s_tx_lock);
        if (more && g_mqtt_connected && !backpressure) { xTaskNotifyGive(s_tx_task); vTaskDelay(1); }
    }
}

void mqtt_task_start(void)
{
    if (!s_tx_task) {
        s_tx = heap_caps_calloc(1, sizeof(*s_tx), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_client_mutex = xSemaphoreCreateMutex();
        if (!s_tx || !s_client_mutex) abort();
        if (xTaskCreatePinnedToCore(tx_task, "mqtt_tx", 6144, NULL, 5, &s_tx_task, 0) != pdPASS) abort();
    }
    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    if (s_client) { xSemaphoreGive(s_client_mutex); return; }
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .broker.address.port = MQTT_BROKER_PORT,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = MQTT_CLIENT_ID,
        .session.keepalive = MQTT_KEEPALIVE_S,
        .network.disable_auto_reconnect = true,
        .network.timeout_ms = 2000,
        .outbox.limit = 8192,
        .session.last_will.topic = STATUS_TOPIC,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
    };
    esp_timer_create_args_t ta = { .callback = reconnect_timer_cb, .name = "mqtt_backoff" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_reconnect_timer));
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) abort();
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
    xSemaphoreGive(s_client_mutex);
}

void mqtt_task_stop(void)
{
    if (!s_client) return;
    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    if (g_mqtt_connected) esp_mqtt_client_publish(s_client, STATUS_TOPIC, "offline", 0, 1, true);
    // Stop/join the event task before deleting the timer its disconnect
    // handler can access. The persistent transmitter is serialized above.
    esp_mqtt_client_stop(s_client);
    if (s_reconnect_timer) {
        esp_timer_stop(s_reconnect_timer);
        esp_timer_delete(s_reconnect_timer);
        s_reconnect_timer = NULL;
    }
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    g_mqtt_connected = false;
    atomic_store(&s_reconnect_requested, false);
    xSemaphoreGive(s_client_mutex);
}

bool mqtt_is_connected(void) { return g_mqtt_connected; }

void mqtt_task_reconnect(void)
{
    atomic_store(&s_reconnect_requested, true);
    if (s_tx_task) xTaskNotifyGive(s_tx_task);
}

// Minimal JSON string escaping: backslash, double quote; control chars dropped.
static void json_escape(const char *src, char *dst, size_t n) {
    size_t i = 0;
    for (; *src && i + 2 < n; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') {
            dst[i++] = '\\';
            dst[i++] = (char)c;
        } else if (c >= 0x20) {
            dst[i++] = (char)c;
        }
    }
    dst[i] = '\0';
}

void mqtt_pub_machine_status(const char *id, bool running) {
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/machines/%s/status", TOPIC_PREFIX, id);
    pub_options(topic, running ? "RUNNING" : "STOPPED", false, 1, false);
}

void mqtt_pub_machine_pulse(const char *id, int count) {
    char topic[96], payload[16];
    snprintf(topic,   sizeof(topic),   "%s/machines/%s/pulses", TOPIC_PREFIX, id);
    snprintf(payload, sizeof(payload), "%d", count);
    pub_options(topic, payload, false, 1, false);
}

void mqtt_pub_machine_metric(const char *id, const char *key, float value) {
    char topic[96], payload[24];
    snprintf(topic,   sizeof(topic),   "%s/machines/%s/%s", TOPIC_PREFIX, id, key);
    snprintf(payload, sizeof(payload), "%.2f", value);
    if (!strcmp(payload, "-0.00")) strcpy(payload, "0.00");
    pub(topic, payload);
}

void mqtt_pub_tank_metric(const char *id, const char *key, float value) {
    char topic[96], payload[24];
    snprintf(topic,   sizeof(topic),   "%s/tanks/%s/%s", TOPIC_PREFIX, id, key);
    snprintf(payload, sizeof(payload), "%.2f", value);
    if (!strcmp(payload, "-0.00")) strcpy(payload, "0.00");
    pub(topic, payload);
}

void mqtt_pub_operator(const char *uid, const char *name,
                       const char *role, const char *plant) {
    char topic[64], payload[MQTT_TX_PAYLOAD_LEN];
    char e_uid[48], e_name[96], e_role[96], e_plant[64];
    int64_t ts_ms = esp_timer_get_time() / 1000;
    json_escape(uid,   e_uid,   sizeof(e_uid));
    json_escape(name,  e_name,  sizeof(e_name));
    json_escape(role,  e_role,  sizeof(e_role));
    json_escape(plant, e_plant, sizeof(e_plant));
    snprintf(topic, sizeof(topic), "%s/operators/login", TOPIC_PREFIX);
    snprintf(payload, sizeof(payload),
             "{\"uid\":\"%s\",\"name\":\"%s\",\"role\":\"%s\",\"plant\":\"%s\",\"ts\":%lld}",
             e_uid, e_name, e_role, e_plant, ts_ms);
    ESP_LOGI(TAG, "pub %s", topic);
    pub_options(topic, payload, false, 1, false);
}
