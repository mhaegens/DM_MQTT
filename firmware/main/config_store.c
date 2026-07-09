// config_store.c — Config template storage shared by the captive portal and
// the on-device Configuration screen. Owns the SPIFFS/SD plumbing that used
// to live in wifi_portal.c so both entry points behave identically.
#include "config_store.h"
#include "config_loader.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static const char *TAG = "cfg_store";

// The store is reachable from two tasks at once (httpd portal handlers and
// the LVGL task via the Configuration screen) — serialize every public entry
// point. Recursive: activate() calls save_active() while holding the lock.
static SemaphoreHandle_t s_lock = NULL;

void config_store_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
}

#define STORE_LOCK()   do { if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); } while (0)
#define STORE_UNLOCK() do { if (s_lock) xSemaphoreGiveRecursive(s_lock); } while (0)

#define CFG_ACTIVE_NAME        "sapdm_config.json"
#define CFG_ANNOTATED_NAME     "sapdm_config.annotated.json"
#define CFG_SD_ACTIVE_PATH     "/sdcard/" CFG_ACTIVE_NAME
#define CFG_SPIFFS_ACTIVE_PATH "/spiffs/" CFG_ACTIVE_NAME
#define CFG_JSON_MAX           (32 * 1024)

// ─── Mount helpers ────────────────────────────────────────────────────────────

// SPIFFS is registered (and left mounted) by config_load() at boot; re-register
// here only if that failed, e.g. after a flash erase without a SPIFFS image.
static bool spiffs_ready(void)
{
    if (esp_spiffs_mounted("spiffs")) return true;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 4,
        .format_if_mount_failed = false,
    };
    return esp_vfs_spiffs_register(&conf) == ESP_OK;
}

// SD mounts are refcounted so nested users (activate → save_active) don't
// yank the card out from under each other.
static sdmmc_card_t *s_card = NULL;
static int           s_sd_refs = 0;

// True once a card has mounted in this power cycle. At boot an SD active
// config outranks SPIFFS, so a card that was seen but stops responding must
// turn flash-only saves into loud failures (the stale card copy would mask
// them at the next boot).
static bool s_sd_expected = false;

static bool sd_mount(void)
{
    if (s_card) { s_sd_refs++; return true; }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // CRITICAL: ESP-Hosted C6 Wi-Fi bridge owns SDMMC slot 1 (SDIO 4-bit).
    // SDMMC_HOST_DEFAULT() picks slot 1 on P4 by default — mounting the SD card
    // there yanks the Wi-Fi bus mid-transaction and panics. The BSP puts the
    // SD card on slot 0; both sockets have to be explicit.
    host.slot = SDMMC_HOST_SLOT_0;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk   = GPIO_SD_CLK;
    slot.cmd   = GPIO_SD_CMD;
    slot.d0    = GPIO_SD_D0;
    slot.d1    = GPIO_SD_D1;
    slot.d2    = GPIO_SD_D2;
    slot.d3    = GPIO_SD_D3;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "SD mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return false;
    }
    s_sd_refs = 1;
    s_sd_expected = true;
    return true;
}

static void sd_unmount(void)
{
    if (!s_card) return;
    if (--s_sd_refs > 0) return;
    esp_vfs_fat_sdcard_unmount("/sdcard", s_card);
    s_card = NULL;
}

// ─── File helpers ─────────────────────────────────────────────────────────────

static bool write_whole_file(const char *path, const char *data, int len)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    bool ok = (fwrite(data, 1, len, f) == (size_t)len);
    fclose(f);
    return ok;
}

// Read up to cap-1 bytes into buf (NUL-terminated). Returns the byte count,
// -1 if the file does not exist, -2 if it exceeds the buffer.
static int read_whole_file(const char *path, char *buf, int cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = (int)fread(buf, 1, cap - 1, f);
    fclose(f);
    if (n >= cap - 1) return -2;   // refuse silent truncation
    buf[n] = '\0';
    return n;
}

