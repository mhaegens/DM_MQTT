// wifi_portal.c - Captive portal: HTTP server at 192.168.4.1 for WiFi provisioning.
#include "wifi_portal.h"
#include "wifi_manager.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi_portal";

static httpd_handle_t s_server     = NULL;
static TimerHandle_t  s_stop_timer = NULL;

// ─── DNS hijack ───────────────────────────────────────────────────────────────

static TaskHandle_t s_dns_task = NULL;
static volatile bool s_dns_running = false;

static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(NULL); return; }

    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); vTaskDelete(NULL); return;
    }

    uint8_t buf[512];
    while (s_dns_running) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&src, &slen);
        if (n < 12) continue;   // too short to be a DNS query

        // Build response: copy header, flip QR bit, append answer
        uint8_t resp[512];
        memcpy(resp, buf, n);
        resp[2] |= 0x80;    // QR = response
        resp[3]  = 0x80;    // RA set, no error
        resp[6]  = 0;       // answer count high
        resp[7]  = 1;       // answer count low = 1

        int rlen = n;
        // Pointer back to the question name (offset 12)
        resp[rlen++] = 0xC0; resp[rlen++] = 0x0C;
        resp[rlen++] = 0x00; resp[rlen++] = 0x01;  // type A
        resp[rlen++] = 0x00; resp[rlen++] = 0x01;  // class IN
        resp[rlen++] = 0x00; resp[rlen++] = 0x00;
        resp[rlen++] = 0x00; resp[rlen++] = 0x3C;  // TTL 60s
        resp[rlen++] = 0x00; resp[rlen++] = 0x04;  // rdlength 4
        resp[rlen++] = 192; resp[rlen++] = 168;
        resp[rlen++] = 4;   resp[rlen++] = 1;      // 192.168.4.1

        sendto(sock, resp, rlen, 0, (struct sockaddr *)&src, slen);
    }

    close(sock);
    vTaskDelete(NULL);
}

static void dns_start(void)
{
    if (s_dns_task) return;
    s_dns_running = true;
    xTaskCreate(dns_task, "dns_hijack", 4096, NULL, 5, &s_dns_task);
    ESP_LOGI(TAG, "DNS hijack started");
}

static void dns_stop(void)
{
    if (!s_dns_task) return;
    s_dns_running = false;
    // Task exits its loop on next 200ms timeout and deletes itself
    s_dns_task = NULL;
    ESP_LOGI(TAG, "DNS hijack stopping");
}

// ─── Scan cache ───────────────────────────────────────────────────────────────

#define PORTAL_SCAN_MAX 20
static wifi_scan_entry_t s_cached_aps[PORTAL_SCAN_MAX];
static int               s_cached_ap_count = 0;

static void portal_scan_done_cb(const wifi_scan_entry_t *results, int count)
{
    s_cached_ap_count = count < PORTAL_SCAN_MAX ? count : PORTAL_SCAN_MAX;
    for (int i = 0; i < s_cached_ap_count; i++) s_cached_aps[i] = results[i];
    ESP_LOGI(TAG, "scan cache updated: %d APs", s_cached_ap_count);
}

// ─── Embedded HTML ────────────────────────────────────────────────────────────

static const char HTML_HEAD[] =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>SAP DM WiFi Setup</title>"
    "<style>"
    "body{font-family:sans-serif;background:#F2F4FA;color:#1D2D3E;"
    "padding:24px;max-width:400px;margin:auto}"
    "h1{color:#0070F2;font-size:20px;margin-bottom:4px}"
    "p{color:#4A6080;font-size:13px;margin-top:0}"
    "label{display:block;margin-top:16px;font-size:14px;font-weight:600}"
    "select,input{width:100%;padding:10px;margin-top:6px;"
    "border:1px solid #DDE1EA;border-radius:8px;font-size:15px;"
    "box-sizing:border-box;background:#fff}"
    "button{margin-top:24px;width:100%;padding:14px;background:#0070F2;"
    "color:#fff;border:none;border-radius:8px;font-size:16px;cursor:pointer}"
    "button:active{background:#0050BE}"
    "a.rescan{display:inline-block;margin-top:12px;color:#0070F2;font-size:13px}"
    "</style></head><body>"
    "<h1>SAP DM WiFi Setup</h1>"
    "<p>Select your network and enter the password.</p>"
    "<form method='post' action='/connect'>"
    "<label>Network<select name='ssid'>";

static const char HTML_TAIL[] =
    "</select></label>"
    "<label>Password"
    "<div style='position:relative'>"
    "<input type='password' name='password' id='pwd' "
    "placeholder='Leave empty for open networks'>"
    "<button type='button' onclick=\"var f=document.getElementById('pwd');"
    "f.type=f.type==='password'?'text':'password';"
    "this.textContent=f.type==='password'?'Show':'Hide'\" "
    "style='position:absolute;right:10px;top:50%;transform:translateY(-50%);"
    "background:none;border:none;color:#0070F2;font-size:13px;cursor:pointer;"
    "padding:0;width:auto;margin:0'>Show</button>"
    "</div></label>"
    "<button type='submit'>Connect</button>"
    "</form>"
    "<a class='rescan' href='/rescan'>Rescan networks</a>"
    "</body></html>";

