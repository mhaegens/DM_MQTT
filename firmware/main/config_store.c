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
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include <unistd.h>
#include <errno.h>
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
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static ConfigStorageStats s_stats;

void config_store_get_stats(ConfigStorageStats *out) {
    portENTER_CRITICAL(&s_stats_lock); *out = s_stats; portEXIT_CRITICAL(&s_stats_lock);
}
static void storage_error(const char *message) {
    portENTER_CRITICAL(&s_stats_lock); snprintf(s_stats.last_error, sizeof(s_stats.last_error), "%s", message); portEXIT_CRITICAL(&s_stats_lock);
}

void config_store_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
}

#define STORE_LOCK()   do { if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); } while (0)
#define STORE_UNLOCK() do { if (s_lock) xSemaphoreGiveRecursive(s_lock); } while (0)

#define CFG_ACTIVE_NAME        "sapdm_config.json"
#define CFG_ANNOTATED_NAME     "sapdm_config.annotated.json"
#ifndef CONFIG_STORE_SD_PATH
#define CONFIG_STORE_SD_PATH "/sdcard"
#endif
#ifndef CONFIG_STORE_FLASH_PATH
#define CONFIG_STORE_FLASH_PATH "/spiffs"
#endif
#define SD_ROOT CONFIG_STORE_SD_PATH
#define FLASH_ROOT CONFIG_STORE_FLASH_PATH
#define CFG_SD_ACTIVE_PATH SD_ROOT "/" CFG_ACTIVE_NAME
#define CFG_SPIFFS_ACTIVE_PATH FLASH_ROOT "/" CFG_ACTIVE_NAME
#define CFG_JSON_MAX           (32 * 1024)

// ─── Mount helpers ────────────────────────────────────────────────────────────

// SPIFFS is registered (and left mounted) by config_load() at boot; re-register
// here only if that failed, e.g. after a flash erase without a SPIFFS image.
static bool spiffs_ready(void)
{
    if (esp_spiffs_mounted("spiffs")) return true;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = FLASH_ROOT,
        .partition_label = "spiffs",
        .max_files = 4,
        .format_if_mount_failed = false,
    };
    return esp_vfs_spiffs_register(&conf) == ESP_OK;
}

// The store owns one persistent SD mount shared by all callers.
static sdmmc_card_t *s_card = NULL;
static esp_ldo_channel_handle_t s_sd_ldo;
static bool s_sd_library_synced;

// True once a card has mounted in this power cycle. At boot an SD active
// config outranks SPIFFS, so a card that was seen but stops responding must
// turn flash-only saves into loud failures (the stale card copy would mask
// them at the next boot).
static bool s_sd_expected = false;

static bool sd_mount(void)
{
    if (s_card) return true;
    if (!s_sd_ldo) {
        // Waveshare powers GPIO39-45 from VO4. The display BSP enables this
        // later, which made an earlier SD mount depend on the reset history.
        esp_ldo_channel_config_t power = { .chan_id = 4, .voltage_mv = 3300 };
        esp_err_t err = esp_ldo_acquire_channel(&power, &s_sd_ldo);
        if (err != ESP_OK) { storage_error("Cannot enable SD power rail"); return false; }
    }
    gpio_set_direction(GPIO_SD_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_SD_POWER, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
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
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_ROOT, &host, &slot, &cfg, &s_card);
    if (err != ESP_OK) {
        // Reset only the card's power; never reset the C6's SDIO slot 1.
        gpio_set_level(GPIO_SD_POWER, 1);
        vTaskDelay(pdMS_TO_TICKS(40));
        gpio_set_level(GPIO_SD_POWER, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        err = esp_vfs_fat_sdmmc_mount(SD_ROOT, &host, &slot, &cfg, &s_card);
    }
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "SD mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        storage_error("SD unavailable: check the card and filesystem");
        return false;
    }
    s_sd_expected = true;
    uint64_t total = 0, available = 0;
    esp_vfs_fat_info(SD_ROOT, &total, &available);
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.sd_ready = true; s_stats.sd_total_bytes = total; s_stats.sd_free_bytes = available;
    s_stats.last_error[0] = 0;
    portEXIT_CRITICAL(&s_stats_lock);
    ESP_LOGI(TAG, "SD mounted: %llu MB, %llu MB free", (unsigned long long)(total / 1048576), (unsigned long long)(available / 1048576));
    return true;
}

static void sd_unmount(void)
{
    // Keep the mount for the entire boot. Repeatedly probing/deinitializing
    // a card during profile browsing caused failures and unnecessary stalls.
}

// ─── File helpers ─────────────────────────────────────────────────────────────

