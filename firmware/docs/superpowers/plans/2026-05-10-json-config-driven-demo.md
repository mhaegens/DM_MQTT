# JSON-Driven Demo Configuration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hardcoded C entity data with a JSON config file (SD card primary, embedded fallback), wire up the dormant sim and MQTT tasks, and make entity counts dynamic in the UI.

**Architecture:** `config_load()` runs at boot, populates `g_machines[]`/`g_tanks[]`/`g_badges[]` from SD-card JSON (or embedded default), then `sim_task` and `mqtt_task` loop generically over those arrays every 1500 ms. The LVGL UI screens read the same global arrays and their existing metric-array structure already handles variable metrics per entity.

**Tech Stack:** ESP-IDF v5.3 — SDMMC (4-bit SDIO), cJSON (bundled `json` component), `esp_mqtt` (WSS), LVGL 9, FreeRTOS. Build: `idf.py build && idf.py flash monitor`. Screenshot: `python3 tools/screenshot.py` from `firmware/`.

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `main/ui/data.h` | Modify | Add `SimProfile` enum + sim fields to `Metric`; `MAX_*` constants; `char[]` ids; `metric_push` decl |
| `main/ui/data.c` | Modify | Remove hardcoded init; add `metric_push` impl; declare runtime count variables |
| `main/default_config.h` | **Create** | `extern const char DEFAULT_CONFIG_JSON[]` declaration |
| `main/default_config.c` | **Create** | Embedded JSON string reproducing current 4-machine/3-tank/5-operator demo |
| `main/config_loader.h` | **Create** | `config_load()` API |
| `main/config_loader.c` | **Create** | SD card mount → cJSON parse → populate arrays → fallback |
| `main/sim_task.c` | Modify | Generic metric loop, dispatch by `sim_profile` |
| `main/mqtt_task.h` | Modify | Remove `mqtt_pub_tank_level`; keep rest |
| `main/mqtt_task.c` | Modify | Switch from `data_model.h` to `data.h`; generic topic format |
| `main/wifi_simple.h` | **Create** | `wifi_simple_init()` declaration |
| `main/ui/ui_machines.c` | Modify | `MAX_MACHINES` arrays; scrollable grid; `g_machine_count` loops; 1500 ms refresh timer |
| `main/ui/ui_tanks.c` | Modify | `MAX_TANKS` arrays; dynamic `col_w`; `g_tank_count` loops; call `mqtt_pub_tank_metric` |
| `main/main.c` | Modify | Call `config_load()`, `wifi_simple_init()`, `mqtt_task_start()`, `sim_task_start()` |
| `main/CMakeLists.txt` | Modify | Add new sources + `fatfs sdmmc json nvs_flash esp_wifi esp_netif` |

---

## Task 1: Update `data.h`

**Files:** Modify `main/ui/data.h`

- [ ] **Open `main/ui/data.h`**. At the top (after includes, before the first typedef), add the `SimProfile` enum:

```c
typedef enum { SIM_RANDOM_WALK = 0, SIM_SINE, SIM_RAMP } SimProfile;
```

- [ ] **Replace the `Metric` typedef** (lines 22–40) with this expanded version that uses fixed `char[]` fields for key/label/unit and adds sim fields:

```c
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
    float      sim_amplitude;   // sine only
    float      sim_period_s;    // sine only
    float      sim_offset;      // sine only
    float      sim_phase_acc;   // sine internal state — zeroed at load, not in JSON
} Metric;
```

- [ ] **Replace the `Machine` typedef** with `char[]` ids and increased limits:

```c
#define MAX_MACHINES     8
#define MAX_TANKS        3
#define MAX_BADGES       12
#define MAX_MACH_METRICS 8
#define MAX_TANK_METRICS 8
#define MAX_CERTS        6

typedef struct {
    char  id[16];
    char  name[32];
    char  type[48];
    bool  running;
    int   pulses;
    Metric metrics[MAX_MACH_METRICS];
    int    metric_count;
} Machine;
```

- [ ] **Replace the `Tank` typedef** (remove `topic` field, keep `phase`/`slosh`):

```c
typedef struct {
    char  id[16];
    char  name[32];
    Metric metrics[MAX_TANK_METRICS];
    int    metric_count;
    float  phase;
    float  slosh;
} Tank;
```

- [ ] **Replace the `Badge` typedef** with `char[]` cert strings:

```c
typedef struct {
    char     id[16];
    char     name[48];
    char     role[48];
    char     role_key[16];
    char     dept[32];
    char     uid[24];
    char     certs[MAX_CERTS][32];
    int      cert_count;
    uint32_t avatar_color;
} Badge;
```

- [ ] **Replace the `Global data` section** — change fixed-count defines to use `MAX_*`, add runtime count externs, add `metric_push`:

```c
extern Machine g_machines[MAX_MACHINES];
extern Tank    g_tanks[MAX_TANKS];
extern Badge   g_badges[MAX_BADGES];
extern int     g_machine_count;
extern int     g_tank_count;
extern int     g_badge_count;
extern int     g_active_badge;
extern int     g_log_sec;
extern bool    g_mqtt_connected;
extern int     g_mqtt_msg_count;
extern int     g_wifi_rssi;
extern char    g_device_ip[16];

void metric_push(Metric *m, float value);
```

- [ ] **Delete the old `#define MACHINE_COUNT 4`, `TANK_COUNT 3`, `BADGE_COUNT 5`** lines — they are replaced by the `MAX_*` defines above.

- [ ] **Build to confirm** the header compiles:

```bash
cd /path/to/DM_MQTT/firmware && idf.py build 2>&1 | grep -E "error:|warning:" | head -30
```

