#include "sim_task.h"
#include "ui/data.h"
#include "mqtt_task.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include <math.h>
#include <string.h>

static const char *TAG = "sim";

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float randf(void) {
    return ((float)(esp_random() & 0xFFFF) / 32768.0f) - 1.0f;
}

static void sim_metric(Metric *m) {
    float next = m->value;
    switch (m->sim_profile) {
    case SIM_RANDOM_WALK: {
        bool bounds = m->rand_max > m->rand_min;
        float lo = bounds ? m->rand_min : m->min_val;
        float hi = bounds ? m->rand_max : m->max_val;
        next = clampf(m->value + randf() * m->step, lo, hi);
        break;
    }
    case SIM_SINE:
        m->sim_phase_acc += (2.0f * (float)M_PI / m->sim_period_s) * ((float)SIM_TICK_MS / 1000.0f);
        next = clampf(m->sim_offset + m->sim_amplitude * sinf(m->sim_phase_acc),
                      m->min_val, m->max_val);
        break;
    case SIM_RAMP:
        next = m->value + m->step;
        if (next > m->max_val) next = m->min_val;
        break;
    case SIM_STEADY:
        break;
    }
    if (m->sim_profile != SIM_STEADY) metric_push(m, next);
}

static int s_pulse_tick[MAX_MACHINES];


static void sim_tick(void) {
    for (int i = 0; i < g_machine_count; i++) {
        Machine *mc = &g_machines[i];
        if (!mc->running) continue;
        for (int k = 0; k < mc->metric_count; k++) {
            // Skip the metric open in the detail view — the user owns it there
            if (i == g_edit_machine && k == g_edit_metric) continue;
            sim_metric(&mc->metrics[k]);
        }
        if (mc->auto_pulse && ++s_pulse_tick[i] >= 5) {
            s_pulse_tick[i] = 0;
            mc->pulses++;
            mqtt_pub_machine_pulse(mc->id, mc->pulses);
        }
    }
    for (int i = 0; i < g_tank_count; i++) {
        Tank *t = &g_tanks[i];
        for (int k = 0; k < t->metric_count; k++) {
            if (k == 0) continue;  // level only changes via drag or fill/drain action
            if (i == g_edit_tank && k == g_edit_metric) continue;  // detail-view hold
            sim_metric(&t->metrics[k]);
        }
    }
}

typedef struct {
    bool running;
    int pulses, metric_count;
    float values[MAX_MACH_METRICS];
} MachineValues;
typedef struct { int metric_count; float values[MAX_TANK_METRICS]; } TankValues;

static void publish_changes(bool connected_now)
{
    static MachineValues previous[MAX_MACHINES];
    static TankValues previous_tanks[MAX_TANKS];
    MachineValues machines[MAX_MACHINES];
    TankValues tanks[MAX_TANKS];
    DATA_LOCK();
    for (int i = 0; i < g_machine_count; i++) {
        Machine *m = &g_machines[i];
        machines[i].running = m->running;
        machines[i].pulses = m->pulses;
        machines[i].metric_count = m->metric_count;
        for (int k = 0; k < m->metric_count; k++) machines[i].values[k] = m->metrics[k].value;
    }
    for (int i = 0; i < g_tank_count; i++) {
        tanks[i].metric_count = g_tanks[i].metric_count;
        for (int k = 0; k < tanks[i].metric_count; k++) tanks[i].values[k] = g_tanks[i].metrics[k].value;
    }
    DATA_UNLOCK();
    // Immutable IDs/keys are safe outside DATA_LOCK; runtime values are a
    // consistent snapshot. No transport or LVGL work occurs under this lock.
    for (int i = 0; i < g_machine_count; i++) {
        MachineValues *m = &machines[i], *last = &previous[i];
        if (connected_now || m->running != last->running)
            mqtt_pub_machine_status(g_machines[i].id, m->running);
        if (connected_now || m->pulses != last->pulses)
            mqtt_pub_machine_pulse(g_machines[i].id, m->pulses);
        for (int k = 0; k < m->metric_count; k++)
            if (connected_now || m->values[k] != last->values[k])
                mqtt_pub_machine_metric(g_machines[i].id, g_machines[i].metrics[k].key, m->values[k]);
        *last = *m;
    }
    for (int i = 0; i < g_tank_count; i++) {
        for (int k = 0; k < tanks[i].metric_count; k++)
            if (connected_now || tanks[i].values[k] != previous_tanks[i].values[k])
                mqtt_pub_tank_metric(g_tanks[i].id, g_tanks[i].metrics[k].key, tanks[i].values[k]);
        previous_tanks[i] = tanks[i];
    }
}

static void sim_task_fn(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount(), last_sim = wake;
    unsigned generation = 0;
    bool first = true;
    ESP_LOGI(TAG, "started: simulation %d ms, change detection %d ms", SIM_TICK_MS, TELEMETRY_CHECK_MS);
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        bool tick = first || now - last_sim >= pdMS_TO_TICKS(SIM_TICK_MS);
        if (tick) {
            DATA_LOCK();
            sim_tick();
            DATA_UNLOCK();
            last_sim = now;
            first = false;
        }
        if (mqtt_is_connected()) {
            unsigned current = mqtt_connection_generation();
            publish_changes(generation != current);
            generation = current;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TELEMETRY_CHECK_MS));
    }
}

void sim_task_start(void) {
    xTaskCreatePinnedToCore(sim_task_fn, "sim", 8192, NULL, 5, NULL, 0);
}
