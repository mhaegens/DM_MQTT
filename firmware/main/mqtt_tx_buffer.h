#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MQTT_TX_METRICS 104  // 10 machines x 8 + 3 tanks x 8
#define MQTT_TX_EVENTS 32
#define MQTT_TX_TOPIC_LEN 96
#define MQTT_TX_PAYLOAD_LEN 480

typedef struct {
    char topic[MQTT_TX_TOPIC_LEN];
    char payload[MQTT_TX_PAYLOAD_LEN];
    uint32_t created_ms;
    int qos;
    bool retain;
    bool is_metric;
} MqttTxMessage;

typedef struct {
    char topic[MQTT_TX_TOPIC_LEN];
    char payload[24];
    uint32_t created_ms;
    bool pending, inflight, published_valid;
    char inflight_payload[24], published_payload[24];
} MqttTxMetric;

// Caller serializes access. Fixed capacity; telemetry keeps the latest value
// per topic while discrete events preserve FIFO order and take priority.
typedef struct {
    MqttTxMetric metrics[MQTT_TX_METRICS];
    MqttTxMessage events[MQTT_TX_EVENTS];
    unsigned event_head, event_count, next_metric;
    uint32_t coalesced, rejected, suppressed;
} MqttTxBuffer;

bool mqtt_tx_push(MqttTxBuffer *buffer, const MqttTxMessage *message, bool metric);
bool mqtt_tx_pop(MqttTxBuffer *buffer, MqttTxMessage *message);
void mqtt_tx_finish(MqttTxBuffer *buffer, const MqttTxMessage *message, bool success);
void mqtt_tx_clear_metrics(MqttTxBuffer *buffer);
unsigned mqtt_tx_pending(const MqttTxBuffer *buffer);
