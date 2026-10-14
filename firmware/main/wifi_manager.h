#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wifi_known_networks.h"

#define WIFI_SCAN_MAX  20

typedef struct {
    char   ssid[33];
    int8_t rssi;
    bool   open;
    bool   saved;
} wifi_scan_entry_t;

typedef struct { char ssid[33]; bool open; } wifi_saved_entry_t;

typedef void (*wifi_scan_cb_t)(const wifi_scan_entry_t *results, int count);
typedef void (*wifi_connect_cb_t)(bool success, const char *ip);

void wifi_manager_init(void);
void wifi_manager_set_on_connected(void (*cb)(void));  // fires once on first IP acquired
void wifi_manager_set_on_ip(void (*cb)(void));          // fires on every IP acquisition
// Start an async scan; cb is delivered on the LVGL task. Returns false when a
// scan is already running (no callback will fire for this request).
bool wifi_manager_scan(wifi_scan_cb_t cb);
void wifi_manager_connect(const char *ssid, const char *password,
                          wifi_connect_cb_t cb);
void wifi_manager_forget(void);
// Copies names only; passwords never leave the manager through this API.
int wifi_manager_saved_networks(wifi_saved_entry_t *entries, int capacity);
bool wifi_manager_is_saved(const char *ssid);
bool wifi_manager_connect_saved(const char *ssid, wifi_connect_cb_t cb);
void wifi_manager_forget_network(const char *ssid);
int  wifi_manager_get_rssi(void);
void wifi_manager_start_ap(void);
void wifi_manager_stop_ap(void);