static const char HTML_SCANNING[] =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>SAP DM WiFi Setup</title>"
    "<style>body{font-family:sans-serif;background:#F2F4FA;color:#1D2D3E;"
    "padding:40px;text-align:center}"
    "h1{color:#0070F2}p{color:#4A6080}"
    "a{color:#0070F2}</style></head><body>"
    "<h1>Scanning...</h1>"
    "<p>Network scan is in progress.<br>"
    "<a href='/'>Refresh this page</a> in a moment.</p>"
    "</body></html>";

static const char HTML_OK[] =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Connecting...</title>"
    "<style>body{font-family:sans-serif;background:#F2F4FA;color:#1D2D3E;"
    "padding:40px;text-align:center}"
    "h1{color:#107E3E}p{color:#4A6080}</style></head><body>"
    "<h1>Connecting...</h1>"
    "<p>The device is connecting to your network.<br>"
    "You can close this page.</p>"
    "</body></html>";

// ─── URL decode helper ────────────────────────────────────────────────────────

static void url_decode(char *dst, const char *src, int max)
{
    int i = 0;
    while (*src && i < max - 1) {
        if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// ─── URI handlers ─────────────────────────────────────────────────────────────

static esp_err_t get_root_handler(httpd_req_t *req)
{
    if (s_cached_ap_count == 0) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, HTML_SCANNING);
        return ESP_OK;
    }

    char opts[1200] = {0};
    int  pos = 0;
    for (int i = 0; i < s_cached_ap_count && pos < (int)sizeof(opts) - 120; i++) {
        pos += snprintf(opts + pos, sizeof(opts) - pos,
                        "<option value='%s'>%s (%d dBm)</option>",
                        s_cached_aps[i].ssid,
                        s_cached_aps[i].ssid,
                        s_cached_aps[i].rssi);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, HTML_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, opts, pos);
    httpd_resp_send_chunk(req, HTML_TAIL, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t get_rescan_handler(httpd_req_t *req)
{
    s_cached_ap_count = 0;
    wifi_manager_scan(portal_scan_done_cb);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

static esp_err_t post_connect_handler(httpd_req_t *req)
{
    char body[300] = {0};
    int  got = httpd_req_recv(req, body, sizeof(body) - 1);
    if (got <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[got] = '\0';

    char raw_ssid[64] = {0}, raw_pass[128] = {0};
    char *p;
    if ((p = strstr(body, "ssid=")) != NULL) {
        char tmp[64] = {0};
        sscanf(p + 5, "%63[^&]", tmp);
        url_decode(raw_ssid, tmp, sizeof(raw_ssid));
    }
    if ((p = strstr(body, "password=")) != NULL) {
        char tmp[128] = {0};
        sscanf(p + 9, "%127[^&]", tmp);
        url_decode(raw_pass, tmp, sizeof(raw_pass));
    }
    ESP_LOGI(TAG, "portal connect: ssid='%s'", raw_ssid);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, HTML_OK);

    wifi_manager_connect(raw_ssid, raw_pass, NULL);
    if (s_stop_timer) xTimerStart(s_stop_timer, 0);
    return ESP_OK;
}

static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

// ─── Stop timer callback ──────────────────────────────────────────────────────

static void stop_timer_cb(TimerHandle_t t)
{
    (void)t;
    wifi_portal_stop();
    wifi_manager_stop_ap();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void wifi_portal_start(void)
{
    if (s_server) return;

    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size       = 8192;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    static const httpd_uri_t root   = { .uri = "/",        .method = HTTP_GET,  .handler = get_root_handler };
    static const httpd_uri_t rescan = { .uri = "/rescan",  .method = HTTP_GET,  .handler = get_rescan_handler };
    static const httpd_uri_t conn   = { .uri = "/connect", .method = HTTP_POST, .handler = post_connect_handler };
    static const httpd_uri_t redir  = { .uri = "/*",       .method = HTTP_GET,  .handler = redirect_handler };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &rescan);
    httpd_register_uri_handler(s_server, &conn);
    httpd_register_uri_handler(s_server, &redir);

    s_stop_timer = xTimerCreate("portal_stop", pdMS_TO_TICKS(1500),
                                pdFALSE, NULL, stop_timer_cb);

    // Pre-populate scan cache immediately so the portal form is ready when phone connects
    s_cached_ap_count = 0;
    wifi_manager_scan(portal_scan_done_cb);

    dns_start();

    ESP_LOGI(TAG, "captive portal started at http://192.168.4.1/");
}

void wifi_portal_stop(void)
{
    if (!s_server) return;
    dns_stop();
    httpd_stop(s_server);
    s_server = NULL;
    if (s_stop_timer) {
        xTimerDelete(s_stop_timer, 0);
        s_stop_timer = NULL;
    }
    ESP_LOGI(TAG, "captive portal stopped");
}
