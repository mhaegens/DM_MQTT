#include "config_loader.h"
#include "default_config.h"
#include "ui/data.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cfg";

#define JSON_BUF_SIZE (32 * 1024)

// ─── cJSON SPIRAM allocator ───────────────────────────────────────────────────
static void *psram_malloc(size_t sz) {
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

const char *config_result_str(ConfigResult r) {
    switch (r) {
    case CONFIG_OK:             return "SD card loaded";
    case CONFIG_SPIFFS_OK:      return "SPIFFS loaded";
    case CONFIG_SD_NOT_FOUND:   return "SD card not found — using default";
    case CONFIG_FILE_NOT_FOUND: return "sapdm_config.json not found — using default";
    case CONFIG_PARSE_ERROR:    return "JSON parse error — using default";
    case CONFIG_USING_DEFAULT:  return "using embedded default";
    }
    return "unknown";
}

static void parse_metric(const cJSON *j, Metric *m) {
    memset(m, 0, sizeof(Metric));

    const cJSON *v;
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
            if      (!strcmp(v->valuestring, "sine"))   m->sim_profile = SIM_SINE;
            else if (!strcmp(v->valuestring, "ramp"))   m->sim_profile = SIM_RAMP;
            else if (!strcmp(v->valuestring, "steady")) m->sim_profile = SIM_STEADY;
            else                                         m->sim_profile = SIM_RANDOM_WALK;
        }
        if ((v = cJSON_GetObjectItem(sim, "rand_min"))  && cJSON_IsNumber(v)) m->rand_min      = (float)v->valuedouble;
        if ((v = cJSON_GetObjectItem(sim, "rand_max"))  && cJSON_IsNumber(v)) m->rand_max      = (float)v->valuedouble;
        if ((v = cJSON_GetObjectItem(sim, "amplitude")) && cJSON_IsNumber(v)) m->sim_amplitude = (float)v->valuedouble;
        if ((v = cJSON_GetObjectItem(sim, "period_s"))  && cJSON_IsNumber(v)) m->sim_period_s  = (float)v->valuedouble;
        if ((v = cJSON_GetObjectItem(sim, "offset"))    && cJSON_IsNumber(v)) m->sim_offset    = (float)v->valuedouble;
    }
    #undef STR
    #undef NUM

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
        if ((v = cJSON_GetObjectItem(item, "id"))      && cJSON_IsString(v)) strncpy(m->id,   v->valuestring, sizeof(m->id)   - 1);
        if ((v = cJSON_GetObjectItem(item, "name"))    && cJSON_IsString(v)) strncpy(m->name, v->valuestring, sizeof(m->name) - 1);
        if ((v = cJSON_GetObjectItem(item, "type"))    && cJSON_IsString(v)) strncpy(m->type, v->valuestring, sizeof(m->type) - 1);
        if ((v = cJSON_GetObjectItem(item, "running"))    && cJSON_IsBool(v))   m->running    = cJSON_IsTrue(v);
        if ((v = cJSON_GetObjectItem(item, "auto_pulse")) && cJSON_IsBool(v))   m->auto_pulse = cJSON_IsTrue(v);
        if ((v = cJSON_GetObjectItem(item, "pulses"))     && cJSON_IsNumber(v)) m->pulses     = v->valueint;
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
        if ((v = cJSON_GetObjectItem(item, "id"))   && cJSON_IsString(v)) strncpy(t->id,   v->valuestring, sizeof(t->id)   - 1);
        if ((v = cJSON_GetObjectItem(item, "name")) && cJSON_IsString(v)) strncpy(t->name, v->valuestring, sizeof(t->name) - 1);
        const cJSON *metrics = cJSON_GetObjectItem(item, "metrics");
        t->metric_count = 0;
        const cJSON *met;
        cJSON_ArrayForEach(met, metrics) {
            if (t->metric_count >= MAX_TANK_METRICS) break;
            parse_metric(met, &t->metrics[t->metric_count++]);
        }
        g_tank_count++;
    }

    arr = cJSON_GetObjectItem(root, "people");
    g_badge_count = 0;
    int badge_total = cJSON_GetArraySize(arr);
    cJSON_ArrayForEach(item, arr) {
        if (g_badge_count >= MAX_BADGES) {
            ESP_LOGW(TAG, "people array has %d entries — MAX_BADGES=%d, extras ignored",
                     badge_total, MAX_BADGES);
            break;
        }
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
        BS("plant",    b->plant,    sizeof(b->plant));
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

// Read an entire JSON file into buf (capacity JSON_BUF_SIZE).
// Returns 0 on success, -1 if the file does not exist, -2 if it exceeds the
// buffer — oversized configs are rejected loudly instead of being silently
// truncated into a misleading "parse error".
static int read_json_file(const char *path, char *buf) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz >= JSON_BUF_SIZE) {
        fclose(f);
        ESP_LOGE(TAG, "%s is %ld bytes — over the %d byte limit, ignoring it",
                 path, sz, JSON_BUF_SIZE - 1);
        return -2;
    }
    size_t n = fread(buf, 1, JSON_BUF_SIZE - 1, f);
    buf[n] = '\0';
    fclose(f);
    return 0;
}

