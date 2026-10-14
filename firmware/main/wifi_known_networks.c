#include "wifi_known_networks.h"
#include <string.h>

void wifi_known_init(wifi_known_store_t *s)
{
    memset(s, 0, sizeof(*s));
    s->magic = WIFI_KNOWN_MAGIC;
}

bool wifi_known_valid(const wifi_known_store_t *s)
{
    if (!s || s->magic != WIFI_KNOWN_MAGIC || s->count > WIFI_KNOWN_MAX) return false;
    for (uint32_t i = 0; i < s->count; ++i) {
        if (!s->networks[i].ssid[0] || !memchr(s->networks[i].ssid, 0, 33) ||
            !memchr(s->networks[i].password, 0, 65)) return false;
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(s->networks[i].ssid, s->networks[j].ssid)) return false;
    }
    return true;
}

int wifi_known_find(const wifi_known_store_t *s, const char *ssid)
{
    if (!s || !ssid) return -1;
    for (uint32_t i = 0; i < s->count; ++i)
        if (!strcmp(s->networks[i].ssid, ssid)) return (int)i;
    return -1;
}

bool wifi_known_remember(wifi_known_store_t *s, const char *ssid, const char *password)
{
    if (!wifi_known_valid(s) || !ssid || !password || !ssid[0] ||
        strlen(ssid) > 32 || strlen(password) > 64) return false;
    int index = wifi_known_find(s, ssid);
    if (index == 0 && !strcmp(s->networks[0].password, password)) return false;
    wifi_known_network_t entry = {0};
    memcpy(entry.ssid, ssid, strlen(ssid));
    memcpy(entry.password, password, strlen(password));
    uint32_t move;
    if (index >= 0) move = (uint32_t)index;
    else {
        if (s->count < WIFI_KNOWN_MAX) ++s->count;
        move = s->count - 1;
    }
    memmove(&s->networks[1], &s->networks[0], move * sizeof(entry));
    s->networks[0] = entry;
    return true;
}

bool wifi_known_forget(wifi_known_store_t *s, const char *ssid)
{
    if (!wifi_known_valid(s)) return false;
    int index = wifi_known_find(s, ssid);
    if (index < 0) return false;
    --s->count;
    memmove(&s->networks[index], &s->networks[index + 1],
            (s->count - (uint32_t)index) * sizeof(s->networks[0]));
    memset(&s->networks[s->count], 0, sizeof(s->networks[0]));
    return true;
}
