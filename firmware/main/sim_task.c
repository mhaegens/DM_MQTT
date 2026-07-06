#include "sim_task.h"
#include "ui/data.h"
#include "mqtt_task.h"
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
        float lo = m->rand_min > 0.0f ? m->rand_min : m->min_val;
        float hi = m->rand_max > 0.0f ? m->rand_max : m->max_val;
        next = clampf(m->value + randf() * m->step, lo, hi);
        break;
    }
    case SIM_SINE:
        m->sim_phase_acc += (2.0f * (float)M_PI / m->sim_period_s) * 1.5f;
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
static bool s_last_running[MAX_MACHINES];

static void sim_tick(void) {
    for (int i = 0; i < g_machine_count; i++) {
        Machine *mc = &g_machines[i];
        if (!mc->running) continue;
        for (int k = 0; k < mc->metric_count; k++)
            sim_metric(&mc->metrics[k]);
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
            sim_metric(&t->metrics[k]);
        }
    }
}

static void publish_all(void) {
    for (int i = 0; i < g_machine_count; i++) {
        Machine *mc = &g_machines[i];
        // status: only publish when state changed
        if (mc->running != s_last_running[i]) {
            mqtt_pub_machine_status(mc->id, mc->running);
            s_last_running[i] = mc->running;
        }
        // metrics: skip stopped machines — values are stale/flat
        if (!mc->running) continue;
        for (int k = 0; k < mc->metric_count; k++)
            mqtt_pub_machine_metric(mc->id, mc->metrics[k].key, mc->metrics[k].value);
    }
    for (int i = 0; i < g_tank_count; i++) {
        Tank *t = &g_tanks[i];
        for (int k = 0; k < t->metric_count; k++)
            mqtt_pub_tank_metric(t->id, t->metrics[k].key, t->metrics[k].value);
    }
    ESP_LOGD(TAG, "published %d machines, %d tanks", g_machine_count, g_tank_count);
}

static void sim_task_fn(void *arg) {
    memset(s_pulse_tick,   0, sizeof(s_pulse_tick));
    memset(s_last_running, 0, sizeof(s_last_running));
    // Force a status publish on first cycle by marking all as !running initially
    // (s_last_running is already false; initial running state differs for running machines)
    ESP_LOGI(TAG, "started — %d machines, %d tanks", g_machine_count, g_tank_count);
    for (int i = 0; i < g_machine_count; i++)
        ESP_LOGI(TAG, "  machine[%d] id=%s auto_pulse=%d", i, g_machines[i].id, g_machines[i].auto_pulse);
    for (int i = 0; i < g_tank_count; i++)
        ESP_LOGI(TAG, "  tank[%d] id=%s", i, g_tanks[i].id);
    while (1) {
        DATA_LOCK();
        sim_tick();
        DATA_UNLOCK();
        if (mqtt_is_connected()) publish_all();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void sim_task_start(void) {
    xTaskCreatePinnedToCore(sim_task_fn, "sim", 8192, NULL, 5, NULL, 0);
}
