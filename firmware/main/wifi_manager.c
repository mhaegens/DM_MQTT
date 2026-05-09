// wifi_manager.c — stub, full implementation added in Task 3
#include "wifi_manager.h"
void wifi_manager_init(void) {}
void wifi_manager_scan(wifi_scan_cb_t cb) { (void)cb; }
void wifi_manager_connect(const char *ssid, const char *password, wifi_connect_cb_t cb) { (void)ssid; (void)password; (void)cb; }
void wifi_manager_forget(void) {}
int  wifi_manager_get_rssi(void) { return -99; }
void wifi_manager_start_ap(void) {}
void wifi_manager_stop_ap(void) {}
