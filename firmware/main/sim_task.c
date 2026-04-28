#include "data_model.h"
#include "mqtt_task.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
//  sim_task.c — Telemetry simulation: random-walk on all machine metrics.
//  Fires every 1500 ms (matches HTML dashboard behaviour).
//  All running machines have their metrics updated and published to MQTT.
// ─────────────────────────────────────────────────────────────────────────────

static const char *TAG = "sim";

// Clamp float to [lo, hi]
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Random float in [-1, 1]
static inline float randf(void) {
    return ((float)(esp_random() & 0xFFFF) / 32768.0f) - 1.0f;
}

static void sim_machine(Machine *m) {
    if (!m->running) return;

    switch (m->type) {
    case MACH_CNC:
        metric_push(&m->rpm,
            clampf(m->rpm.v + randf() * 120, 0, 8000));
        metric_push(&m->spindle_load,
            clampf(m->spindle_load.v + randf() * 3, 0, 100));
        metric_push(&m->tool_wear,
            clampf(m->tool_wear.v + randf() * 0.4f, 0, 100));
        metric_push(&m->feed_rate,
            clampf(m->feed_rate.v + randf() * 15, 0, 600));
        metric_push(&m->mach_temp,
            clampf(m->mach_temp.v + randf() * 0.3f, 15, 80));
        break;

    case MACH_FILL:
        metric_push(&m->flow_rate,
            clampf(m->flow_rate.v + randf() * 0.5f, 0, 25));
        metric_push(&m->fill_pres,
            clampf(m->fill_pres.v + randf() * 0.05f, 0, 5));
        metric_push(&m->mach_temp,
            clampf(m->mach_temp.v + randf() * 0.2f, 15, 80));
        break;

    case MACH_PACK:
        metric_push(&m->line_speed,
            clampf(m->line_speed.v + randf() * 2, 0, 80));
        metric_push(&m->oee,
            clampf(m->oee.v + randf() * 0.5f, 0, 100));
        metric_push(&m->mach_temp,
            clampf(m->mach_temp.v + randf() * 0.2f, 15, 80));
        break;
    }

    // Auto-pulse every 5 ticks (~7.5 s) when running
    static int tick[MACH_COUNT] = {0};
    for (int i = 0; i < MACH_COUNT; i++) {
        if (&g_machines[i] == m) {
            if (++tick[i] >= 5) {
                tick[i] = 0;
                m->pulses++;
                mqtt_pub_machine_pulse(m->id, m->pulses);
            }
            break;
        }
    }
}

// Publish all current machine metrics to MQTT (called once per sim tick)
static void publish_all(void) {
    for (int i = 0; i < MACH_COUNT; i++) {
        Machine *m = &g_machines[i];
        switch (m->type) {
        case MACH_CNC:
            mqtt_pub_machine_metric(m->id, "rpm",          m->rpm.v);
            mqtt_pub_machine_metric(m->id, "spindle_load", m->spindle_load.v);
            mqtt_pub_machine_metric(m->id, "tool_wear",    m->tool_wear.v);
            mqtt_pub_machine_metric(m->id, "feed_rate",    m->feed_rate.v);
            mqtt_pub_machine_metric(m->id, "temperature",  m->mach_temp.v);
            break;
        case MACH_FILL:
            mqtt_pub_machine_metric(m->id, "flow_rate",    m->flow_rate.v);
            mqtt_pub_machine_metric(m->id, "fill_pressure",m->fill_pres.v);
            mqtt_pub_machine_metric(m->id, "temperature",  m->mach_temp.v);
            break;
        case MACH_PACK:
            mqtt_pub_machine_metric(m->id, "line_speed",   m->line_speed.v);
            mqtt_pub_machine_metric(m->id, "oee",          m->oee.v);
            mqtt_pub_machine_metric(m->id, "temperature",  m->mach_temp.v);
            break;
        }
    }
}

static void sim_task_fn(void *arg) {
    ESP_LOGI(TAG, "simulation task started");
    while (1) {
        for (int i = 0; i < MACH_COUNT; i++) sim_machine(&g_machines[i]);
        if (mqtt_is_connected()) publish_all();
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void sim_task_start(void) {
    xTaskCreatePinnedToCore(sim_task_fn, "sim", 4096, NULL, 5, NULL, 1);
}
