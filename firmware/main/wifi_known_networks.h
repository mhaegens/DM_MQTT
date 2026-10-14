#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WIFI_KNOWN_MAX 10
#define WIFI_KNOWN_MAGIC UINT32_C(0x57494631)

typedef struct { char ssid[33]; char password[65]; } wifi_known_network_t;
typedef struct {
    uint32_t magic;
    uint32_t count;
    wifi_known_network_t networks[WIFI_KNOWN_MAX];
} wifi_known_store_t;

void wifi_known_init(wifi_known_store_t *store);
bool wifi_known_valid(const wifi_known_store_t *store);
int wifi_known_find(const wifi_known_store_t *store, const char *ssid);
// Call only after successful IP acquisition. Most recent success comes first.
bool wifi_known_remember(wifi_known_store_t *store, const char *ssid, const char *password);
bool wifi_known_forget(wifi_known_store_t *store, const char *ssid);
