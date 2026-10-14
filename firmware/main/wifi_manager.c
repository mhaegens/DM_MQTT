// WiFi commands run in one worker; LVGL never waits for SDIO RPC or flash writes.
#include "wifi_manager.h"
#include "ui/data.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "ui/ui_dispatch.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi_mgr";
#define NVS_NS "wifi"
#define NVS_KEY_NETWORKS "networks_v1"
#define CONNECT_TIMEOUT_MS 15000
#define RETRY_CYCLE_MS 10000

typedef enum { CMD_START, CMD_CONNECT, CMD_SCAN, CMD_DISCONNECTED, CMD_IP,
               CMD_FORGET, CMD_AP_START, CMD_AP_STOP } command_kind_t;
typedef struct {
    command_kind_t kind;
    char ssid[33], password[65], ip[16];
    int reason;
    wifi_connect_cb_t connect_cb;
    wifi_scan_cb_t scan_cb;
} wifi_command_t;

static QueueHandle_t s_commands;
static SemaphoreHandle_t s_store_mutex;
static wifi_known_store_t s_known;
static bool s_store_dirty;
static _Atomic bool s_scan_running;
static esp_netif_t *s_ap_netif;
static void (*s_on_connected)(void);
static void (*s_on_ip)(void);

// Only the WiFi worker owns connection state and candidate credentials.
static wifi_known_network_t s_candidate;
static wifi_connect_cb_t s_connect_cb;
static bool s_attempt_active, s_manual, s_allow_default = true;
static int s_auto_index, s_attempts;
static int64_t s_deadline, s_next_cycle;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void load_networks(void)
{
    wifi_known_init(&s_known);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    wifi_known_store_t stored;
    size_t size = sizeof(stored);
    if (nvs_get_blob(h, NVS_KEY_NETWORKS, &stored, &size) == ESP_OK &&
        size == sizeof(stored) && wifi_known_valid(&stored)) {
        s_known = stored;
    } else {
        // Preserve the one remembered network from older firmware. New attempts
        // never overwrite it; migrate it on the next successful connection.
        char ssid[33] = {0}, password[65] = {0};
        size_t sl = sizeof(ssid), pl = sizeof(password);
        if (nvs_get_str(h, "ssid", ssid, &sl) == ESP_OK &&
            nvs_get_str(h, "pass", password, &pl) == ESP_OK && ssid[0]) {
            wifi_known_remember(&s_known, ssid, password);
            s_store_dirty = true;
        }
    }
    nvs_close(h);
    ESP_LOGI(TAG, "loaded %lu saved network(s)", (unsigned long)s_known.count);
}

static void persist_networks(void)
{
    wifi_known_store_t snapshot;
    xSemaphoreTake(s_store_mutex, portMAX_DELAY);
    snapshot = s_known;
    xSemaphoreGive(s_store_mutex);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY_NETWORKS, &snapshot, sizeof(snapshot));
        if (err == ESP_OK) {
            // A single blob is the authoritative store. Remove obsolete keys
            // in the same commit so Forget cannot resurrect a legacy network.
            nvs_erase_key(h, "ssid");
            nvs_erase_key(h, "pass");
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    s_store_dirty = err != ESP_OK;
    if (err != ESP_OK) ESP_LOGE(TAG, "could not save networks: %s", esp_err_to_name(err));
}

typedef struct { wifi_connect_cb_t cb; bool ok; char ip[16]; } connection_result_t;
static void connection_dispatch(void *arg)
{
    connection_result_t *r = arg;
    r->cb(r->ok, r->ok ? r->ip : NULL);
    free(r);
}

