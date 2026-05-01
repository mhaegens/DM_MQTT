#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define HISTORY_LEN   32

// ─── Colors (uint32_t raw hex — only defines NOT already in ui_theme.h) ────
// ui_theme.h owns the lv_color_t versions of all standard COL_* tokens.
// These four border-shade variants are unique to data.h and used in UI code
// via lv_color_hex(COL_OK_BD) etc.
#define COL_HI_MID   0xC5DCFF
#define COL_OK_BD    0x9ADDB8
#define COL_WARN_BD  0xF8C07E
#define COL_ERR_BD   0xFFAAAA

// ─── Viz/input types ───────────────────────────────────────────────────────
typedef enum { VIZ_SPARK=0, VIZ_ARC, VIZ_THERMO, VIZ_PH, VIZ_TANK, VIZ_BULLET } VizType;
typedef enum { INPUT_STEP=0, INPUT_SLIDER } InputType;

// ─── Metric threshold ──────────────────────────────────────────────────────
typedef struct { float limit; uint32_t color; } Thr;

// ─── Metric definition (const) and runtime state (mutable) ─────────────────
typedef struct {
    const char *key;
    const char *label;
    const char *unit;
    float       init_val;
    float       min_val, max_val, step;
    float       rand_min, rand_max;
    VizType     viz;
    InputType   input;
    Thr         thr[3];
    int         thr_count;
    uint32_t    color;
    // runtime
    float       value;
    float       history[HISTORY_LEN];
    int         history_len;
} Metric;

// ─── Machine ───────────────────────────────────────────────────────────────
#define MAX_MACH_METRICS 5
typedef struct {
    const char *id, *name, *type, *topic;
    bool        running;
    int         pulses;
    Metric      metrics[MAX_MACH_METRICS];
    int         metric_count;
} Machine;

// ─── Tank ──────────────────────────────────────────────────────────────────
#define MAX_TANK_METRICS 5
typedef struct {
    const char *id, *name, *topic;
    Metric      metrics[MAX_TANK_METRICS];
    int         metric_count;
    float       phase;   // wave animation phase (updated by timer)
    float       slosh;   // slosh offset
} Tank;

// ─── Badge (operator) ──────────────────────────────────────────────────────
#define MAX_CERTS 3
typedef struct {
    const char *id, *name, *role, *role_key, *dept, *plant, *uid;
    const char *certs[MAX_CERTS];
    int         cert_count;
    uint32_t    avatar_color;
} Badge;

// ─── Global data ───────────────────────────────────────────────────────────
#define MACHINE_COUNT 4
#define TANK_COUNT    3
#define BADGE_COUNT   5

// Runtime state (updated by wifi/mqtt tasks later; static placeholders for UI mockup)
extern int  g_mqtt_msg_count;
extern int  g_wifi_rssi;
extern char g_device_ip[16];

extern Machine g_machines[MACHINE_COUNT];
extern Tank    g_tanks[TANK_COUNT];
extern Badge   g_badges[BADGE_COUNT];
extern int     g_active_badge;
extern int     g_log_sec;
extern bool    g_mqtt_connected;

// Helper: get tank level (always metrics[0].value)
static inline float tank_level(const Tank *t) { return t->metrics[0].value; }

// Helper: color by fill level (returns raw uint32_t for lv_color_hex())
static inline uint32_t tank_fill_color(float pct) {
    if (pct < 15.0f) return 0xBB0000;  // red
    if (pct < 40.0f) return 0xE76500;  // orange
    if (pct < 75.0f) return 0x0070F2;  // blue
    return 0x0092D1;                    // cyan
}

// Helper: metric color via threshold or default (returns raw uint32_t)
static inline uint32_t metric_color(const Metric *m) {
    for (int i = 0; i < m->thr_count; i++)
        if (m->value <= m->thr[i].limit) return m->thr[i].color;
    if (m->thr_count > 0) return 0xBB0000;  // red = over threshold
    return m->color ? m->color : 0x0070F2;  // fallback = blue
}
