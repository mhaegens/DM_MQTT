#include "data_model.h"
#include <string.h>

// ─────────────────────────────────────────────────────────────────────────────
//  data_model.c — Initial state matching PRD §9 default values.
// ─────────────────────────────────────────────────────────────────────────────

Machine  g_machines[MACH_COUNT];
Tank     g_tanks[TANK_COUNT];
Operator g_operators[OP_COUNT];
int      g_active_operator = -1;
int      g_mqtt_msg_count  = 0;
bool     g_mqtt_connected  = false;
bool     g_wifi_connected  = false;
int      g_wifi_rssi       = -99;
char     g_device_ip[16]   = "0.0.0.0";
char     g_mqtt_broker[64] = "mqttfordm.haegens.be";

// ─── Helpers ─────────────────────────────────────────────────────────────────
static void metric_init(Metric *m, float val, float mn, float mx) {
    memset(m, 0, sizeof(*m));
    m->v = val;
    m->min_range = mn;
    m->max_range = mx;
    // Pre-fill history with initial value so sparkline isn't empty on boot
    for (int i = 0; i < HIST_LEN; i++) m->hist[i] = val;
    m->hist_len = HIST_LEN;
    m->hist_head = 0;
}

void metric_push(Metric *m, float value) {
    m->v = value;
    m->hist[m->hist_head] = value;
    m->hist_head = (m->hist_head + 1) % HIST_LEN;
    if (m->hist_len < HIST_LEN) m->hist_len++;
}

// ─── Initialization ───────────────────────────────────────────────────────────
void data_model_init(void) {
    // ── CNC-01 ──
    g_machines[0] = (Machine){
        .id = "cnc01", .name = "CNC-01", .type_label = "CNC Machining",
        .type = MACH_CNC, .running = true, .pulses = 347
    };
    metric_init(&g_machines[0].rpm,          4850, 0,    8000);
    metric_init(&g_machines[0].spindle_load,   62, 0,     100);
    metric_init(&g_machines[0].tool_wear,      34, 0,     100);
    metric_init(&g_machines[0].feed_rate,     380, 0,     600);
    metric_init(&g_machines[0].mach_temp,    41.2, 15,     80);

    // ── CNC-02 (stopped) ──
    g_machines[1] = (Machine){
        .id = "cnc02", .name = "CNC-02", .type_label = "CNC Machining",
        .type = MACH_CNC, .running = false, .pulses = 210
    };
    metric_init(&g_machines[1].rpm,             0, 0,    8000);
    metric_init(&g_machines[1].spindle_load,    0, 0,     100);
    metric_init(&g_machines[1].tool_wear,      71, 0,     100);
    metric_init(&g_machines[1].feed_rate,       0, 0,     600);
    metric_init(&g_machines[1].mach_temp,    28.3, 15,     80);

    // ── FILL-01 ──
    g_machines[2] = (Machine){
        .id = "fill01", .name = "FILL-01", .type_label = "Filling Line",
        .type = MACH_FILL, .running = true, .pulses = 418
    };
    metric_init(&g_machines[2].flow_rate,    12.4, 0,      25);
    metric_init(&g_machines[2].fill_pres,     2.1, 0,       5);
    metric_init(&g_machines[2].mach_temp,    36.8, 15,     80);

    // ── PACK-01 ──
    g_machines[3] = (Machine){
        .id = "pack01", .name = "PACK-01", .type_label = "Packaging",
        .type = MACH_PACK, .running = true, .pulses = 309
    };
    metric_init(&g_machines[3].line_speed,     48, 0,      80);
    metric_init(&g_machines[3].oee,            91, 0,     100);
    metric_init(&g_machines[3].mach_temp,    33.5, 15,     80);

    // ── Tanks ──
    g_tanks[0] = (Tank){ .id = "raw",   .name = "Raw Material",  .phys = {.pct=68} };
    metric_init(&g_tanks[0].level,    68, 0, 100);
    metric_init(&g_tanks[0].tank_temp, 22.4, 5, 70);
    metric_init(&g_tanks[0].flow_in,   4.2, 0, 20);

    g_tanks[1] = (Tank){ .id = "prod",  .name = "Production",    .phys = {.pct=28} };
    metric_init(&g_tanks[1].level,    28, 0, 100);
    metric_init(&g_tanks[1].tank_temp, 38.1, 5, 70);
    metric_init(&g_tanks[1].pressure,   1.4, 0,  5);

    g_tanks[2] = (Tank){ .id = "waste", .name = "Waste",         .phys = {.pct=11} };
    metric_init(&g_tanks[2].level,    11, 0, 100);
    metric_init(&g_tanks[2].tank_temp, 18.6, 5, 70);
    metric_init(&g_tanks[2].ph,         5.2, 0, 14);
    metric_init(&g_tanks[2].flow_out,   1.1, 0, 20);

    // ── Operators ──
    g_operators[0] = (Operator){
        .id="EMP-0042", .name="Michiel Haegens", .role="Shift Lead",
        .role_key="lead", .dept="Production", .plant="Plant BE01",
        .uid="A3:F7:C2:91", .certs={"ISO 9001","Lean","PLC Certified",NULL},
        .avatar_color=0x0050BE
    };
    g_operators[1] = (Operator){
        .id="EMP-0117", .name="Jana Kovacevic", .role="CNC Operator",
        .role_key="operator", .dept="Machining", .plant="Plant BE01",
        .uid="B1:2E:9A:04", .certs={"CNC Level 3","Safety",NULL,NULL},
        .avatar_color=0x0070F2
    };
    g_operators[2] = (Operator){
        .id="EMP-0089", .name="Lars Vermeulen", .role="Process Engineer",
        .role_key="engineer", .dept="Engineering", .plant="Plant BE01",
        .uid="C4:88:F1:3D", .certs={"Six Sigma GB","HAZOP","ISO 14001",NULL},
        .avatar_color=0xB84D00
    };
    g_operators[3] = (Operator){
        .id="EMP-0201", .name="Amira El-Sayed", .role="Quality Inspector",
        .role_key="quality", .dept="Quality", .plant="Plant BE01",
        .uid="D7:A3:55:E9", .certs={"ISO 9001 Lead","FMEA","8D",NULL},
        .avatar_color=0x8A0000
    };
    g_operators[4] = (Operator){
        .id="EMP-0033", .name="Pieter van Dijk", .role="Production Supervisor",
        .role_key="supervisor", .dept="Operations", .plant="Plant BE01",
        .uid="E2:CF:71:B8", .certs={"MES Certified","Lean Black","IFS",NULL},
        .avatar_color=0x4A1A8C
    };
}

Machine *machine_by_id(const char *id) {
    for (int i = 0; i < MACH_COUNT; i++)
        if (strcmp(g_machines[i].id, id) == 0) return &g_machines[i];
    return NULL;
}

Tank *tank_by_id(const char *id) {
    for (int i = 0; i < TANK_COUNT; i++)
        if (strcmp(g_tanks[i].id, id) == 0) return &g_tanks[i];
    return NULL;
}

int data_running_count(void) {
    int n = 0;
    for (int i = 0; i < MACH_COUNT; i++) if (g_machines[i].running) n++;
    return n;
}

int data_total_pulses(void) {
    int n = 0;
    for (int i = 0; i < MACH_COUNT; i++) n += g_machines[i].pulses;
    return n;
}
