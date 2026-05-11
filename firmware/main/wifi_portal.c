// wifi_portal.c — Captive portal: HTTP server at 192.168.4.1 for WiFi provisioning.
#include "wifi_portal.h"
#include "wifi_manager.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi_portal";

static httpd_handle_t s_server     = NULL;
static TimerHandle_t  s_stop_timer = NULL;

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
    "</style></head><body>"
    "<h1>SAP DM WiFi Setup</h1>"
    "<p>Select your network and enter the password.</p>"
    "<form method='post' action='/connect'>"
    "<label>Network<select name='ssid'>";

static const char HTML_TAIL[] =
    "</select></label>"
    "<label>Password"
    "<input type='password' name='password' "
    "placeholder='Leave empty for open networks'></label>"
    "<button type='submit'>Connect</button>"
    "</form></body></html>";

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
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_wifi_scan_start(&sc, true);

    uint16_t count = 20;
    wifi_ap_record_t recs[20];
    esp_wifi_scan_get_ap_records(&count, recs);

    char opts[1200] = {0};
    int  pos = 0;
    for (int i = 0; i < (int)count && pos < (int)sizeof(opts) - 120; i++) {
        if (recs[i].ssid[0] == '\0') continue;
        pos += snprintf(opts + pos, sizeof(opts) - pos,
                        "<option value='%s'>%s (%d dBm)</option>",
                        (char *)recs[i].ssid,
                        (char *)recs[i].ssid,
                        recs[i].rssi);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, HTML_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, opts, pos);
    httpd_resp_send_chunk(req, HTML_TAIL, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
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
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    static const httpd_uri_t root  = { .uri = "/",        .method = HTTP_GET,  .handler = get_root_handler };
    static const httpd_uri_t conn  = { .uri = "/connect",  .method = HTTP_POST, .handler = post_connect_handler };
    static const httpd_uri_t redir = { .uri = "/*",        .method = HTTP_GET,  .handler = redirect_handler };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &conn);
    httpd_register_uri_handler(s_server, &redir);

    s_stop_timer = xTimerCreate("portal_stop", pdMS_TO_TICKS(1500),
                                pdFALSE, NULL, stop_timer_cb);

    ESP_LOGI(TAG, "captive portal started at http://192.168.4.1/");
}

void wifi_portal_stop(void)
{
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
    if (s_stop_timer) {
        xTimerDelete(s_stop_timer, 0);
        s_stop_timer = NULL;
    }
    ESP_LOGI(TAG, "captive portal stopped");
}
