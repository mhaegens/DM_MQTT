#include "data.h"
#include <string.h>

// ─── Runtime globals ──────────────────────────────────────────────────────────
Machine g_machines[MAX_MACHINES];
Tank    g_tanks[MAX_TANKS];
Badge   g_badges[MAX_BADGES];

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