static void report_result(wifi_connect_cb_t cb, bool ok, const char *ip)
{
    if (!cb) return;
    connection_result_t *r = calloc(1, sizeof(*r));
    if (!r) return;
    r->cb = cb; r->ok = ok;
    if (ip) snprintf(r->ip, sizeof(r->ip), "%s", ip);
    // This runs in the worker, never in a touch callback or the event loop.
    for (int i = 0; i < 100; ++i) {
        if (ui_dispatch_call(connection_dispatch, r) == LV_RESULT_OK) return;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(r);
    ESP_LOGW(TAG, "UI callback queue remained full");
}

static void finish_manual(bool ok, const char *ip)
{
    wifi_connect_cb_t cb = s_connect_cb;
    s_connect_cb = NULL;
    report_result(cb, ok, ip);
}

static void begin_attempt(void)
{
    g_wifi_connected = false;
    g_wifi_ssid[0] = 0;
    snprintf(g_device_ip, sizeof(g_device_ip), "0.0.0.0");
    esp_wifi_disconnect();
    wifi_config_t cfg = {0};
    // Driver fields are fixed byte arrays: support the full 32-byte SSID and
    // 64-byte hexadecimal PSK without silently truncating their last byte.
    memcpy(cfg.sta.ssid, s_candidate.ssid, strlen(s_candidate.ssid));
    memcpy(cfg.sta.password, s_candidate.password, strlen(s_candidate.password));
    cfg.sta.threshold.authmode = s_candidate.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    s_attempt_active = true;
    s_deadline = now_ms() + CONNECT_TIMEOUT_MS;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) err = esp_wifi_connect();
    ESP_LOGI(TAG, "connecting to: %s", s_candidate.ssid);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "connect request failed: %s", esp_err_to_name(err));
        s_deadline = now_ms();
    }
}

static void try_next_saved(void)
{
    bool found = false;
    xSemaphoreTake(s_store_mutex, portMAX_DELAY);
    if (s_auto_index < (int)s_known.count) {
        s_candidate = s_known.networks[s_auto_index++];
        found = true;
    } else if (!s_known.count && s_allow_default && s_auto_index == 0 && DEFAULT_WIFI_SSID[0]) {
        memset(&s_candidate, 0, sizeof(s_candidate));
        snprintf(s_candidate.ssid, sizeof(s_candidate.ssid), "%s", DEFAULT_WIFI_SSID);
        snprintf(s_candidate.password, sizeof(s_candidate.password), "%s", DEFAULT_WIFI_PASSWORD);
        s_auto_index = 1;
        found = true;
    }
    xSemaphoreGive(s_store_mutex);
    s_manual = false;
    if (found) {
        s_attempts = 1;
        begin_attempt();
    } else {
        s_attempt_active = false;
        s_auto_index = 0;
        s_next_cycle = now_ms() + RETRY_CYCLE_MS;
    }
}

static void attempt_failed(bool skip_retry)
{
    if (s_manual) {
        finish_manual(false, NULL);
        s_manual = false;
        s_attempt_active = false;
        s_auto_index = 0;
        s_next_cycle = now_ms() + 1000;
    } else if (!skip_retry && s_attempts < 2) {
        ++s_attempts;
        begin_attempt();
    } else {
        try_next_saved();
    }
}

typedef struct { wifi_scan_cb_t cb; wifi_scan_entry_t *entries; int count; } scan_result_t;
static void scan_dispatch(void *arg)
{
    scan_result_t *r = arg;
    r->cb(r->entries, r->count);
    free(r->entries); free(r);
}