Expected: errors in `data.c` and UI files (we haven't updated them yet) — that's fine. No errors inside `data.h` itself.

- [ ] **Commit:**

```bash
git add main/ui/data.h
git commit -m "refactor(data): generic Metric/Machine/Tank/Badge structs with MAX_* limits and SimProfile"
```

---

## Task 2: Update `data.c`

**Files:** Modify `main/ui/data.c`

- [ ] **Replace the entire file** with this version that removes all hardcoded init (config_loader will populate arrays) and adds `metric_push`:

```c
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

// ─── Helpers (UI code reads these directly) ───────────────────────────────────
static inline float tank_level_val(const Tank *t) { return t->metrics[0].value; }

static inline uint32_t tank_fill_color(float pct) {
    if (pct < 15.0f) return 0xBB0000;
    if (pct < 40.0f) return 0xE76500;
    if (pct < 75.0f) return 0x0070F2;
    return 0x0092D1;
}

static inline uint32_t metric_color(const Metric *m) {
    for (int i = 0; i < m->thr_count; i++)
        if (m->value <= m->thr[i].limit) return m->thr[i].color;
    if (m->thr_count > 0) return 0xBB0000;
    return m->color ? m->color : 0x0070F2;
}
```

Note: `tank_level_val` replaces `tank_level` (same logic). Check if any UI file calls `tank_level()` and update the call to `tank_level_val()` — or just rename back to `tank_level` if you prefer.

- [ ] **Build:**

```bash
idf.py build 2>&1 | grep -E "error:" | head -30
```

Expected: errors in UI files that still use `MACHINE_COUNT`, `TANK_COUNT`, `BADGE_COUNT` as constants in loops — those get fixed in later tasks.

- [ ] **Commit:**

```bash
git add main/ui/data.c
git commit -m "refactor(data): remove hardcoded init, add metric_push, runtime count globals"
```

---

## Task 3: Create `default_config.h` + `default_config.c`

**Files:** Create `main/default_config.h`, create `main/default_config.c`

- [ ] **Create `main/default_config.h`:**

```c
#pragma once
extern const char DEFAULT_CONFIG_JSON[];
```

- [ ] **Create `main/default_config.c`** with the embedded JSON that reproduces the current demo exactly. This is the fallback when no SD card is present:

```c
#include "default_config.h"

const char DEFAULT_CONFIG_JSON[] =
"{"
"\"plant_name\":\"Plant BE01\","
"\"machines\":["
  "{"
    "\"id\":\"cnc01\",\"name\":\"CNC-01\",\"type\":\"CNC Machining\","
    "\"running\":true,\"pulses\":347,"
    "\"metrics\":["
      "{\"key\":\"rpm\",\"label\":\"Spindle RPM\",\"unit\":\"RPM\","
       "\"viz\":\"spark\",\"input\":\"step\",\"color\":\"0x0070F2\","
       "\"init_val\":4850,\"min_val\":0,\"max_val\":8000,\"step\":50,"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":2000,\"rand_max\":7000}},"
      "{\"key\":\"spindleLoad\",\"label\":\"Spindle Load\",\"unit\":\"%\","
       "\"viz\":\"arc\",\"input\":\"slider\","
       "\"init_val\":62,\"min_val\":0,\"max_val\":100,\"step\":1,"
       "\"thresholds\":[{\"limit\":40,\"color\":\"0x107E3E\"},"
                        "{\"limit\":70,\"color\":\"0xE76500\"},"
                        "{\"limit\":100,\"color\":\"0xBB0000\"}],"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":20,\"rand_max\":95}},"
      "{\"key\":\"toolWear\",\"label\":\"Tool Wear\",\"unit\":\"%\","
       "\"viz\":\"arc\",\"input\":\"slider\","
       "\"init_val\":34,\"min_val\":0,\"max_val\":100,\"step\":1,"
       "\"thresholds\":[{\"limit\":40,\"color\":\"0x107E3E\"},"
                        "{\"limit\":70,\"color\":\"0xE76500\"},"
                        "{\"limit\":100,\"color\":\"0xBB0000\"}],"
       "\"sim\":{\"profile\":\"ramp\"}},"
      "{\"key\":\"feedRate\",\"label\":\"Feed Rate\",\"unit\":\"mm/m\","
       "\"viz\":\"bullet\",\"input\":\"step\",\"color\":\"0x0070F2\","
       "\"init_val\":380,\"min_val\":0,\"max_val\":600,\"step\":10,"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":200,\"rand_max\":550}},"
      "{\"key\":\"machTemp\",\"label\":\"Temperature\",\"unit\":\"\\u00b0C\","
       "\"viz\":\"thermo\",\"input\":\"step\","
       "\"init_val\":41.2,\"min_val\":15,\"max_val\":80,\"step\":0.5,"
       "\"sim\":{\"profile\":\"sine\",\"amplitude\":3.0,\"period_s\":120,\"offset\":41.2}}"
    "]"
  "},"
  "{"
    "\"id\":\"cnc02\",\"name\":\"CNC-02\",\"type\":\"CNC Machining\","
    "\"running\":false,\"pulses\":210,"
    "\"metrics\":["
      "{\"key\":\"rpm\",\"label\":\"Spindle RPM\",\"unit\":\"RPM\","
       "\"viz\":\"spark\",\"input\":\"step\",\"color\":\"0x0070F2\","
       "\"init_val\":0,\"min_val\":0,\"max_val\":8000,\"step\":50,"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":2000,\"rand_max\":7000}},"
      "{\"key\":\"spindleLoad\",\"label\":\"Spindle Load\",\"unit\":\"%\","
       "\"viz\":\"arc\",\"input\":\"slider\","
       "\"init_val\":0,\"min_val\":0,\"max_val\":100,\"step\":1,"
       "\"thresholds\":[{\"limit\":40,\"color\":\"0x107E3E\"},"
                        "{\"limit\":70,\"color\":\"0xE76500\"},"
                        "{\"limit\":100,\"color\":\"0xBB0000\"}],"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":20,\"rand_max\":95}},"
      "{\"key\":\"toolWear\",\"label\":\"Tool Wear\",\"unit\":\"%\","
       "\"viz\":\"arc\",\"input\":\"slider\","
       "\"init_val\":71,\"min_val\":0,\"max_val\":100,\"step\":1,"
       "\"thresholds\":[{\"limit\":40,\"color\":\"0x107E3E\"},"
                        "{\"limit\":70,\"color\":\"0xE76500\"},"
                        "{\"limit\":100,\"color\":\"0xBB0000\"}],"
       "\"sim\":{\"profile\":\"ramp\"}},"
      "{\"key\":\"feedRate\",\"label\":\"Feed Rate\",\"unit\":\"mm/m\","
       "\"viz\":\"bullet\",\"input\":\"step\",\"color\":\"0x0070F2\","
       "\"init_val\":0,\"min_val\":0,\"max_val\":600,\"step\":10,"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":200,\"rand_max\":550}},"
      "{\"key\":\"machTemp\",\"label\":\"Temperature\",\"unit\":\"\\u00b0C\","
       "\"viz\":\"thermo\",\"input\":\"step\","
       "\"init_val\":28.3,\"min_val\":15,\"max_val\":80,\"step\":0.5,"
       "\"sim\":{\"profile\":\"sine\",\"amplitude\":2.0,\"period_s\":120,\"offset\":28.3}}"
    "]"
  "},"
  "{"
    "\"id\":\"fill01\",\"name\":\"FILL-01\",\"type\":\"Filling Line\","
    "\"running\":true,\"pulses\":418,"
    "\"metrics\":["
      "{\"key\":\"flowRate\",\"label\":\"Flow Rate\",\"unit\":\"L/min\","
       "\"viz\":\"spark\",\"input\":\"step\",\"color\":\"0x0070F2\","
       "\"init_val\":12.4,\"min_val\":0,\"max_val\":25,\"step\":0.5,"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":8,\"rand_max\":18}},"
      "{\"key\":\"fillPres\",\"label\":\"Fill Pressure\",\"unit\":\"bar\","
       "\"viz\":\"arc\",\"input\":\"step\","
       "\"init_val\":2.1,\"min_val\":0,\"max_val\":5,\"step\":0.05,"
       "\"thresholds\":[{\"limit\":3.5,\"color\":\"0x107E3E\"},"
                        "{\"limit\":4.5,\"color\":\"0xE76500\"},"
                        "{\"limit\":5.0,\"color\":\"0xBB0000\"}],"
       "\"sim\":{\"profile\":\"sine\",\"amplitude\":0.3,\"period_s\":30,\"offset\":2.1}},"
      "{\"key\":\"machTemp\",\"label\":\"Temperature\",\"unit\":\"\\u00b0C\","
       "\"viz\":\"thermo\",\"input\":\"step\","
       "\"init_val\":36.8,\"min_val\":15,\"max_val\":80,\"step\":0.2,"
       "\"sim\":{\"profile\":\"sine\",\"amplitude\":2.0,\"period_s\":90,\"offset\":36.8}}"
    "]"
  "},"
  "{"
    "\"id\":\"pack01\",\"name\":\"PACK-01\",\"type\":\"Packaging\","
    "\"running\":true,\"pulses\":309,"
    "\"metrics\":["
      "{\"key\":\"lineSpeed\",\"label\":\"Line Speed\",\"unit\":\"ppm\","
       "\"viz\":\"spark\",\"input\":\"step\",\"color\":\"0x0070F2\","
       "\"init_val\":48,\"min_val\":0,\"max_val\":80,\"step\":2,"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":40,\"rand_max\":65}},"
      "{\"key\":\"oee\",\"label\":\"OEE\",\"unit\":\"%\","
       "\"viz\":\"arc\",\"input\":\"slider\","
       "\"init_val\":91,\"min_val\":0,\"max_val\":100,\"step\":0.5,"
       "\"thresholds\":[{\"limit\":60,\"color\":\"0xBB0000\"},"
                        "{\"limit\":80,\"color\":\"0xE76500\"},"
                        "{\"limit\":100,\"color\":\"0x107E3E\"}],"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":85,\"rand_max\":97}},"
      "{\"key\":\"machTemp\",\"label\":\"Temperature\",\"unit\":\"\\u00b0C\","
       "\"viz\":\"thermo\",\"input\":\"step\","
       "\"init_val\":33.5,\"min_val\":15,\"max_val\":80,\"step\":0.2,"
       "\"sim\":{\"profile\":\"sine\",\"amplitude\":2.0,\"period_s\":90,\"offset\":33.5}}"
    "]"
  "}"
"],"
"\"tanks\":["
  "{"
    "\"id\":\"raw\",\"name\":\"Raw Material\","
    "\"metrics\":["
      "{\"key\":\"level\",\"label\":\"Fill Level\",\"unit\":\"%\","
       "\"viz\":\"tank\",\"input\":\"slider\","
       "\"init_val\":68,\"min_val\":0,\"max_val\":100,\"step\":1,"
       "\"thresholds\":[{\"limit\":15,\"color\":\"0xBB0000\"},"
                        "{\"limit\":40,\"color\":\"0xE76500\"}],"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":60,\"rand_max\":80}},"
      "{\"key\":\"temperature\",\"label\":\"Temperature\",\"unit\":\"\\u00b0C\","
       "\"viz\":\"thermo\",\"input\":\"step\","
       "\"init_val\":22.4,\"min_val\":5,\"max_val\":70,\"step\":0.5,"
       "\"sim\":{\"profile\":\"sine\",\"amplitude\":2.0,\"period_s\":60,\"offset\":22.4}},"
      "{\"key\":\"flowIn\",\"label\":\"Flow In\",\"unit\":\"L/min\","
       "\"viz\":\"spark\",\"input\":\"step\",\"color\":\"0x0070F2\","
       "\"init_val\":4.2,\"min_val\":0,\"max_val\":20,\"step\":0.5,"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":2,\"rand_max\":10}}"
    "]"
  "},"
  "{"
    "\"id\":\"prod\",\"name\":\"Production\","
    "\"metrics\":["
      "{\"key\":\"level\",\"label\":\"Fill Level\",\"unit\":\"%\","
       "\"viz\":\"tank\",\"input\":\"slider\","
       "\"init_val\":28,\"min_val\":0,\"max_val\":100,\"step\":1,"
       "\"thresholds\":[{\"limit\":15,\"color\":\"0xBB0000\"},"
                        "{\"limit\":40,\"color\":\"0xE76500\"}],"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":20,\"rand_max\":40}},"
      "{\"key\":\"temperature\",\"label\":\"Temperature\",\"unit\":\"\\u00b0C\","
       "\"viz\":\"thermo\",\"input\":\"step\","
       "\"init_val\":38.1,\"min_val\":5,\"max_val\":70,\"step\":0.5,"
       "\"sim\":{\"profile\":\"sine\",\"amplitude\":3.0,\"period_s\":45,\"offset\":38.1}},"
      "{\"key\":\"pressure\",\"label\":\"Pressure\",\"unit\":\"bar\","
       "\"viz\":\"arc\",\"input\":\"step\","
       "\"init_val\":1.4,\"min_val\":0,\"max_val\":5,\"step\":0.05,"
       "\"thresholds\":[{\"limit\":3.0,\"color\":\"0x107E3E\"},"
                        "{\"limit\":4.0,\"color\":\"0xE76500\"},"
                        "{\"limit\":5.0,\"color\":\"0xBB0000\"}],"
       "\"sim\":{\"profile\":\"sine\",\"amplitude\":0.2,\"period_s\":20,\"offset\":1.4}}"
    "]"
  "},"
  "{"
    "\"id\":\"waste\",\"name\":\"Waste\","
    "\"metrics\":["
      "{\"key\":\"level\",\"label\":\"Fill Level\",\"unit\":\"%\","
       "\"viz\":\"tank\",\"input\":\"slider\","
       "\"init_val\":11,\"min_val\":0,\"max_val\":100,\"step\":1,"
       "\"thresholds\":[{\"limit\":15,\"color\":\"0xBB0000\"},"
                        "{\"limit\":40,\"color\":\"0xE76500\"}],"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":5,\"rand_max\":25}},"
      "{\"key\":\"temperature\",\"label\":\"Temperature\",\"unit\":\"\\u00b0C\","
       "\"viz\":\"thermo\",\"input\":\"step\","
       "\"init_val\":18.6,\"min_val\":5,\"max_val\":70,\"step\":0.5,"
       "\"sim\":{\"profile\":\"sine\",\"amplitude\":1.5,\"period_s\":90,\"offset\":18.6}},"
      "{\"key\":\"ph\",\"label\":\"pH\",\"unit\":\"pH\","
       "\"viz\":\"ph\",\"input\":\"step\","
       "\"init_val\":5.2,\"min_val\":0,\"max_val\":14,\"step\":0.1,"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":4.0,\"rand_max\":7.0}},"
      "{\"key\":\"flowOut\",\"label\":\"Flow Out\",\"unit\":\"L/min\","
       "\"viz\":\"spark\",\"input\":\"step\",\"color\":\"0x0070F2\","
       "\"init_val\":1.1,\"min_val\":0,\"max_val\":20,\"step\":0.5,"
       "\"sim\":{\"profile\":\"random_walk\",\"rand_min\":0.5,\"rand_max\":5}}"
    "]"
  "}"
"],"
"\"operators\":["
  "{"
    "\"id\":\"EMP-0042\",\"name\":\"Demo Operator\","
    "\"role\":\"Shift Lead\",\"role_key\":\"lead\","
    "\"dept\":\"Production\",\"uid\":\"A3:F7:C2:91\","
    "\"avatar_color\":\"0x0050BE\","
    "\"certs\":[\"ISO 9001\",\"Lean\",\"PLC Certified\"]"
  "},"
  "{"
    "\"id\":\"EMP-0117\",\"name\":\"Jana Kovacevic\","
    "\"role\":\"CNC Operator\",\"role_key\":\"operator\","
    "\"dept\":\"Machining\",\"uid\":\"B1:2E:9A:04\","
    "\"avatar_color\":\"0x0070F2\","
    "\"certs\":[\"CNC Level 3\",\"Safety\"]"
  "},"
  "{"
    "\"id\":\"EMP-0089\",\"name\":\"Lars Vermeulen\","
    "\"role\":\"Process Engineer\",\"role_key\":\"engineer\","
    "\"dept\":\"Engineering\",\"uid\":\"C4:88:F1:3D\","
    "\"avatar_color\":\"0xB84D00\","
    "\"certs\":[\"Six Sigma GB\",\"HAZOP\",\"ISO 14001\"]"
  "},"
  "{"
    "\"id\":\"EMP-0201\",\"name\":\"Amira El-Sayed\","
    "\"role\":\"Quality Inspector\",\"role_key\":\"quality\","
    "\"dept\":\"Quality\",\"uid\":\"D7:A3:55:E9\","
    "\"avatar_color\":\"0x8A0000\","
    "\"certs\":[\"ISO 9001 Lead\",\"FMEA\",\"8D\"]"
  "},"
  "{"
    "\"id\":\"EMP-0033\",\"name\":\"Pieter van Dijk\","
    "\"role\":\"Production Supervisor\",\"role_key\":\"supervisor\","
    "\"dept\":\"Operations\",\"uid\":\"E2:CF:71:B8\","
    "\"avatar_color\":\"0x4A1A8C\","
    "\"certs\":[\"MES Certified\",\"Lean Black\",\"IFS\"]"
  "}"
"]"
"}";
```

- [ ] **Build to confirm no syntax errors:**

```bash
idf.py build 2>&1 | grep -E "error:" | grep -i "default_config" | head -10
```

Expected: no errors from these files.

- [ ] **Commit:**

```bash
git add main/default_config.h main/default_config.c
git commit -m "feat(config): add embedded default JSON config (current demo as fallback)"
```

---

## Task 4: Create `config_loader.h` + `config_loader.c`

**Files:** Create `main/config_loader.h`, create `main/config_loader.c`

- [ ] **Create `main/config_loader.h`:**

```c
#pragma once

typedef enum {
    CONFIG_OK,
    CONFIG_SD_NOT_FOUND,
    CONFIG_FILE_NOT_FOUND,
    CONFIG_PARSE_ERROR,
    CONFIG_USING_DEFAULT,
} ConfigResult;

ConfigResult    config_load(void);
const char     *config_result_str(ConfigResult r);
```

- [ ] **Create `main/config_loader.c`:**

```c
#include "config_loader.h"
#include "default_config.h"
#include "ui/data.h"
#include "config.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cfg";

#define JSON_BUF_SIZE (32 * 1024)

const char *config_result_str(ConfigResult r) {
    switch (r) {
    case CONFIG_OK:             return "SD card loaded";
    case CONFIG_SD_NOT_FOUND:   return "SD card not found — using default";
    case CONFIG_FILE_NOT_FOUND: return "sapdm_config.json not found — using default";
    case CONFIG_PARSE_ERROR:    return "JSON parse error — using default";
    case CONFIG_USING_DEFAULT:  return "using embedded default";
    }
    return "unknown";
}

// ─── Parse helpers ────────────────────────────────────────────────────────────

static void parse_metric(const cJSON *j, Metric *m) {
    memset(m, 0, sizeof(Metric));

    cJSON *v;
    #define STR(field, dest, size) \
        if ((v = cJSON_GetObjectItem(j, field)) && cJSON_IsString(v)) \
            strncpy(dest, v->valuestring, size - 1)
    #define NUM(field, dest) \
        if ((v = cJSON_GetObjectItem(j, field)) && cJSON_IsNumber(v)) \
            dest = (float)v->valuedouble

    STR("key",   m->key,   sizeof(m->key));
    STR("label", m->label, sizeof(m->label));
    STR("unit",  m->unit,  sizeof(m->unit));

    if ((v = cJSON_GetObjectItem(j, "viz")) && cJSON_IsString(v)) {
        if      (!strcmp(v->valuestring, "spark"))  m->viz = VIZ_SPARK;
        else if (!strcmp(v->valuestring, "arc"))    m->viz = VIZ_ARC;
        else if (!strcmp(v->valuestring, "thermo")) m->viz = VIZ_THERMO;
        else if (!strcmp(v->valuestring, "ph"))     m->viz = VIZ_PH;
        else if (!strcmp(v->valuestring, "tank"))   m->viz = VIZ_TANK;
        else if (!strcmp(v->valuestring, "bullet")) m->viz = VIZ_BULLET;
    }
    if ((v = cJSON_GetObjectItem(j, "input")) && cJSON_IsString(v))
        m->input = !strcmp(v->valuestring, "slider") ? INPUT_SLIDER : INPUT_STEP;
    if ((v = cJSON_GetObjectItem(j, "color")) && cJSON_IsString(v))
        m->color = (uint32_t)strtoul(v->valuestring, NULL, 0);

    NUM("init_val", m->init_val);
    NUM("min_val",  m->min_val);
    NUM("max_val",  m->max_val);
    NUM("step",     m->step);
    m->value = m->init_val;

    const cJSON *thrs = cJSON_GetObjectItem(j, "thresholds");
    if (cJSON_IsArray(thrs)) {
        int n = cJSON_GetArraySize(thrs);
        m->thr_count = n < 3 ? n : 3;
        for (int i = 0; i < m->thr_count; i++) {
            const cJSON *t = cJSON_GetArrayItem(thrs, i);
            if ((v = cJSON_GetObjectItem(t, "limit")) && cJSON_IsNumber(v))
                m->thr[i].limit = (float)v->valuedouble;
            if ((v = cJSON_GetObjectItem(t, "color")) && cJSON_IsString(v))
                m->thr[i].color = (uint32_t)strtoul(v->valuestring, NULL, 0);
        }
    }

    const cJSON *sim = cJSON_GetObjectItem(j, "sim");
    if (cJSON_IsObject(sim)) {
        if ((v = cJSON_GetObjectItem(sim, "profile")) && cJSON_IsString(v)) {
            if      (!strcmp(v->valuestring, "sine")) m->sim_profile = SIM_SINE;
            else if (!strcmp(v->valuestring, "ramp")) m->sim_profile = SIM_RAMP;
            else                                       m->sim_profile = SIM_RANDOM_WALK;
        }
        NUM("rand_min",  m->rand_min);
        NUM("rand_max",  m->rand_max);
        if ((v = cJSON_GetObjectItem(sim, "amplitude")) && cJSON_IsNumber(v))
            m->sim_amplitude = (float)v->valuedouble;
        if ((v = cJSON_GetObjectItem(sim, "period_s")) && cJSON_IsNumber(v))
            m->sim_period_s = (float)v->valuedouble;
        if ((v = cJSON_GetObjectItem(sim, "offset")) && cJSON_IsNumber(v))
            m->sim_offset = (float)v->valuedouble;
    }
    #undef STR
    #undef NUM

    // Pre-fill history
    for (int i = 0; i < HISTORY_LEN; i++) m->history[i] = m->init_val;
    m->history_len = (m->viz == VIZ_SPARK) ? HISTORY_LEN : 0;
}

static void populate_from_json(const cJSON *root) {
    const cJSON *arr, *item;

    arr = cJSON_GetObjectItem(root, "machines");
    g_machine_count = 0;
    cJSON_ArrayForEach(item, arr) {
        if (g_machine_count >= MAX_MACHINES) break;
        Machine *m = &g_machines[g_machine_count];
        memset(m, 0, sizeof(Machine));
        const cJSON *v;
        if ((v = cJSON_GetObjectItem(item, "id"))   && cJSON_IsString(v)) strncpy(m->id,   v->valuestring, sizeof(m->id)  - 1);
        if ((v = cJSON_GetObjectItem(item, "name")) && cJSON_IsString(v)) strncpy(m->name, v->valuestring, sizeof(m->name)- 1);
        if ((v = cJSON_GetObjectItem(item, "type")) && cJSON_IsString(v)) strncpy(m->type, v->valuestring, sizeof(m->type)- 1);
        if ((v = cJSON_GetObjectItem(item, "running")) && cJSON_IsBool(v))   m->running = cJSON_IsTrue(v);
        if ((v = cJSON_GetObjectItem(item, "pulses"))  && cJSON_IsNumber(v)) m->pulses  = v->valueint;
        const cJSON *metrics = cJSON_GetObjectItem(item, "metrics");
        m->metric_count = 0;
        const cJSON *met;
        cJSON_ArrayForEach(met, metrics) {
            if (m->metric_count >= MAX_MACH_METRICS) break;
            parse_metric(met, &m->metrics[m->metric_count++]);
        }
        g_machine_count++;
    }

    arr = cJSON_GetObjectItem(root, "tanks");
    g_tank_count = 0;
    cJSON_ArrayForEach(item, arr) {
        if (g_tank_count >= MAX_TANKS) break;
        Tank *t = &g_tanks[g_tank_count];
        memset(t, 0, sizeof(Tank));
        const cJSON *v;
        if ((v = cJSON_GetObjectItem(item, "id"))   && cJSON_IsString(v)) strncpy(t->id,   v->valuestring, sizeof(t->id)  - 1);
        if ((v = cJSON_GetObjectItem(item, "name")) && cJSON_IsString(v)) strncpy(t->name, v->valuestring, sizeof(t->name)- 1);
        const cJSON *metrics = cJSON_GetObjectItem(item, "metrics");
        t->metric_count = 0;
        const cJSON *met;
        cJSON_ArrayForEach(met, metrics) {
            if (t->metric_count >= MAX_TANK_METRICS) break;
            parse_metric(met, &t->metrics[t->metric_count++]);
        }
        g_tank_count++;
    }

    arr = cJSON_GetObjectItem(root, "operators");
    g_badge_count = 0;
    cJSON_ArrayForEach(item, arr) {
        if (g_badge_count >= MAX_BADGES) break;
        Badge *b = &g_badges[g_badge_count];
        memset(b, 0, sizeof(Badge));
        const cJSON *v;
        #define BS(field, dest, size) \
            if ((v = cJSON_GetObjectItem(item, field)) && cJSON_IsString(v)) \
                strncpy(dest, v->valuestring, size - 1)
        BS("id",       b->id,       sizeof(b->id));
        BS("name",     b->name,     sizeof(b->name));
        BS("role",     b->role,     sizeof(b->role));
        BS("role_key", b->role_key, sizeof(b->role_key));
        BS("dept",     b->dept,     sizeof(b->dept));
        BS("uid",      b->uid,      sizeof(b->uid));
        #undef BS
        if ((v = cJSON_GetObjectItem(item, "avatar_color")) && cJSON_IsString(v))
            b->avatar_color = (uint32_t)strtoul(v->valuestring, NULL, 0);
        const cJSON *certs = cJSON_GetObjectItem(item, "certs");
        b->cert_count = 0;
        const cJSON *cert;
        cJSON_ArrayForEach(cert, certs) {
            if (b->cert_count >= MAX_CERTS) break;
            if (cJSON_IsString(cert))
                strncpy(b->certs[b->cert_count++], cert->valuestring, 31);
        }
        g_badge_count++;
    }

    ESP_LOGI(TAG, "loaded: %d machines, %d tanks, %d operators",
             g_machine_count, g_tank_count, g_badge_count);
}

static ConfigResult parse_and_populate(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "cJSON parse failed: %s", cJSON_GetErrorPtr());
        return CONFIG_PARSE_ERROR;
    }
    populate_from_json(root);
    cJSON_Delete(root);
    return CONFIG_OK;
}

// ─── Public: config_load ─────────────────────────────────────────────────────

ConfigResult config_load(void) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk   = GPIO_SD_CLK;
    slot.cmd   = GPIO_SD_CMD;
    slot.d0    = GPIO_SD_D0;
    slot.d1    = GPIO_SD_D1;
    slot.d2    = GPIO_SD_D2;
    slot.d3    = GPIO_SD_D3;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount_cfg, &card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed (%s) — using default config", esp_err_to_name(err));
        parse_and_populate(DEFAULT_CONFIG_JSON);
        return CONFIG_SD_NOT_FOUND;
    }

    char *buf = malloc(JSON_BUF_SIZE);
    if (!buf) {
        esp_vfs_fat_sdmmc_unmount("/sdcard", card);
        parse_and_populate(DEFAULT_CONFIG_JSON);
        return CONFIG_PARSE_ERROR;
    }

    FILE *f = fopen("/sdcard/sapdm_config.json", "r");
    if (!f) {
        ESP_LOGW(TAG, "sapdm_config.json not found — using default config");
        free(buf);
        esp_vfs_fat_sdmmc_unmount("/sdcard", card);
        parse_and_populate(DEFAULT_CONFIG_JSON);
        return CONFIG_FILE_NOT_FOUND;
    }

    size_t n = fread(buf, 1, JSON_BUF_SIZE - 1, f);
    buf[n] = '\0';
    fclose(f);
    esp_vfs_fat_sdmmc_unmount("/sdcard", card);

    ConfigResult r = parse_and_populate(buf);
    free(buf);
    if (r != CONFIG_OK) {
        parse_and_populate(DEFAULT_CONFIG_JSON);
        return CONFIG_USING_DEFAULT;
    }
    return CONFIG_OK;
}
```

- [ ] **Build:**

```bash
idf.py build 2>&1 | grep -E "error:" | grep -i "config_loader\|default_config" | head -20
```

Expected: no errors from these two files (other files still have `MACHINE_COUNT` errors — those are fixed in later tasks).

- [ ] **Commit:**

```bash
git add main/config_loader.h main/config_loader.c
git commit -m "feat(config): config_loader — SD card JSON with embedded fallback"
```

---

## Task 5: Refactor `sim_task.c`

**Files:** Modify `main/sim_task.c`

- [ ] **Replace the entire file** with the generic sim loop:

```c
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
    }
    metric_push(m, next);
}

static int s_pulse_tick[MAX_MACHINES];

static void sim_tick(void) {
    for (int i = 0; i < g_machine_count; i++) {
        Machine *mc = &g_machines[i];
        if (!mc->running) continue;
        for (int k = 0; k < mc->metric_count; k++)
            sim_metric(&mc->metrics[k]);
        if (++s_pulse_tick[i] >= 5) {
            s_pulse_tick[i] = 0;
            mc->pulses++;
            mqtt_pub_machine_pulse(mc->id, mc->pulses);
        }
    }
    for (int i = 0; i < g_tank_count; i++) {
        Tank *t = &g_tanks[i];
        for (int k = 0; k < t->metric_count; k++)
            sim_metric(&t->metrics[k]);
    }
}

static void publish_all(void) {
    for (int i = 0; i < g_machine_count; i++) {
        Machine *mc = &g_machines[i];
        mqtt_pub_machine_status(mc->id, mc->running);
        for (int k = 0; k < mc->metric_count; k++)
            mqtt_pub_machine_metric(mc->id, mc->metrics[k].key, mc->metrics[k].value);
    }
    for (int i = 0; i < g_tank_count; i++) {
        Tank *t = &g_tanks[i];
        for (int k = 0; k < t->metric_count; k++)
            mqtt_pub_tank_metric(t->id, t->metrics[k].key, t->metrics[k].value);
    }
}

static void sim_task_fn(void *arg) {
    memset(s_pulse_tick, 0, sizeof(s_pulse_tick));
    ESP_LOGI(TAG, "started — %d machines, %d tanks", g_machine_count, g_tank_count);
    while (1) {
        sim_tick();
        if (mqtt_is_connected()) publish_all();
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void sim_task_start(void) {
    xTaskCreatePinnedToCore(sim_task_fn, "sim", 8192, NULL, 5, NULL, 1);
}
```

- [ ] **Check `main/sim_task.h`** — it should already declare `void sim_task_start(void)`. If it doesn't, replace it with:

```c
#pragma once
void sim_task_start(void);
```

- [ ] **Build:**

```bash
idf.py build 2>&1 | grep -E "error:" | grep -i "sim_task" | head -10
```

Expected: no errors from sim_task.c (MACHINE_COUNT errors in UI files are still unfixed).

- [ ] **Commit:**

```bash
git add main/sim_task.c main/sim_task.h
git commit -m "feat(sim): generic sim task — random_walk, sine, ramp profiles"
```

---

## Task 6: Update `mqtt_task.h` + `mqtt_task.c`

**Files:** Modify `main/mqtt_task.h`, modify `main/mqtt_task.c`

- [ ] **Replace `main/mqtt_task.h`** — remove `mqtt_pub_tank_level` (level is now just a metric):

```c
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
```

- [ ] **Replace `main/mqtt_task.c`** — fix `#include "data_model.h"` → `#include "ui/data.h"`, rewrite publish functions with generic topic format:

```c
#include "mqtt_task.h"
#include "ui/data.h"
#include "config.h"
#include "ui/ui_main.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client = NULL;

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to %s", MQTT_BROKER_URI);
        g_mqtt_connected = true;
        g_mqtt_msg_count = 0;
        ui_notify_mqtt_status(true);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected — will retry");
        g_mqtt_connected = false;
        ui_notify_mqtt_status(false);
        break;
    case MQTT_EVENT_PUBLISHED:
        g_mqtt_msg_count++;
        break;
    case MQTT_EVENT_ERROR: {
        esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;
        ESP_LOGE(TAG, "error type=%d", ev->error_handle->error_type);
        break;
    }
    default: break;
    }
}

void mqtt_task_start(void) {
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri            = MQTT_BROKER_URI,
        .broker.address.port           = MQTT_BROKER_PORT,
        .credentials.client_id         = MQTT_CLIENT_ID,
        .session.keepalive             = MQTT_KEEPALIVE_S,
        .network.reconnect_timeout_ms  = 5000,
    };
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "MQTT client started → %s", MQTT_BROKER_URI);
}

void mqtt_task_stop(void) {
    if (!s_client) return;
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    g_mqtt_connected = false;
}

bool mqtt_is_connected(void) { return g_mqtt_connected; }

static void pub(const char *topic, const char *payload) {
    if (!g_mqtt_connected || !s_client) return;
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

void mqtt_pub_machine_status(const char *id, bool running) {
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/machines/%s/status", TOPIC_PREFIX, id);
    pub(topic, running ? "RUNNING" : "STOPPED");
}

void mqtt_pub_machine_pulse(const char *id, int count) {
    char topic[96], payload[16];
    snprintf(topic,   sizeof(topic),   "%s/machines/%s/pulses", TOPIC_PREFIX, id);
    snprintf(payload, sizeof(payload), "%d", count);
    pub(topic, payload);
}

void mqtt_pub_machine_metric(const char *id, const char *key, float value) {
    char topic[96], payload[24];
    snprintf(topic,   sizeof(topic),   "%s/machines/%s/%s", TOPIC_PREFIX, id, key);
    snprintf(payload, sizeof(payload), "%.2f", value);
    pub(topic, payload);
}

void mqtt_pub_tank_metric(const char *id, const char *key, float value) {
    char topic[96], payload[24];
    snprintf(topic,   sizeof(topic),   "%s/tanks/%s/%s", TOPIC_PREFIX, id, key);
    snprintf(payload, sizeof(payload), "%.2f", value);
    pub(topic, payload);
}

void mqtt_pub_operator(const char *uid, const char *name,
                       const char *role, const char *plant) {
    char topic[64], payload[256];
    snprintf(topic, sizeof(topic), "%s/nfc/badge", TOPIC_PREFIX);
    snprintf(payload, sizeof(payload),
             "{\"uid\":\"%s\",\"name\":\"%s\",\"role\":\"%s\",\"plant\":\"%s\"}",
             uid, name, role, plant);
    pub(topic, payload);
}
```

- [ ] **Build:**

```bash
idf.py build 2>&1 | grep -E "error:" | grep -i "mqtt_task" | head -10
```

Expected: no errors from mqtt_task files.

- [ ] **Commit:**

```bash
git add main/mqtt_task.h main/mqtt_task.c
git commit -m "feat(mqtt): generic publish with JSON-derived topic paths"
```

---

## Task 7: Update `ui_machines.c`

**Files:** Modify `main/ui/ui_machines.c`

Four targeted changes:

- [ ] **Change static array sizes** from `MACHINE_COUNT` to `MAX_MACHINES` (lines 54–59):

```c
static lv_obj_t *s_cards[MAX_MACHINES]       = {NULL};
static lv_obj_t *s_dots[MAX_MACHINES]        = {NULL};
static lv_obj_t *s_toggle_btns[MAX_MACHINES] = {NULL};
static lv_obj_t *s_toggle_lbls[MAX_MACHINES] = {NULL};
static lv_obj_t *s_charts[MAX_MACHINES]      = {NULL};
static lv_chart_series_t *s_series[MAX_MACHINES] = {NULL};
```

- [ ] **Make the screen scrollable and add refresh timer** in `ui_machines_create()`. Find these two lines (around line 548):

```c
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
```

Replace with:

```c
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_VER);
    lv_obj_set_style_scroll_snap_y(s_screen, LV_SCROLL_SNAP_NONE, 0);
```

- [ ] **Make the grid `LV_SIZE_CONTENT` height** so it grows with machine count. Find (around line 553):

```c
    lv_obj_set_size(grid, CONTENT_W - 2 * GRID_PAD, CONTENT_H - 2 * GRID_PAD);
```

Replace with:

```c
    lv_obj_set_size(grid, CONTENT_W - 2 * GRID_PAD, LV_SIZE_CONTENT);
```

Also find and remove:

```c
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
```

- [ ] **Change the card-build loop** from `MACHINE_COUNT` to `g_machine_count` (line 567):

```c
    for (int i = 0; i < g_machine_count; i++) {
        build_machine_card(grid, i);
    }
```

And add a periodic refresh timer immediately after the loop:

```c
    lv_timer_t *refresh_t = lv_timer_create(
        [](lv_timer_t *t){ (void)t; ui_machines_refresh(); },
        1500, NULL);
    (void)refresh_t;
```

Note: LVGL C API doesn't support lambdas. Create a static callback instead. Add this function before `ui_machines_create`:

```c
static void machines_refresh_cb(lv_timer_t *t) {
    (void)t;
    ui_machines_refresh();
}
```

Then the timer line becomes:

```c
    lv_timer_create(machines_refresh_cb, 1500, NULL);
```

- [ ] **Change all `MACHINE_COUNT` loops** in `ui_machines_refresh()` and `ui_machines_pulse_first()` to `g_machine_count`:

```c
// ui_machines_refresh() — line 578:
    for (int i = 0; i < g_machine_count; i++) {

// ui_machines_pulse_first() — line 621:
    for (int i = 0; i < g_machine_count; i++) {
```

- [ ] **Build:**

```bash
idf.py build 2>&1 | grep -E "error:" | grep -i "ui_machines" | head -10
```

- [ ] **Commit:**

```bash
git add main/ui/ui_machines.c
git commit -m "feat(ui): machines screen — dynamic count, scrollable grid, 1500ms refresh"
```

---

## Task 8: Update `ui_tanks.c`

**Files:** Modify `main/ui/ui_tanks.c`

- [ ] **Change static array sizes** from `TANK_COUNT` to `MAX_TANKS` (lines 37–44):

```c
static tank_col_t  s_cols[MAX_TANKS];
static int16_t     s_press_y[MAX_TANKS];
static float       s_press_pct[MAX_TANKS];
static bool        s_dragging[MAX_TANKS];
```

- [ ] **Find the column-width calculation** in the `ui_tanks_create()` function (it will be something like `col_w = (CONTENT_W - ...) / TANK_COUNT`). Replace it with a dynamic calculation using `g_tank_count`:

```c
    lv_coord_t col_w = (g_tank_count > 0)
        ? (CONTENT_W - COL_GAP * (g_tank_count + 1)) / g_tank_count
        : CONTENT_W - 2 * COL_GAP;
```

- [ ] **Change all loops** that iterate over `TANK_COUNT` to use `g_tank_count`. Search for every `i < TANK_COUNT` in the file and change to `i < g_tank_count`. Typical locations: column creation loop, refresh timer callback, drag callbacks.

```bash
grep -n "TANK_COUNT" /path/to/DM_MQTT/firmware/main/ui/ui_tanks.c
```

Change each occurrence to `g_tank_count` (except array size declarations — those were already handled).

- [ ] **Find any call to `mqtt_pub_tank_level()`** in ui_tanks.c (used during drag-to-adjust) and replace with `mqtt_pub_tank_metric()`:

```bash
grep -n "mqtt_pub_tank_level" /path/to/DM_MQTT/firmware/main/ui/ui_tanks.c
```

Replace each occurrence, e.g.:

```c
// Before:
mqtt_pub_tank_level(g_tanks[idx].id, pct);
// After:
mqtt_pub_tank_metric(g_tanks[idx].id, "level", pct);
```

- [ ] **Build:**

```bash
idf.py build 2>&1 | grep -E "error:" | grep -i "ui_tanks" | head -10
```

- [ ] **Commit:**

```bash
git add main/ui/ui_tanks.c
git commit -m "feat(ui): tanks screen — dynamic count, dynamic column width"
```

---

## Task 9: Wire Up `main.c` + `CMakeLists.txt`

**Files:** Create `main/wifi_simple.h`, modify `main/main.c`, modify `main/CMakeLists.txt`

- [ ] **Create `main/wifi_simple.h`** (the `.c` already exists, just needs a header):

```c
#pragma once
void wifi_simple_init(void);
```

- [ ] **Update `main/main.c`** — add includes and startup calls. After the existing includes add:

```c
#include "config_loader.h"
#include "wifi_simple.h"
#include "mqtt_task.h"
#include "sim_task.h"
```

In `app_main()`, add `config_load()` before the display init and the task starts after UI init:

```c
void app_main(void) {
    ESP_LOGI(TAG, "SAP DM MQTT Simulator - ESP32-P4");

    ConfigResult cr = config_load();
    ESP_LOGI(TAG, "Config: %s", config_result_str(cr));

    bsp_display_cfg_t cfg = { /* ... unchanged ... */ };
    // ... existing display setup unchanged ...

    bsp_display_lock(0);
    ui_main_init();
    bsp_display_unlock();

    wifi_simple_init();
    mqtt_task_start();
    sim_task_start();

    xTaskCreatePinnedToCore(btn_task,     "btn", 2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(console_task, "con", 8192, NULL, 3, NULL, 0);

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
```

- [ ] **Update `main/CMakeLists.txt`** — add all dormant source files and the required ESP-IDF components:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "config_loader.c"
        "default_config.c"
        "sim_task.c"
        "mqtt_task.c"
        "wifi_simple.c"
        "ui/ui_main.c"
        "ui/ui_home.c"
        "ui/ui_machines.c"
        "ui/ui_tanks.c"
        "ui/ui_debug.c"
        "ui/ui_nfc.c"
        "ui/ui_detail.c"
        "ui/ui_tank_overview.c"
        "ui/ui_screenshot.c"
        "ui/ui_wifi_setup.c"
        "ui/ui_mqtt_detail.c"
        "ui/data.c"
        "ui/assets/sap_logo.c"
        "ui/assets/tank_icon.c"
    INCLUDE_DIRS
        "."
        "ui"
        "ui/assets"
    REQUIRES
        waveshare__esp32_p4_wifi6_touch_lcd_4b
        espressif__esp_lvgl_port
        log
        freertos
        esp_timer
        mqtt
        fatfs
        sdmmc
        json
        nvs_flash
        esp_wifi
        esp_netif
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)
```

- [ ] **Fix any remaining `MACHINE_COUNT` / `TANK_COUNT` / `BADGE_COUNT` references** in other UI files (ui_nfc.c, ui_home.c, ui_detail.c etc.) by searching and replacing:

```bash
grep -rn "MACHINE_COUNT\|TANK_COUNT\|BADGE_COUNT" /path/to/DM_MQTT/firmware/main/ui/ --include="*.c" --include="*.h"
```

For each occurrence: replace `MACHINE_COUNT` → `g_machine_count`, `TANK_COUNT` → `g_tank_count`, `BADGE_COUNT` → `g_badge_count`. (These are runtime values now, not compile-time constants.)

- [ ] **Full build:**

```bash
idf.py build 2>&1 | tail -5
```

Expected: `Project build complete.` with binary sizes shown.

- [ ] **Commit:**

```bash
git add main/wifi_simple.h main/main.c main/CMakeLists.txt
git commit -m "feat: wire up config_load, wifi, mqtt, sim at boot — firmware is now functional"
```

---

## Task 10: Flash and Verify

**Files:** None (verification only)

- [ ] **Flash with no SD card inserted:**

```bash
idf.py flash monitor 2>&1 | head -60
```

Expected log output:
```
I (xxx) cfg: SD mount failed (ESP_ERR_INVALID_STATE) — using default config
I (xxx) cfg: loaded: 4 machines, 3 tanks, 5 operators
I (xxx) sim: started — 4 machines, 3 tanks
I (xxx) mqtt: MQTT client started → wss://mqtt.example.com
```

- [ ] **Visual check — no SD card:** Navigate to Machines tab. Verify 4 machine cards appear (2×2). Navigate to Tanks tab. Verify 3 tanks appear. Navigate to NFC tab. Verify 5 operator badges. MQTT and WiFi status pills in the shell bar should show connecting/connected state.

- [ ] **Take a screenshot:**

```bash
python3 tools/screenshot.py
```

- [ ] **Verify MQTT messages** using MQTT Explorer or Node-RED connected to `wss://mqtt.example.com`. Subscribe to `factory/#`. Expected: messages arriving every 1.5 s on topics like `factory/machines/cnc01/rpm`, `factory/tanks/raw/level`, etc.