static ConfigResult parse_and_populate(const char *json_str) {
    // Route cJSON tree allocations to SPIRAM for duration of parse
    cJSON_Hooks hooks = { .malloc_fn = psram_malloc, .free_fn = free };
    cJSON_InitHooks(&hooks);

    cJSON *root = cJSON_Parse(json_str);

    // Restore default cJSON allocator before any early return
    cJSON_InitHooks(NULL);

    if (!root) {
        ESP_LOGE(TAG, "cJSON parse failed: %s", cJSON_GetErrorPtr());
        return CONFIG_PARSE_ERROR;
    }
    populate_from_json(root);
    cJSON_Delete(root);
    return CONFIG_OK;
}

bool config_json_valid(const char *json_str) {
    cJSON_Hooks hooks = { .malloc_fn = psram_malloc, .free_fn = free };
    cJSON_InitHooks(&hooks);
    cJSON *root = cJSON_Parse(json_str);
    cJSON_InitHooks(NULL);
    if (!root) return false;
    bool ok = cJSON_IsArray(cJSON_GetObjectItem(root, "machines")) ||
              cJSON_IsArray(cJSON_GetObjectItem(root, "tanks"))    ||
              cJSON_IsArray(cJSON_GetObjectItem(root, "people"));
    cJSON_Delete(root);
    return ok;
}

// Load order: SD card first (an inserted card is an explicit external
// override), then SPIFFS (factory image + captive-portal uploads), then the
// embedded default. The portal writes uploads to BOTH stores so the winner
// always carries the latest upload; see wifi_portal.c.
ConfigResult config_load(void) {
    // Register SPIFFS up front and leave it mounted — the captive portal
    // reads/writes /spiffs/sapdm_config.json at runtime.
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 4,
        .format_if_mount_failed = false,
    };
    esp_err_t sp_err = esp_vfs_spiffs_register(&spiffs_conf);
    bool spiffs_ok = (sp_err == ESP_OK || sp_err == ESP_ERR_INVALID_STATE);
    if (!spiffs_ok)
        ESP_LOGW(TAG, "SPIFFS mount failed (%s)", esp_err_to_name(sp_err));

    char *buf = malloc(JSON_BUF_SIZE);
    if (!buf) {
        ESP_LOGW(TAG, "alloc failed — config source: embedded default");
        parse_and_populate(DEFAULT_CONFIG_JSON);
        return CONFIG_PARSE_ERROR;
    }

    // --- 1. SD card ---
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // ESP-Hosted C6 owns slot 1 (SDIO Wi-Fi). SD card is on slot 0 per BSP.
    host.slot = SDMMC_HOST_SLOT_0;
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

    bool sd_present = false;
    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount_cfg, &card);
    if (err == ESP_OK) {
        sd_present = true;
        int rr = read_json_file("/sdcard/sapdm_config.json", buf);
        esp_vfs_fat_sdcard_unmount("/sdcard", card);
        if (rr == 0) {
            if (parse_and_populate(buf) == CONFIG_OK) {
                free(buf);
                ESP_LOGI(TAG, "config source: SD card (/sdcard/sapdm_config.json)");
                return CONFIG_OK;
            }
            ESP_LOGW(TAG, "SD JSON parse failed — trying SPIFFS");
        } else if (rr == -1) {
            ESP_LOGI(TAG, "no sapdm_config.json on SD — trying SPIFFS");
        }
    } else {
        ESP_LOGI(TAG, "SD mount failed (%s) — trying SPIFFS", esp_err_to_name(err));
    }

    // --- 2. SPIFFS ---
    if (spiffs_ok) {
        int rr = read_json_file("/spiffs/sapdm_config.json", buf);
        if (rr == 0) {
            if (parse_and_populate(buf) == CONFIG_OK) {
                free(buf);
                ESP_LOGI(TAG, "config source: SPIFFS (/spiffs/sapdm_config.json)");
                return CONFIG_SPIFFS_OK;
            }
            ESP_LOGW(TAG, "SPIFFS JSON parse failed — using embedded default");
            free(buf);
            parse_and_populate(DEFAULT_CONFIG_JSON);
            return CONFIG_PARSE_ERROR;
        }
        if (rr == -1)
            ESP_LOGW(TAG, "no /spiffs/sapdm_config.json — using embedded default");
    }

    // --- 3. Embedded default ---
    free(buf);
    parse_and_populate(DEFAULT_CONFIG_JSON);
    return sd_present ? CONFIG_FILE_NOT_FOUND : CONFIG_SD_NOT_FOUND;
}