// ─── Save active config (both stores) ────────────────────────────────────────

const char *config_store_save_active(const char *data, int len)
{
    STORE_LOCK();
    bool spiffs_saved = spiffs_ready() &&
                        write_whole_file(CFG_SPIFFS_ACTIVE_PATH, data, len);

    bool sd_present = sd_mount();
    bool sd_saved   = false;
    if (sd_present) {
        sd_saved = write_whole_file(CFG_SD_ACTIVE_PATH, data, len);
        sd_unmount();
    }
    STORE_UNLOCK();

    // An inserted SD card outranks SPIFFS at boot — if it exists but could not
    // be written, the stale card config would mask this save. Fail loudly.
    if (sd_present && !sd_saved) {
        return spiffs_saved
            ? "Saved to flash, but the inserted SD card failed to write - remove or replace the card, then retry"
            : "Cannot write config to SD card or flash";
    }
    // Same masking hazard when a card that mounted earlier in this power
    // cycle stops responding: its stale active config wins at the next boot,
    // so a flash-only "success" here would silently be undone by the reboot.
    if (!sd_present && s_sd_expected) {
        return spiffs_saved
            ? "Saved to flash, but the SD card stopped responding - its old config will win at boot. Reseat or remove the card, then retry"
            : "Cannot save config (SD card stopped responding, flash write failed)";
    }
    if (!spiffs_saved && !sd_saved)
        return "Cannot save config (flash and SD card both unavailable)";

    ESP_LOGI(TAG, "active config saved (%d bytes, spiffs=%d sd=%d)",
             len, spiffs_saved, sd_saved);
    return NULL;
}

// ─── Template listing ─────────────────────────────────────────────────────────