static bool write_whole_file(const char *path, const char *data, int len)
{
    char chunk[512];
    // Avoid wear and flash stalls when activating/re-uploading identical data.
    FILE *previous = fopen(path, "r");
    if (previous) {
        bool same = true; int offset = 0;
        while (offset < len && same) {
            int want = len - offset < (int)sizeof(chunk) ? len - offset : (int)sizeof(chunk);
            int got = (int)fread(chunk, 1, want, previous);
            same = got == want && !memcmp(chunk, data + offset, want);
            offset += got;
        }
        same = same && fgetc(previous) == EOF && !ferror(previous);
        if (fclose(previous) != 0) same = false;
        if (same) return true;
    }
    char temporary[128], backup[128];
    snprintf(temporary, sizeof(temporary), "%s.pending", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    FILE *f = fopen(temporary, "w");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == (size_t)len && fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) { unlink(temporary); return false; }
    // Verify bytes before replacing any existing profile.
    f = fopen(temporary, "r");
    if (!f) { unlink(temporary); return false; }
    int offset = 0;
    while (offset < len && ok) {
        int want = len - offset < (int)sizeof(chunk) ? len - offset : (int)sizeof(chunk);
        int got = (int)fread(chunk, 1, want, f);
        ok = got == want && !memcmp(chunk, data + offset, want);
        offset += got;
    }
    if (fclose(f) != 0) ok = false;
    if (!ok) { unlink(temporary); return false; }
    bool had_previous = access(path, F_OK) == 0;
    unlink(backup);
    if (had_previous && rename(path, backup) != 0) { unlink(temporary); return false; }
    if (rename(temporary, path) != 0) {
        if (had_previous) rename(backup, path);
        unlink(temporary); return false;
    }
    unlink(backup);
    return true;
}

// Read up to cap-1 bytes into buf (NUL-terminated). Returns the byte count,
// -1 if the file does not exist, -2 if it exceeds the buffer.
static int read_whole_file(const char *path, char *buf, int cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = (int)fread(buf, 1, cap - 1, f);
    bool failed = ferror(f);
    bool oversized = n == cap - 1 && fgetc(f) != EOF;
    if (fclose(f) != 0) failed = true;
    if (failed) return -3;
    if (oversized) return -2;
    buf[n] = 0;
    return n;
}

static void recover_directory(const char *directory)
{
    DIR *dir = opendir(directory);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        size_t length = strlen(entry->d_name);
        if (length < 10 || length > CFG_TPL_NAME_MAX + 4 ||
            strcasecmp(entry->d_name + length - 9, ".json.bak")) continue;
        char original[128], backup[128];
        snprintf(backup, sizeof(backup), "%s/%s", directory, entry->d_name);
        snprintf(original, sizeof(original), "%s/%.*s", directory, (int)length - 4, entry->d_name);
        if (access(original, F_OK) != 0) rename(backup, original);
    }
    closedir(dir);
}

int config_store_read_active(char *buf, int capacity, bool *from_sd)
{
    STORE_LOCK();
    bool sd = sd_mount();
    if (sd) recover_directory(SD_ROOT);
    if (spiffs_ready()) recover_directory(FLASH_ROOT);
    int length = sd ? read_whole_file(CFG_SD_ACTIVE_PATH, buf, capacity) : -1;
    if (length >= 0) { if (from_sd) *from_sd = true; }
    else {
        if (from_sd) *from_sd = false;
        length = spiffs_ready() ? read_whole_file(CFG_SPIFFS_ACTIVE_PATH, buf, capacity) : -1;
    }
    STORE_UNLOCK();
    return length;
}

// ─── Save active config (both stores) ────────────────────────────────────────

const char *config_store_save_active(const char *data, int len)
{
    if (!data || len <= 0 || len >= CFG_JSON_MAX || !config_json_valid(data)) return "Invalid active configuration";
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

// Extract plant name + entity counts from a config JSON blob.
static void tpl_parse_meta(ConfigTemplate *t, const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
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
        snprintf(tmp + nlen, sizeof(tmp) - nlen, ".json");

    snprintf(out, cap, "%s", tpl_name_ok(tmp) ? tmp : CFG_TPL_FALLBACK_NAME);
}

static int list_dir_all(const char *dirpath, bool from_sd, ConfigTemplate **out,
                        int n, int *capacity, const char *active, int active_len, char *buf)
{
    DIR *dir = opendir(dirpath);
    if (!dir) return n;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!name_is_template(entry->d_name)) continue;
        bool duplicate = false;
        for (int i = 0; i < n; ++i)
            if (!strcasecmp((*out)[i].name, entry->d_name)) duplicate = true;
        if (duplicate) continue;
        if (n == *capacity) {
            int next = *capacity ? *capacity * 2 : 32;
            ConfigTemplate *grown = heap_caps_realloc(*out, (size_t)next * sizeof(**out),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!grown) { storage_error("Not enough memory to index all profiles"); break; }
            *out = grown; *capacity = next;
        }
        ConfigTemplate *t = &(*out)[n++];
        memset(t, 0, sizeof(*t));
        snprintf(t->name, sizeof(t->name), "%.47s", entry->d_name);
        t->from_sd = from_sd;
        char path[96]; snprintf(path, sizeof(path), "%s/%s", dirpath, t->name);
        int length = read_whole_file(path, buf, CFG_JSON_MAX);
        t->valid = length >= 0 && config_json_valid(buf);
        t->active = length > 0 && length == active_len && !memcmp(buf, active, length);
        if (length > 0) tpl_parse_meta(t, buf);
        // Profile parsing must not monopolize the networking core.
        vTaskDelay(1);
    }
    closedir(dir);
    return n;
}

