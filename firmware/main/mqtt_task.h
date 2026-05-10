#pragma once
#include <stdbool.h>

void mqtt_task_start(void);
void mqtt_task_stop(void);
bool mqtt_is_connected(void);

void mqtt_pub_machine_status(const char *id, bool running);
void mqtt_pub_machine_pulse(const char *id, int count);
void mqtt_pub_machine_metric(const char *id, const char *key, float value);
void mqtt_pub_tank_metric(const char *id, const char *key, float value);
void mqtt_pub_operator(const char *uid, const char *name,
                       const char *role, const char *plant);