static void run_scan(wifi_scan_cb_t cb)
{
    bool resume = g_wifi_connected || s_attempt_active;
    // Serialize the C6 bridge's blocking scan with all connect commands.
    // Deliberate ASSOC_LEAVE events are ignored by the controller below.
    esp_wifi_disconnect();
    s_attempt_active = false;
    g_wifi_connected = false;
    vTaskDelay(pdMS_TO_TICKS(300));
    wifi_ap_record_t *records = calloc(WIFI_SCAN_MAX, sizeof(*records));
    uint16_t count = WIFI_SCAN_MAX;
    esp_err_t err = records ? esp_wifi_scan_start(NULL, true) : ESP_ERR_NO_MEM;
    if (records && err != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1200));
        err = esp_wifi_scan_start(NULL, true);
    }
    if (err != ESP_OK || esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) count = 0;
    wifi_scan_entry_t *entries = calloc(count ? count : 1, sizeof(*entries));
    int used = 0;
    if (entries) for (int i = 0; i < count; ++i) {
        if (!records[i].ssid[0]) continue;
        // Several access points may advertise the same SSID; show it once.
        bool duplicate = false;
        for (int j = 0; j < used; ++j)
            if (!strncmp(entries[j].ssid, (char *)records[i].ssid, 32)) duplicate = true;
        if (duplicate) continue;
        memcpy(entries[used].ssid, records[i].ssid, 32);
        entries[used].rssi = records[i].rssi;
        entries[used].open = records[i].authmode == WIFI_AUTH_OPEN;
        entries[used].saved = wifi_manager_is_saved(entries[used].ssid);
        ++used;
    }
    free(records);
    ESP_LOGI(TAG, "scan found %d network(s)", used);
    if (resume && s_candidate.ssid[0]) begin_attempt();
    else s_next_cycle = now_ms();
    atomic_store(&s_scan_running, false);
    scan_result_t *result = calloc(1, sizeof(*result));
    if (!result) { free(entries); return; }
    result->cb = cb; result->entries = entries; result->count = used;
    for (int i = 0; i < 100; ++i) {
        if (ui_dispatch_call(scan_dispatch, result) == LV_RESULT_OK) return;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(entries); free(result);
}

static void got_ip(const char *ip)
{
    wifi_ap_record_t ap;
    // Reject a queued IP event from a network we have already left.
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
        strncmp((char *)ap.ssid, s_candidate.ssid, 32)) return;
    g_wifi_rssi = ap.rssi;
    snprintf(g_device_ip, sizeof(g_device_ip), "%s", ip);
    snprintf(g_wifi_ssid, sizeof(g_wifi_ssid), "%s", s_candidate.ssid);
    g_wifi_connected = true;
    s_attempt_active = false;
    s_auto_index = 0;
    bool changed;
    xSemaphoreTake(s_store_mutex, portMAX_DELAY);
    changed = wifi_known_remember(&s_known, s_candidate.ssid, s_candidate.password);
    xSemaphoreGive(s_store_mutex);
    if (changed || s_store_dirty) persist_networks();
    ESP_LOGI(TAG, "IP: %s  SSID: %s", ip, g_wifi_ssid);
    finish_manual(true, ip);
    s_manual = false;
    if (s_on_connected) { void (*cb)(void) = s_on_connected; s_on_connected = NULL; cb(); }
    if (s_on_ip) s_on_ip();
}

static void forget_network(const char *ssid)
{
    bool changed;
    xSemaphoreTake(s_store_mutex, portMAX_DELAY);
    changed = wifi_known_forget(&s_known, ssid);
    xSemaphoreGive(s_store_mutex);
    if (changed) persist_networks();
    s_allow_default = false;
    if (!strcmp(s_candidate.ssid, ssid)) {
        esp_wifi_disconnect();
        finish_manual(false, NULL);
        memset(&s_candidate, 0, sizeof(s_candidate));
        g_wifi_connected = false; g_wifi_ssid[0] = 0;
        snprintf(g_device_ip, sizeof(g_device_ip), "0.0.0.0");
        s_attempt_active = false; s_manual = false;
        s_next_cycle = now_ms();
    }
    s_auto_index = 0;
    ESP_LOGI(TAG, "forgot network: %s", ssid);
}