static int compare_templates(const void *a, const void *b)
{
    return strcasecmp(((const ConfigTemplate *)a)->name, ((const ConfigTemplate *)b)->name);
}

ConfigTemplate *config_store_list_all(int *count)
{
    *count = 0;
    char *active = heap_caps_malloc(CFG_JSON_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *buf = heap_caps_malloc(CFG_JSON_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!active || !buf) { free(active); free(buf); return NULL; }
    STORE_LOCK();
    bool sp = spiffs_ready(), sd = sd_mount();
    int active_len = sd ? read_whole_file(CFG_SD_ACTIVE_PATH, active, CFG_JSON_MAX) : -1;
    if (active_len < 0 && sp) active_len = read_whole_file(CFG_SPIFFS_ACTIVE_PATH, active, CFG_JSON_MAX);
    int capacity = 0, n = 0;
    ConfigTemplate *list = NULL;
    if (sd) n = list_dir_all(SD_ROOT, true, &list, n, &capacity, active, active_len, buf);
    // Boot seeding/imports keep named flash profiles on SD. Once synchronized,
    // avoid rescanning the large SPIFFS volume on every page/portal request.
    if (sp && (!sd || !s_sd_library_synced))
        n = list_dir_all(FLASH_ROOT, false, &list, n, &capacity, active, active_len, buf);
    if (n > 1) qsort(list, n, sizeof(*list), compare_templates);
    int invalid = 0;
    for (int i = 0; i < n; ++i) if (!list[i].valid) ++invalid;
    uint64_t total = 0, available = 0;
    if (sd) esp_vfs_fat_info(SD_ROOT, &total, &available);
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.profiles = n; s_stats.invalid_profiles = invalid;
    if (sd) { s_stats.sd_total_bytes = total; s_stats.sd_free_bytes = available; }
    portEXIT_CRITICAL(&s_stats_lock);
    STORE_UNLOCK();
    free(active); free(buf); *count = n;
    ESP_LOGI(TAG, "profile library: %d files (%d need attention), SD=%d", n, invalid, sd);
    return list;
}

int config_store_list(ConfigTemplate *out, int maximum)
{
    if (!out || maximum <= 0) return 0;
    int count = 0;
    ConfigTemplate *list = config_store_list_all(&count);
    int copied = count < maximum ? count : maximum;
    if (copied) memcpy(out, list, copied * sizeof(*out));
    free(list);
    return copied;
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
    snprintf(path, sizeof(path), "%s/%s", from_sd ? SD_ROOT : FLASH_ROOT, name);
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

const char *config_store_import(const char *name, const char *data, int len,
                               bool *saved_to_sd, char *stored_name, int name_capacity)
{
    if (saved_to_sd) *saved_to_sd = false;
    if (!tpl_name_ok(name) || len <= 0 || !config_json_valid(data)) return "Invalid profile";
    STORE_LOCK();
    bool sd = sd_mount(), sp = spiffs_ready();
    if (!sd && !sp) { STORE_UNLOCK(); return "No profile storage available"; }
    char candidate[CFG_TPL_NAME_MAX]; snprintf(candidate, sizeof(candidate), "%s", name);
    char *existing = heap_caps_malloc(CFG_JSON_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!existing) { STORE_UNLOCK(); return "Not enough memory to save profile"; }
    bool chosen = false;
    for (int version = 1; version < 100000 && !chosen; ++version) {
        if (version > 1) snprintf(candidate, sizeof(candidate), "%.*s_%d.json",
                                  (int)(strlen(name) - 5) < 32 ? (int)(strlen(name) - 5) : 32, name, version);
        char path[96]; bool conflict = false;
        const char *directories[] = {SD_ROOT, FLASH_ROOT};
        for (int i = 0; i < 2; ++i) {
            if ((i == 0 && !sd) || (i == 1 && !sp)) continue;
            snprintf(path, sizeof(path), "%s/%s", directories[i], candidate);
            int old_len = read_whole_file(path, existing, CFG_JSON_MAX);
            if (old_len != -1 && (old_len != len || memcmp(existing, data, len))) conflict = true;
        }
        chosen = !conflict;
    }
    free(existing);
    if (!chosen) { STORE_UNLOCK(); return "Cannot create a distinct profile filename"; }
    char path[96]; bool sd_saved = false, flash_saved = false;
    if (sd) { snprintf(path, sizeof(path), SD_ROOT "/%s", candidate); sd_saved = write_whole_file(path, data, len); }
    if (sp) { snprintf(path, sizeof(path), FLASH_ROOT "/%s", candidate); flash_saved = write_whole_file(path, data, len); }
    if (flash_saved && !sd_saved) s_sd_library_synced = false;
    if (sd_saved && !flash_saved) storage_error("Profile saved on SD; flash backup unavailable");
    else if (!sd_saved && flash_saved) storage_error("Profile saved in flash; SD unavailable");
    else if (!sd_saved && !flash_saved) storage_error("Cannot write profile to SD or flash");
    else storage_error("");
    if (saved_to_sd) *saved_to_sd = sd_saved;
    if (stored_name && name_capacity > 0) snprintf(stored_name, name_capacity, "%s", candidate);
    STORE_UNLOCK();
    if (!sd_saved && !flash_saved) return "Cannot store profile on SD or flash";
    ESP_LOGI(TAG, "profile %s preserved (SD=%d flash backup=%d)", candidate, sd_saved, flash_saved);
    return NULL;
}

const char *config_store_save_template(const char *name, const char *data, int len, bool *saved_to_sd)
{
    return config_store_import(name, data, len, saved_to_sd, NULL, 0);
}

// ─── Seed the SD card from the bundled flash configs ─────────────────────────

int config_store_seed_sd(void)
{
    STORE_LOCK();
    bool sd = sd_mount();

    int copied = 0;
    char *buf = heap_caps_malloc(CFG_JSON_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool flash = spiffs_ready();
    DIR *dir = (sd && buf && flash) ? opendir(FLASH_ROOT) : NULL;
    bool synchronized = sd && buf && (!flash || dir);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            // Every bundled *.json travels to the card: the scenario
            // templates, the annotated schema doc, and the active config.
            const char *name = entry->d_name;
            size_t nlen = strlen(name);
            if (nlen < 5 || strcasecmp(name + nlen - 5, ".json") != 0) continue;

            char dst[96];
            snprintf(dst, sizeof(dst), SD_ROOT "/%s", name);
            FILE *probe = fopen(dst, "r");
            if (probe) { fclose(probe); continue; }   // never overwrite card copies

            char src[96];
            snprintf(src, sizeof(src), FLASH_ROOT "/%s", name);
            int len = read_whole_file(src, buf, CFG_JSON_MAX);
            if (len <= 0) continue;

            if (write_whole_file(dst, buf, len)) {
                ESP_LOGI(TAG, "seeded %s to SD card (%d bytes)", name, len);
                copied++;
            } else {
                ESP_LOGW(TAG, "failed to seed %s to SD card", name);
                synchronized = false;
            }
        }
        closedir(dir);
    }
    s_sd_library_synced = synchronized;

    if (copied)
        ESP_LOGI(TAG, "SD card seeded with %d bundled config file(s)", copied);
    int count; ConfigTemplate *catalog = config_store_list_all(&count);
    char *active = heap_caps_malloc(CFG_JSON_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // Older portal versions could save only an unnamed active file. Preserve
    // both stores if they differ, including when the SD card is unavailable.
    const char *roots[] = {SD_ROOT, FLASH_ROOT};
    for (int source = 0; buf && active && source < 2; ++source) {
        if ((source == 0 && !sd) || (source == 1 && !spiffs_ready())) continue;
        char path[96]; snprintf(path, sizeof(path), "%s/%s", roots[source], CFG_ACTIVE_NAME);
        int length = read_whole_file(path, active, CFG_JSON_MAX);
        if (length <= 0 || !config_json_valid(active)) continue;
        bool preserved = false;
        for (int i = 0; i < count && !preserved; ++i) {
            snprintf(path, sizeof(path), "%s/%s", catalog[i].from_sd ? SD_ROOT : FLASH_ROOT, catalog[i].name);
            int old_length = read_whole_file(path, buf, CFG_JSON_MAX);
            preserved = old_length == length && !memcmp(buf, active, length);
        }
        if (!preserved) {
            config_store_import("saved_active_config.json", active, length, NULL, NULL, 0);
            free(catalog); catalog = config_store_list_all(&count);
        }
    }
    free(active); free(buf); free(catalog);
    sd_unmount(); STORE_UNLOCK();
    return sd ? copied : -1;
}
