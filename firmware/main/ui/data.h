#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

// ─── Geometry ──────────────────────────────────────────────────────────────
#define SCREEN_W     720
#define SCREEN_H     720
#define SHELL_H       64
#define TABBAR_H      72
#define CONTENT_H    (SCREEN_H - SHELL_H - TABBAR_H)  // 584
#define HISTORY_LEN   32

// ─── Colors ────────────────────────────────────────────────────────────────
#define COL_HI       0x0070F2
#define COL_HI_DK    0x0050BE
#define COL_HI_LT    0xE8F2FF
#define COL_HI_MID   0xC5DCFF
#define COL_OK       0x107E3E
#define COL_OK_LT    0xE3F5EC
#define COL_OK_BD    0x9ADDB8
#define COL_WARN     0xE76500
#define COL_WARN_LT  0xFEF0E5
#define COL_WARN_BD  0xF8C07E
#define COL_ERR      0xBB0000
#define COL_ERR_LT   0xFFE8E8
#define COL_ERR_BD   0xFFAAAA
#define COL_TEAL     0x045B7A
#define COL_TEAL_LT  0xE0F2F8
#define COL_PUR      0x6A28C8
#define COL_PUR_LT   0xF2EAFF
#define COL_BG       0xF2F4FA
#define COL_BASE     0xFFFFFF
#define COL_BASE2    0xEEF1F8
#define COL_BASE3    0xE3E6EF
#define COL_BD       0xDDE1EA
#define COL_BD_STR   0xB8BFCC
#define COL_TX       0x1D2D3E
#define COL_TX2      0x4A6080
#define COL_TX3      0x8090A8
#define COL_DBG_BG   0x080F1C
#define COL_DBG_PUB  0x5BB2FF
#define COL_DBG_OK   0x4CC87A
#define COL_DBG_ERR  0xFF6666
#define COL_DBG_TS   0x334E68

// ─── Size constants ────────────────────────────────────────────────────────
#define SZ_RADIUS    12
#define SZ_RADIUS_SM  8
#define SZ_GAP       10
#define SZ_CARD_PAD  12
#define SZ_BTN       80
#define SZ_BTN_SM    64

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
#define MAX_TANK_METRICS 4
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

extern Machine g_machines[MACHINE_COUNT];
extern Tank    g_tanks[TANK_COUNT];
extern Badge   g_badges[BADGE_COUNT];
extern int     g_active_badge;
extern int     g_log_sec;

// Helper: get tank level (always metrics[0].value)
static inline float tank_level(const Tank *t) { return t->metrics[0].value; }

// Helper: color by fill level
static inline uint32_t tank_fill_color(float pct) {
    if (pct < 15.0f) return COL_ERR;
    if (pct < 40.0f) return COL_WARN;
    if (pct < 75.0f) return COL_HI;
    return 0x0092D1;
}

// Helper: metric color via threshold or default
static inline uint32_t metric_color(const Metric *m) {
    for (int i = 0; i < m->thr_count; i++)
        if (m->value <= m->thr[i].limit) return m->thr[i].color;
    if (m->thr_count > 0) return COL_ERR;
    return m->color ? m->color : COL_HI;
}