static void wifi_worker(void *arg)
{
    (void)arg;
    wifi_command_t command;
    int64_t last_rssi_poll = 0;
    for (;;) {
        if (xQueueReceive(s_commands, &command, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (command.kind) {
            case CMD_START:
                if (!g_wifi_connected && !s_attempt_active) { s_auto_index = 0; try_next_saved(); }
                break;
            case CMD_CONNECT:
                finish_manual(false, NULL);
                snprintf(s_candidate.ssid, sizeof(s_candidate.ssid), "%s", command.ssid);
                snprintf(s_candidate.password, sizeof(s_candidate.password), "%s", command.password);
                s_connect_cb = command.connect_cb; s_manual = true; s_attempts = 1;
                begin_attempt();
                break;
            case CMD_SCAN: run_scan(command.scan_cb); break;
            case CMD_IP: got_ip(command.ip); break;
            case CMD_DISCONNECTED:
                if (command.reason == WIFI_REASON_ASSOC_LEAVE ||
                    (command.ssid[0] && strcmp(command.ssid, s_candidate.ssid))) break;
                g_wifi_connected = false; g_wifi_ssid[0] = 0;
                snprintf(g_device_ip, sizeof(g_device_ip), "0.0.0.0");
                if (!s_attempt_active) { s_auto_index = 0; try_next_saved(); }
                else attempt_failed(command.reason == WIFI_REASON_AUTH_FAIL ||
                                    command.reason == WIFI_REASON_NO_AP_FOUND);
                break;
            case CMD_FORGET: forget_network(command.ssid); break;
            case CMD_AP_START: {
                if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();
                wifi_config_t cfg = {0};
                memcpy(cfg.ap.ssid, PROV_AP_SSID, strlen(PROV_AP_SSID));
                cfg.ap.ssid_len = strlen(PROV_AP_SSID); cfg.ap.channel = 6;
                cfg.ap.authmode = WIFI_AUTH_OPEN; cfg.ap.max_connection = 4;
                esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
                if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
                if (err != ESP_OK) ESP_LOGE(TAG, "hotspot start failed: %s", esp_err_to_name(err));
                break;
            }
            case CMD_AP_STOP: esp_wifi_set_mode(WIFI_MODE_STA); break;
            }
        }
        if (g_wifi_connected && now_ms() - last_rssi_poll >= 5000) {
            int rssi;
            if (esp_wifi_sta_get_rssi(&rssi) == ESP_OK) g_wifi_rssi = rssi;
            last_rssi_poll = now_ms();
        }
        if (s_attempt_active && now_ms() >= s_deadline) {
            ESP_LOGW(TAG, "connection timed out: %s", s_candidate.ssid);
            attempt_failed(false);
        } else if (!g_wifi_connected && !s_attempt_active && now_ms() >= s_next_cycle) {
            try_next_saved();
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    wifi_command_t command = {0};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) command.kind = CMD_START;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        command.kind = CMD_DISCONNECTED; command.reason = event->reason;
        memcpy(command.ssid, event->ssid, event->ssid_len < 32 ? event->ssid_len : 32);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        command.kind = CMD_IP;
        esp_ip4addr_ntoa(&event->ip_info.ip, command.ip, sizeof(command.ip));
    } else return;
    // No network calls or NVS writes in ESP's shared event task.
    if (xQueueSend(s_commands, &command, 0) != pdTRUE)
        ESP_LOGW(TAG, "WiFi event queue full; timeout recovery remains active");
}

void wifi_manager_set_on_connected(void (*cb)(void)) { s_on_connected = cb; }
void wifi_manager_set_on_ip(void (*cb)(void)) { s_on_ip = cb; }

void wifi_manager_init(void)
{
    s_store_mutex = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(16, sizeof(wifi_command_t));
    if (!s_store_mutex || !s_commands) abort();
    load_networks();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // Our successful-connection store is authoritative, not the driver's
    // previously attempted (and potentially wrong) credentials.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // No candidate attempt until the driver's STA_START event.
    s_next_cycle = INT64_MAX;
    if (xTaskCreatePinnedToCore(wifi_worker, "wifi_ctrl", 6144, NULL, 4, NULL, 0) != pdPASS) abort();
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

bool wifi_manager_scan(wifi_scan_cb_t cb)
{
    bool expected = false;
    if (!cb || !s_commands || !atomic_compare_exchange_strong(&s_scan_running, &expected, true)) return false;
    wifi_command_t command = { .kind = CMD_SCAN, .scan_cb = cb };
    if (xQueueSend(s_commands, &command, 0) == pdTRUE) return true;
    atomic_store(&s_scan_running, false);
    return false;
}

void wifi_manager_connect(const char *ssid, const char *password, wifi_connect_cb_t cb)
{
    if (!ssid || !password || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 64) {
        // Public calls originate on LVGL or the portal; avoid callback waits.
        if (cb) {
            connection_result_t *r = calloc(1, sizeof(*r));
            if (r) { r->cb = cb; if (ui_dispatch_call(connection_dispatch, r) != LV_RESULT_OK) free(r); }
        }
        return;
    }
    wifi_command_t command = { .kind = CMD_CONNECT, .connect_cb = cb };
    snprintf(command.ssid, sizeof(command.ssid), "%s", ssid);
    snprintf(command.password, sizeof(command.password), "%s", password);
    if (!s_commands || xQueueSend(s_commands, &command, 0) != pdTRUE) {
        connection_result_t *r = calloc(1, sizeof(*r));
        if (r && cb) { r->cb = cb; if (ui_dispatch_call(connection_dispatch, r) != LV_RESULT_OK) free(r); }
        else free(r);
        ESP_LOGW(TAG, "connection request queue full");
    }
}

int wifi_manager_saved_networks(wifi_saved_entry_t *entries, int capacity)
{
    if (!s_store_mutex || !entries || capacity <= 0) return 0;
    xSemaphoreTake(s_store_mutex, portMAX_DELAY);
    int count = (int)s_known.count < capacity ? (int)s_known.count : capacity;
    for (int i = 0; i < count; ++i) {
        snprintf(entries[i].ssid, sizeof(entries[i].ssid), "%s", s_known.networks[i].ssid);
        entries[i].open = !s_known.networks[i].password[0];
    }
    xSemaphoreGive(s_store_mutex);
    return count;
}

bool wifi_manager_is_saved(const char *ssid)
{
    if (!s_store_mutex) return false;
    xSemaphoreTake(s_store_mutex, portMAX_DELAY);
    bool known = wifi_known_find(&s_known, ssid) >= 0;
    xSemaphoreGive(s_store_mutex);
    return known;
}

bool wifi_manager_connect_saved(const char *ssid, wifi_connect_cb_t cb)
{
    if (!s_store_mutex) return false;
    wifi_known_network_t network;
    xSemaphoreTake(s_store_mutex, portMAX_DELAY);
    int index = wifi_known_find(&s_known, ssid);
    if (index >= 0) network = s_known.networks[index];
    xSemaphoreGive(s_store_mutex);
    if (index < 0) return false;
    wifi_manager_connect(network.ssid, network.password, cb);
    return true;
}

void wifi_manager_forget_network(const char *ssid)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !s_commands) return;
    wifi_command_t command = { .kind = CMD_FORGET };
    snprintf(command.ssid, sizeof(command.ssid), "%s", ssid);
    if (xQueueSend(s_commands, &command, 0) != pdTRUE) ESP_LOGW(TAG, "forget request queue full");
}

void wifi_manager_forget(void) { wifi_manager_forget_network(g_wifi_ssid); }
int wifi_manager_get_rssi(void) { return g_wifi_rssi; }
void wifi_manager_start_ap(void)
{
    wifi_command_t command = { .kind = CMD_AP_START };
    if (xQueueSend(s_commands, &command, 0) != pdTRUE) ESP_LOGW(TAG, "hotspot request queue full");
}
void wifi_manager_stop_ap(void)
{
    wifi_command_t command = { .kind = CMD_AP_STOP };
    if (xQueueSend(s_commands, &command, 0) != pdTRUE) ESP_LOGW(TAG, "hotspot request queue full");
}
