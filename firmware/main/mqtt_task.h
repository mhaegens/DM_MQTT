#pragma once
#include <stdbool.h>

// ─────────────────────────────────────────────────────────────────────────────
//  mqtt_task.h — MQTT publish API and connection lifecycle.
//  Transport: MQTT over WebSocket Secure to mqttfordm.haegens.be:443
// ─────────────────────────────────────────────────────────────────────────────

void mqtt_task_start(void);
void mqtt_task_stop(void);

// Publish helpers — all QoS 0, fire and forget
void mqtt_pub_machine_status(const char *machine_id, bool running);
void mqtt_pub_machine_pulse(const char *machine_id, int cumulative_count);
void mqtt_pub_machine_metric(const char *machine_id, const char *metric_key, float value);
void mqtt_pub_tank_level(const char *tank_id, float pct);
void mqtt_pub_tank_metric(const char *tank_id, const char *metric_key, float value);
void mqtt_pub_operator(const char *uid, const char *name, const char *role, const char *plant);

bool mqtt_is_connected(void);
