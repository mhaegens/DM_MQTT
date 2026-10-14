#include "wifi_known_networks.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    wifi_known_store_t store, before;
    wifi_known_init(&store);
    assert(wifi_known_valid(&store) && store.count == 0);
    assert(wifi_known_remember(&store,"home","first-password"));
    assert(wifi_known_remember(&store,"office","second-password"));
    assert(store.count == 2 && !strcmp(store.networks[0].ssid,"office"));
    assert(wifi_known_find(&store,"home") == 1);
    assert(wifi_known_remember(&store,"home","first-password"));
    assert(store.count == 2 && !strcmp(store.networks[0].ssid,"home"));
    before = store;
    assert(!wifi_known_remember(&store,"home","first-password"));
    assert(!memcmp(&store,&before,sizeof(store))); // avoids repeated flash writes
    assert(wifi_known_remember(&store,"home","replacement-password"));
    assert(!strcmp(store.networks[0].password,"replacement-password"));
    assert(!strcmp(store.networks[1].password,"second-password"));
    for (int i = 0; i < 12; ++i) {
        char ssid[33]; snprintf(ssid,sizeof(ssid),"network-%d",i);
        assert(wifi_known_remember(&store,ssid,""));
    }
    assert(store.count == WIFI_KNOWN_MAX && wifi_known_find(&store,"network-0") == -1);
    assert(!strcmp(store.networks[0].ssid,"network-11"));
    assert(wifi_known_forget(&store,"network-7"));
    assert(store.count == 9 && wifi_known_find(&store,"network-7") == -1);
    assert(!wifi_known_forget(&store,"absent"));
    char full_ssid[33], full_password[65];
    memset(full_ssid,'s',32); full_ssid[32]=0;
    memset(full_password,'a',64); full_password[64]=0;
    assert(wifi_known_remember(&store,full_ssid,full_password));
    assert(wifi_known_valid(&store) && strlen(store.networks[0].ssid)==32 && strlen(store.networks[0].password)==64);
    before=store;
    assert(!wifi_known_remember(&store,"", ""));
    assert(!wifi_known_remember(&store,"123456789012345678901234567890123", ""));
    assert(!memcmp(&before,&store,sizeof(store)));
    store.count = WIFI_KNOWN_MAX+1; assert(!wifi_known_valid(&store));
    store=before; store.magic=0; assert(!wifi_known_valid(&store));
    store=before; memset(store.networks[0].password,'x',65); assert(!wifi_known_valid(&store));
    store=before; store.networks[1]=store.networks[0]; assert(!wifi_known_valid(&store));
    store=before;
    while(store.count) { char ssid[33]; memcpy(ssid,store.networks[0].ssid,33); assert(wifi_known_forget(&store,ssid)); }
    assert(wifi_known_valid(&store));
    puts("Known WiFi networks: persistence layout, multiple passwords, most-recent ordering, bounded eviction, forget, full-length credentials, validation and unchanged-write suppression passed");
}