- [ ] **Create a minimal test JSON** on your machine:

```json
{
  "plant_name": "Test Plant",
  "machines": [
    {
      "id": "test01", "name": "Test Machine", "type": "Test",
      "running": true, "pulses": 0,
      "metrics": [
        { "key": "speed", "label": "Speed", "unit": "rpm",
          "viz": "spark", "input": "step", "color": "0x0070F2",
          "init_val": 1000, "min_val": 0, "max_val": 2000, "step": 50,
          "sim": { "profile": "random_walk", "rand_min": 800, "rand_max": 1800 } }
      ]
    }
  ],
  "tanks": [
    {
      "id": "t01", "name": "Test Tank",
      "metrics": [
        { "key": "level", "label": "Fill Level", "unit": "%",
          "viz": "tank", "input": "slider",
          "init_val": 50, "min_val": 0, "max_val": 100, "step": 1,
          "thresholds": [{"limit": 15, "color": "0xBB0000"}],
          "sim": { "profile": "random_walk", "rand_min": 40, "rand_max": 60 } }
      ]
    }
  ],
  "operators": [
    { "id": "EMP-0001", "name": "Test User", "role": "Operator",
      "role_key": "operator", "dept": "Test", "uid": "AA:BB:CC:DD",
      "avatar_color": "0x0050BE", "certs": ["Test Cert"] }
  ]
}
```

Copy to SD card as `sapdm_config.json`. Insert card and reflash (or just reboot if the card was already mounted — power cycle the device).

- [ ] **Visual check — custom JSON:** Machines screen shows 1 card. Tanks screen shows 1 tank. MQTT shows only `factory/machines/test01/speed` and `factory/tanks/t01/level`.

- [ ] **Commit any fixes found during verification, then create the PR:**

```bash
gh pr create \
  --title "feat: JSON-driven demo config — SD card + sim + MQTT" \
  --body "$(cat <<'EOF'
## Summary
- Boot reads sapdm_config.json from SD card; falls back to embedded default
- Machines and tanks screens render dynamically from loaded entity count
- Sim task runs generic metric loop (random_walk / sine / ramp profiles)
- MQTT publishes every metric to factory/{type}/{id}/{key} at 1.5 s intervals

## Test plan
- [ ] Flash with no SD card — verify default demo (4 machines, 3 tanks, 5 operators)
- [ ] Flash with minimal 1-machine 1-tank JSON — verify UI adapts
- [ ] Verify MQTT messages arrive in MQTT Explorer on correct topics
- [ ] Verify drag-to-adjust on tanks publishes factory/tanks/{id}/level
EOF
)"
```