// Route cJSON tree allocations to SPIRAM — internal SRAM is too scarce for a
// parsed 32 KB config tree (same policy as config_loader.c).
static void *psram_malloc(size_t sz) {
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// Extract plant name + entity counts from a config JSON blob.
static void tpl_parse_meta(ConfigTemplate *t, const char *json_str)
{
    cJSON_Hooks hooks = { .malloc_fn = psram_malloc, .free_fn = free };
    cJSON_InitHooks(&hooks);
    cJSON *root = cJSON_Parse(json_str);
    cJSON_InitHooks(NULL);
    if (!root) return;

    const cJSON *v = cJSON_GetObjectItem(root, "plant_name");
    if (cJSON_IsString(v))
        strncpy(t->plant, v->valuestring, sizeof(t->plant) - 1);
    t->machines = cJSON_GetArraySize(cJSON_GetObjectItem(root, "machines"));
    t->tanks    = cJSON_GetArraySize(cJSON_GetObjectItem(root, "tanks"));
    cJSON_Delete(root);
}

static bool name_is_template(const char *name)
{
    size_t nlen = strlen(name);
    // 6 = shortest real name, "x.json" — a bare ".json" is not a template
    if (nlen < 6 || nlen >= CFG_TPL_NAME_MAX) return false;
    if (strcasecmp(name + nlen - 5, ".json") != 0) return false;
    // The active file and its annotated documentation copy are not templates
    if (strcasecmp(name, CFG_ACTIVE_NAME) == 0) return false;
    if (strcasecmp(name, CFG_ANNOTATED_NAME) == 0) return false;
    return true;
}

// A safe, storable template filename: bare "name.json" with no path parts,
// not one of the reserved active-config names.
static bool tpl_name_ok(const char *name)
{
    if (!name || !name_is_template(name)) return false;
    if (strchr(name, '/') || strchr(name, '\\') || strstr(name, ".."))
        return false;
    return true;
}

void config_store_sanitize_tpl_name(const char *raw, char *out, int cap)
{
    char tmp[CFG_TPL_NAME_MAX];

    // Basename only — browsers/OSes may leave a path on the upload name
    const char *base = raw ? raw : "";
    for (const char *p = base; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;

    // Copy with FAT-hostile characters replaced; keep room to append ".json"
    int i = 0;
    for (const char *p = base; *p && i < (int)sizeof(tmp) - 6; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-' || c == ' ';
        tmp[i++] = ok ? c : '_';
    }
    tmp[i] = '\0';

    size_t nlen = strlen(tmp);
    if (nlen < 5 || strcasecmp(tmp + nlen - 5, ".json") != 0)
        strlcat(tmp, ".json", sizeof(tmp));

    snprintf(out, cap, "%s", tpl_name_ok(tmp) ? tmp : CFG_TPL_FALLBACK_NAME);
}

static int list_dir(const char *dirpath, bool from_sd,
                    ConfigTemplate *out, int n, int max,
                    const char *active, int active_len, char *fbuf)
{
    DIR *dir = opendir(dirpath);
    if (!dir) return n;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && n < max) {
        // FAT VFS always returns DT_UNKNOWN — filter by name only
        if (!name_is_template(entry->d_name)) continue;

        // Skip names already listed: templates seeded from flash to the SD
        // card exist on both stores, and the SD copy (listed first) wins.
        bool dup = false;
        for (int i = 0; i < n && !dup; i++)
            dup = (strcasecmp(out[i].name, entry->d_name) == 0);
        if (dup) continue;

        char path[96];
        snprintf(path, sizeof(path), "%s/%s", dirpath, entry->d_name);
        int len = read_whole_file(path, fbuf, CFG_JSON_MAX);
        if (len <= 0) continue;
        if (!config_json_valid(fbuf)) continue;   // hide non-config JSON files

        ConfigTemplate *t = &out[n];
        memset(t, 0, sizeof(*t));
        strncpy(t->name, entry->d_name, sizeof(t->name) - 1);
        t->from_sd = from_sd;
        t->active  = (len == active_len && memcmp(fbuf, active, len) == 0);
        tpl_parse_meta(t, fbuf);
        n++;
    }
    closedir(dir);
    return n;
}

int config_store_list(ConfigTemplate *out, int max)
{
    if (!out || max <= 0) return 0;

    char *active = heap_caps_malloc(CFG_JSON_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *fbuf   = heap_caps_malloc(CFG_JSON_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!active || !fbuf) {
        free(active);
        free(fbuf);
        return 0;
    }

    STORE_LOCK();
    bool sp = spiffs_ready();
    bool sd = sd_mount();

    // Active config content for the identity check: the SD copy wins at boot
    // when a card is inserted, so compare against that one first.
    int active_len = -1;
    if (sd) active_len = read_whole_file(CFG_SD_ACTIVE_PATH, active, CFG_JSON_MAX);
    if (active_len < 0 && sp)
        active_len = read_whole_file(CFG_SPIFFS_ACTIVE_PATH, active, CFG_JSON_MAX);

    // SD card first: it is the home of the profile library (bundled templates
    // are seeded onto it at boot), so its copies win the duplicate check and
    // flash-only entries still show up when a file has not reached the card.
    int n = 0;
    if (sd) n = list_dir("/sdcard", true, out, n, max, active, active_len, fbuf);
    if (sp) n = list_dir("/spiffs", false, out, n, max, active, active_len, fbuf);
    if (sd) sd_unmount();
    STORE_UNLOCK();

    free(active);
    free(fbuf);
    ESP_LOGI(TAG, "listed %d config templates", n);
    return n;
}

// ─── Activate a template ──────────────────────────────────────────────────────

const char *config_store_activate(const char *name, bool from_sd)
{
    // Validate: must end in .json, no slashes, no ..
    size_t nlen = name ? strlen(name) : 0;
    if (nlen < 5 || strcasecmp(name + nlen - 5, ".json") != 0 ||
        strchr(name, '/') || strstr(name, ".."))
        return "Invalid filename";

    STORE_LOCK();
    if (from_sd && !sd_mount()) {
        STORE_UNLOCK();
        return "SD card not available";
    }
    if (!from_sd && !spiffs_ready()) {
        STORE_UNLOCK();
        return "Flash storage not available";
    }

    char *data = heap_caps_malloc(CFG_JSON_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) {
        if (from_sd) sd_unmount();
        STORE_UNLOCK();
        return "Out of memory";
    }

    char path[96];
    snprintf(path, sizeof(path), "%s/%s", from_sd ? "/sdcard" : "/spiffs", name);
    int len = read_whole_file(path, data, CFG_JSON_MAX);

    const char *err = NULL;
    if (len == -1)      err = "File not found";
    else if (len < 0)   err = "File too large";
    else if (!config_json_valid(data)) err = "File is not a valid config";
    else                err = config_store_save_active(data, len);

    free(data);
    if (from_sd) sd_unmount();
    STORE_UNLOCK();

    if (!err)
        ESP_LOGI(TAG, "activated template %s (%s)", name, from_sd ? "sd" : "flash");
    return err;
}

// ─── Save a named template ────────────────────────────────────────────────────

const char *config_store_save_template(const char *name, const char *data,
                                       int len, bool *saved_to_sd)
{
    if (saved_to_sd) *saved_to_sd = false;
    if (!tpl_name_ok(name)) return "Invalid template filename";

    char path[96];
    const char *err = NULL;

    STORE_LOCK();
    // SD card is the home of the profile library — flash is only the fallback
    // so a missing/full card does not lose the upload entirely.
    if (sd_mount()) {
        snprintf(path, sizeof(path), "/sdcard/%s", name);
        bool ok = write_whole_file(path, data, len);
        sd_unmount();
        if (ok) {
            STORE_UNLOCK();
            if (saved_to_sd) *saved_to_sd = true;
            ESP_LOGI(TAG, "template %s saved to SD card (%d bytes)", name, len);
            return NULL;
        }
        ESP_LOGW(TAG, "template %s failed to write to SD — trying flash", name);
    }
    if (spiffs_ready()) {
        snprintf(path, sizeof(path), "/spiffs/%s", name);
        if (write_whole_file(path, data, len))
            ESP_LOGI(TAG, "template %s saved to flash (%d bytes)", name, len);
        else
            err = "Cannot store template on SD card or flash";
    } else {
        err = "No storage available for template";
    }
    STORE_UNLOCK();
    return err;
}

// ─── Seed the SD card from the bundled flash configs ─────────────────────────

int config_store_seed_sd(void)
{
    STORE_LOCK();
    if (!sd_mount()) {
        STORE_UNLOCK();
        ESP_LOGI(TAG, "no SD card — profile library stays on flash");
        return -1;
    }

    int copied = 0;
    char *buf = heap_caps_malloc(CFG_JSON_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    DIR *dir = (buf && spiffs_ready()) ? opendir("/spiffs") : NULL;
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            // Every bundled *.json travels to the card: the scenario
            // templates, the annotated schema doc, and the active config.
            const char *name = entry->d_name;
            size_t nlen = strlen(name);
            if (nlen < 5 || strcasecmp(name + nlen - 5, ".json") != 0) continue;

            char dst[96];
            snprintf(dst, sizeof(dst), "/sdcard/%s", name);
            FILE *probe = fopen(dst, "r");
            if (probe) { fclose(probe); continue; }   // never overwrite card copies

            char src[96];
            snprintf(src, sizeof(src), "/spiffs/%s", name);
            int len = read_whole_file(src, buf, CFG_JSON_MAX);
            if (len <= 0) continue;

            if (write_whole_file(dst, buf, len)) {
                ESP_LOGI(TAG, "seeded %s to SD card (%d bytes)", name, len);
                copied++;
            } else {
                ESP_LOGW(TAG, "failed to seed %s to SD card", name);
            }
        }
        closedir(dir);
    }

    free(buf);
    sd_unmount();
    STORE_UNLOCK();
    if (copied)
        ESP_LOGI(TAG, "SD card seeded with %d bundled config file(s)", copied);
    return copied;
}
