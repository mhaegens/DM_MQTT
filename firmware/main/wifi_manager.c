// wifi_manager.c — WiFi: STA connect/scan/NVS + soft-AP start/stop.
#include "wifi_manager.h"
#include "ui/data.h"
#include "ui/ui_main.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs.h"
#include "lwip/ip4_addr.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi_mgr";

#define NVS_NS        "wifi"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

static wifi_scan_cb_t    s_scan_cb       = NULL;
static wifi_connect_cb_t s_connect_cb    = NULL;
static bool              s_connecting    = false;
static char              s_pending_ssid[33] = {0};
static void            (*s_on_connected)(void) = NULL;
static esp_netif_t      *s_ap_netif      = NULL;

// ─── NVS helpers ─────────────────────────────────────────────────────────────

static bool nvs_load(char ssid[33], char pass[65])
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = 33, pl = 65;
    bool ok = (nvs_get_str(h, NVS_KEY_SSID, ssid, &sl) == ESP_OK &&
               nvs_get_str(h, NVS_KEY_PASS, pass, &pl) == ESP_OK);
    nvs_close(h);
    return ok;
}

static void nvs_save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "credentials saved for: %s", ssid);
}

static void nvs_clear_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
}

// ─── lv_async_call trampolines ────────────────────────────────────────────────

typedef struct { wifi_scan_cb_t cb; wifi_scan_entry_t *e; int n; } scan_async_t;

static void scan_dispatch(void *p)
{
    scan_async_t *a = (scan_async_t *)p;
    a->cb(a->e, a->n);
    free(a->e);
    free(a);
}

typedef struct { wifi_connect_cb_t cb; bool ok; char ip[16]; } conn_async_t;

static void conn_dispatch(void *p)
{
    conn_async_t *a = (conn_async_t *)p;
    a->cb(a->ok, a->ok ? a->ip : NULL);
    free(a);
}

// ─── Event handler ────────────────────────────────────────────────────────────

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = data;
        g_wifi_ssid[0] = '\0';
        ui_notify_wifi_status(false, -99, NULL);

        if (s_connecting && s_connect_cb &&
            (ev->reason == WIFI_REASON_AUTH_FAIL  ||
             ev->reason == WIFI_REASON_NO_AP_FOUND ||
             ev->reason == WIFI_REASON_ASSOC_FAIL)) {
            s_connecting = false;
            conn_async_t *a = malloc(sizeof(*a));
            a->cb = s_connect_cb;
            a->ok = false;
            s_connect_cb = NULL;
            lv_async_call(conn_dispatch, a);
        } else if (ev->reason != WIFI_REASON_ASSOC_LEAVE) {
            esp_wifi_connect();
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        char ip[16];
        esp_ip4addr_ntoa(&ev->ip_info.ip, ip, sizeof(ip));
        g_wifi_rssi = wifi_manager_get_rssi();
        strncpy(g_wifi_ssid, s_pending_ssid, sizeof(g_wifi_ssid) - 1);
        ui_notify_wifi_status(true, g_wifi_rssi, ip);
        ESP_LOGI(TAG, "IP: %s  SSID: %s", ip, g_wifi_ssid);

        if (s_connecting && s_connect_cb) {
            s_connecting = false;
            conn_async_t *a = malloc(sizeof(*a));
            a->cb = s_connect_cb;
            a->ok = true;
            strncpy(a->ip, ip, sizeof(a->ip) - 1);
            s_connect_cb = NULL;
            lv_async_call(conn_dispatch, a);
        }

        if (s_on_connected) {
            void (*cb)(void) = s_on_connected;
            s_on_connected = NULL;   // fire once
            cb();
        }

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t n = WIFI_SCAN_MAX;
        wifi_ap_record_t *recs = calloc(n, sizeof(*recs));
        if (!recs) return;
        esp_wifi_scan_get_ap_records(&n, recs);

        wifi_scan_entry_t *e = calloc(n + 1, sizeof(*e));
        if (!e) { free(recs); return; }
        int cnt = 0;
        for (int i = 0; i < (int)n; i++) {
            if (recs[i].ssid[0] == '\0') continue;
            strncpy(e[cnt].ssid, (char *)recs[i].ssid, 32);
            e[cnt].rssi = recs[i].rssi;
            e[cnt].open = (recs[i].authmode == WIFI_AUTH_OPEN);
            cnt++;
        }
        free(recs);

        if (s_scan_cb) {
            scan_async_t *a = malloc(sizeof(*a));
            if (!a) { free(e); return; }
            a->cb = s_scan_cb;
            a->e  = e;
            a->n  = cnt;
            s_scan_cb = NULL;
            lv_async_call(scan_dispatch, a);
        } else {
            free(e);
        }
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void wifi_manager_set_on_connected(void (*cb)(void)) { s_on_connected = cb; }

void wifi_manager_init(void)
{
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    char ssid[33] = {0}, pass[65] = {0};
    if (!nvs_load(ssid, pass)) {
        strncpy(ssid, DEFAULT_WIFI_SSID,     sizeof(ssid) - 1);
        strncpy(pass, DEFAULT_WIFI_PASSWORD, sizeof(pass) - 1);
        ESP_LOGI(TAG, "no NVS creds — using compile-time default: %s", ssid);
    } else {
        ESP_LOGI(TAG, "loaded NVS creds for: %s", ssid);
    }

    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);

    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid,     ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_manager_scan(wifi_scan_cb_t cb)
{
    s_scan_cb = cb;
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_wifi_scan_start(&sc, false);
}

void wifi_manager_connect(const char *ssid, const char *password,
                          wifi_connect_cb_t cb)
{
    nvs_save_creds(ssid, password);
    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    s_connect_cb = cb;
    s_connecting = (cb != NULL);

    esp_wifi_disconnect();

    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid,     ssid,     sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    esp_wifi_connect();
    ESP_LOGI(TAG, "connecting to: %s", ssid);
}

void wifi_manager_forget(void)
{
    nvs_clear_creds();
    esp_wifi_disconnect();
    g_wifi_ssid[0] = '\0';
    ui_notify_wifi_status(false, -99, NULL);
    ESP_LOGI(TAG, "credentials cleared");
}

int wifi_manager_get_rssi(void)
{
    int rssi = g_wifi_rssi;
    if (g_wifi_connected) esp_wifi_sta_get_rssi(&rssi);
    return rssi;
}

void wifi_manager_start_ap(void)
{
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap = {};
    strncpy((char *)ap.ap.ssid, PROV_AP_SSID, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len       = strlen(PROV_AP_SSID);
    ap.ap.channel        = 6;
    ap.ap.authmode       = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_LOGI(TAG, "soft-AP started: %s", PROV_AP_SSID);
}

void wifi_manager_stop_ap(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_LOGI(TAG, "soft-AP stopped");
}
