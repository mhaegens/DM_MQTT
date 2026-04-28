#pragma once
#include <stdint.h>
#include <stdbool.h>

// ─────────────────────────────────────────────────────────────────────────────
//  data_model.h — All application state structs.
//  Circular history buffers for sparklines; single-value metrics for gauges.
// ─────────────────────────────────────────────────────────────────────────────

#define HIST_LEN  40    // sparkline history depth (matches HTML)
#define MACH_COUNT 4
#define TANK_COUNT 3
#define OP_COUNT   5

// ─── Metric ──────────────────────────────────────────────────────────────────
typedef struct {
    float   v;               // current value
    float   hist[HIST_LEN];  // circular history (only used for sparkline metrics)
    int     hist_head;       // next write index
    int     hist_len;        // number of valid samples (0 → HIST_LEN)
    float   min_range;       // display range min (from metric definition)
    float   max_range;       // display range max
} Metric;

void metric_push(Metric *m, float value);   // push to circular history + update v

// ─── Machine ─────────────────────────────────────────────────────────────────
typedef enum {
    MACH_CNC,
    MACH_FILL,
    MACH_PACK,
} MachineType;

typedef struct {
    const char  *id;        // "cnc01"
    const char  *name;      // "CNC-01"
    const char  *type_label;// "CNC Machining"
    MachineType  type;
    bool         running;
    int          pulses;
    // CNC metrics
    Metric rpm;             // viz: sparkline
    Metric spindle_load;    // viz: arc gauge (%)
    Metric tool_wear;       // viz: arc gauge (%)
    Metric feed_rate;       // viz: bullet chart
    Metric mach_temp;       // viz: thermometer
    // FILL-01 metrics
    Metric flow_rate;       // viz: sparkline
    Metric fill_pres;       // viz: arc gauge
    // PACK-01 metrics
    Metric line_speed;      // viz: sparkline
    Metric oee;             // viz: arc gauge (%)
} Machine;

// ─── Tank physics ─────────────────────────────────────────────────────────────
typedef struct {
    float pct;      // fill level 0–100
    float phase;    // wave phase (radians, advances each frame)
    float slosh;    // horizontal momentum from drag (dampens × 0.92/frame)
    bool  dragging;
    uint32_t last_mqtt_ms; // throttle MQTT publishes to 400 ms during drag
} TankPhysics;

// ─── Tank ─────────────────────────────────────────────────────────────────────
typedef struct {
    const char  *id;         // "raw"
    const char  *name;       // "Raw Material"
    TankPhysics  phys;
    Metric       level;      // viz: tank
    Metric       tank_temp;  // viz: thermometer
    Metric       flow_in;    // RAW: viz sparkline
    Metric       pressure;   // PROD: viz arc gauge
    Metric       ph;         // WASTE: viz ph band
    Metric       flow_out;   // WASTE: viz sparkline
} Tank;

// ─── Operator ─────────────────────────────────────────────────────────────────
typedef struct {
    const char *id;          // "EMP-0042"
    const char *name;        // "Michiel Haegens"
    const char *role;        // "Shift Lead"
    const char *role_key;    // "lead" — maps to badge gradient
    const char *dept;        // "Production"
    const char *plant;       // "Plant BE01"
    const char *uid;         // "A3:F7:C2:91"
    const char *certs[4];    // certification labels (NULL-terminated)
    uint32_t    avatar_color;// hex color for avatar background
} Operator;

// ─── Global state ─────────────────────────────────────────────────────────────
extern Machine  g_machines[MACH_COUNT];
extern Tank     g_tanks[TANK_COUNT];
extern Operator g_operators[OP_COUNT];
extern int      g_active_operator; // index into g_operators, -1 = none
extern int      g_mqtt_msg_count;
extern bool     g_mqtt_connected;
extern bool     g_wifi_connected;
extern int      g_wifi_rssi;
extern char     g_device_ip[16];
extern char     g_mqtt_broker[64];

void data_model_init(void);
Machine *machine_by_id(const char *id);
Tank    *tank_by_id(const char *id);

// Total running machines and total pulses today (for Home KPIs)
int data_running_count(void);
int data_total_pulses(void);
