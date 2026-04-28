// wifi_simple.c — Phase 3: connect to WiFi using compile-time credentials.
// Phase 8 replaces this with wifi_prov_init() (captive portal).
#include "config.h"
#include "ui/ui_main.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include <string.h>

static const char *TAG = "wifi";

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_connected = false;
        ui_notify_wifi_status(false, -99, NULL);
        esp_wifi_connect();   // auto-reconnect
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        char ip_str[16];
        esp_ip4addr_ntoa(&ev->ip_info.ip, ip_str, sizeof(ip_str));
        g_wifi_connected = true;
        g_wifi_rssi      = -55;  // placeholder; use esp_wifi_sta_get_rssi() for real value
        ui_notify_wifi_status(true, g_wifi_rssi, ip_str);
        ESP_LOGI(TAG, "IP: %s", ip_str);
    }
}

void wifi_simple_init(void) {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    wifi_config_t wcfg = {};
    strncpy((char *)wcfg.sta.ssid,     DEFAULT_WIFI_SSID,
            sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, DEFAULT_WIFI_PASSWORD,
            sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "connecting to %s…", DEFAULT_WIFI_SSID);
}
