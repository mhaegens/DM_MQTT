#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define HISTORY_LEN   32

// ─── Colors (uint32_t raw hex — only defines NOT already in ui_theme.h) ────
// ui_theme.h owns the lv_color_t versions of all standard COL_* tokens.
// These four border-shade variants are unique to data.h and used in UI code
// via lv_color_hex(COL_OK_BD) etc.
#define COL_HI_MID   0xC5DCFF
#define COL_OK_BD    0x9ADDB8
#define COL_WARN_BD  0xF8C07E
#define COL_ERR_BD   0xFFAAAA

// ─── Simulation profile ────────────────────────────────────────────────────
typedef enum { SIM_RANDOM_WALK = 0, SIM_SINE, SIM_RAMP, SIM_STEADY } SimProfile;

// ─── Viz/input types ───────────────────────────────────────────────────────
typedef enum { VIZ_SPARK=0, VIZ_ARC, VIZ_THERMO, VIZ_PH, VIZ_TANK, VIZ_BULLET } VizType;
typedef enum { INPUT_STEP=0, INPUT_SLIDER } InputType;

// ─── Metric threshold ──────────────────────────────────────────────────────
typedef struct { float limit; uint32_t color; } Thr;

// ─── Metric definition (const) and runtime state (mutable) ─────────────────
typedef struct {
    char      key[32];
    char      label[48];
    char      unit[16];
    float     init_val;
    float     min_val, max_val, step;
    float     rand_min, rand_max;
    VizType   viz;
    InputType input;
    Thr       thr[3];
    int       thr_count;
    uint32_t  color;
    // runtime
    float     value;
    float     history[HISTORY_LEN];
    int       history_len;
    // simulation
    SimProfile sim_profile;
    float      sim_amplitude;
    float      sim_period_s;
    float      sim_offset;
    float      sim_phase_acc;
} Metric;

// ─── Machine ───────────────────────────────────────────────────────────────
#define MAX_MACHINES     10
#define MAX_TANKS        3
#define MAX_BADGES       12
#define MAX_MACH_METRICS 8
#define MAX_TANK_METRICS 8
#define MAX_CERTS        6

// Compat aliases for callers that used the old names
#define MACHINE_COUNT MAX_MACHINES
#define TANK_COUNT    MAX_TANKS
#define BADGE_COUNT   MAX_BADGES

typedef struct {
    char  id[16];
    char  name[32];
    char  type[48];
    bool  running;
    bool  auto_pulse;
    int   pulses;
    Metric metrics[MAX_MACH_METRICS];
    int    metric_count;
} Machine;

// ─── Tank ──────────────────────────────────────────────────────────────────
typedef struct {
    char  id[16];
    char  name[32];
    Metric metrics[MAX_TANK_METRICS];
    int    metric_count;
    float  phase;   // wave animation phase (updated by timer)
    float  slosh;   // slosh offset
} Tank;

// ─── Badge (operator) ──────────────────────────────────────────────────────
typedef struct {
    char     id[16];
    char     name[48];
    char     role[48];
    char     role_key[16];
    char     dept[32];
    char     plant[32];
    char     uid[24];
    char     certs[MAX_CERTS][32];
    int      cert_count;
    uint32_t avatar_color;
} Badge;

// ─── Global data ───────────────────────────────────────────────────────────
extern Machine *g_machines;
extern Tank    *g_tanks;
extern Badge   *g_badges;
extern int     g_machine_count;
extern int     g_tank_count;
extern int     g_badge_count;
extern int     g_active_badge;
extern int     g_log_sec;
extern bool    g_mqtt_connected;
extern int     g_mqtt_msg_count;
extern bool    g_wifi_connected;
extern char    g_wifi_ssid[33];   // currently connected SSID, "" when disconnected
extern int     g_wifi_rssi;
extern char    g_device_ip[16];
extern bool    g_tank_drag_active[MAX_TANKS];
extern char    g_last_pub_topic[128];

void metric_push(Metric *m, float value);
void data_init_psram(void);

extern SemaphoreHandle_t g_data_mutex;

// Convenience macros — always use these, never take/give directly
#define DATA_LOCK()    xSemaphoreTake(g_data_mutex, portMAX_DELAY)
#define DATA_UNLOCK()  xSemaphoreGive(g_data_mutex)

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
