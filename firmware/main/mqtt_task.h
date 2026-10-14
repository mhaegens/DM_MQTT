#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int rtt_ms;
    uint32_t pending, coalesced, rejected, failed, suppressed;
    uint32_t queue_last_ms, queue_max_ms;
    char last_topic[128];
    char broker_uri[192];
    bool using_lan;
} MqttStats;
void mqtt_get_stats(MqttStats *stats);
unsigned mqtt_connection_generation(void);

void mqtt_task_start(void);
void mqtt_task_stop(void);
void mqtt_task_reconnect(void);
bool mqtt_is_connected(void);

void mqtt_pub_machine_status(const char *id, bool running);
void mqtt_pub_machine_pulse(const char *id, int count);
void mqtt_pub_machine_metric(const char *id, const char *key, float value);
void mqtt_pub_tank_metric(const char *id, const char *key, float value);
void mqtt_pub_operator(const char *uid, const char *name,
                       const char *role, const char *plant);
