// wifi_portal.c - Captive portal: HTTP server at 192.168.4.1 for WiFi provisioning.
#include "wifi_portal.h"
#include "wifi_manager.h"
#include "ui/ui_wifi_setup.h"
#include "config.h"
#include "config_loader.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/sockets.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static const char *TAG = "wifi_portal";

static httpd_handle_t s_server          = NULL;
static TimerHandle_t  s_stop_timer      = NULL;
static bool           s_portal_connecting = false;

// ─── Config store helpers ─────────────────────────────────────────────────────
// Active config lives in two places: SPIFFS (always present, factory image +
// uploads) and the SD card (external override — it outranks SPIFFS at boot,
// see config_loader.c). Every write targets both so whichever store wins at
// the next boot carries the latest upload.

#define PORTAL_SD_CONFIG_PATH     "/sdcard/sapdm_config.json"
#define PORTAL_SPIFFS_CONFIG_PATH "/spiffs/sapdm_config.json"
#define PORTAL_JSON_MAX           (32 * 1024)

static sdmmc_card_t *s_card = NULL;

// SPIFFS is registered (and left mounted) by config_load() at boot; re-register
// here only if that failed, e.g. after a flash erase without a SPIFFS image.
static bool portal_spiffs_ready(void)
{
    if (esp_spiffs_mounted("spiffs")) return true;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 4,
        .format_if_mount_failed = false,
    };
    return esp_vfs_spiffs_register(&conf) == ESP_OK;
}

static bool portal_sd_mount(void) {
    if (s_card) return true;
    sdmmc_host_t host        = SDMMC_HOST_DEFAULT();
    // CRITICAL: ESP-Hosted C6 Wi-Fi bridge owns SDMMC slot 1 (SDIO 4-bit).
    // SDMMC_HOST_DEFAULT() picks slot 1 on P4 by default — mounting the SD card
    // there yanks the Wi-Fi bus mid-transaction and panics. The BSP puts the
    // SD card on slot 0; both sockets have to be explicit.
    host.slot                = SDMMC_HOST_SLOT_0;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk   = GPIO_SD_CLK;
    slot.cmd   = GPIO_SD_CMD;
    slot.d0    = GPIO_SD_D0;
    slot.d1    = GPIO_SD_D1;
    slot.d2    = GPIO_SD_D2;
    slot.d3    = GPIO_SD_D3;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return false;
    }
    return true;
}

static void portal_sd_unmount(void) {
    if (!s_card) return;
    esp_vfs_fat_sdcard_unmount("/sdcard", s_card);
    s_card = NULL;
}

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

        // Parse QTYPE: skip the QNAME labels that follow the 12-byte header.
        // Phones probe with AAAA/HTTPS queries too — answering those with an
        // A record is malformed and makes iOS distrust the resolver, which
        // breaks the captive-portal auto-launch. Answer A queries with our
        // address and everything else with an empty NOERROR.
        int q = 12;
        while (q < n && buf[q] != 0) {
            if (buf[q] & 0xC0) { q++; break; }  // compression pointer
            q += buf[q] + 1;
        }
        q++;  // consume the terminating 0 (or the pointer's low byte)
        uint16_t qtype = 0;
        if (q + 1 < n) qtype = ((uint16_t)buf[q] << 8) | buf[q + 1];

        // Build response: copy the query, flip QR bit, append answer
        uint8_t resp[512];
        memcpy(resp, buf, n);
        resp[2] |= 0x80;    // QR = response
        resp[3]  = 0x80;    // RA set, no error
        resp[6]  = 0;       // answer count high
        int rlen = n;

        if (qtype == 1) {   // A query → answer with 192.168.4.1
            resp[7] = 1;    // answer count low = 1
            // Pointer back to the question name (offset 12)
            resp[rlen++] = 0xC0; resp[rlen++] = 0x0C;
            resp[rlen++] = 0x00; resp[rlen++] = 0x01;  // type A
            resp[rlen++] = 0x00; resp[rlen++] = 0x01;  // class IN
            resp[rlen++] = 0x00; resp[rlen++] = 0x00;
            resp[rlen++] = 0x00; resp[rlen++] = 0x3C;  // TTL 60s
            resp[rlen++] = 0x00; resp[rlen++] = 0x04;  // rdlength 4
            resp[rlen++] = 192; resp[rlen++] = 168;
            resp[rlen++] = 4;   resp[rlen++] = 1;      // 192.168.4.1
        } else {
            resp[7] = 0;    // no answers — NOERROR, query fails over to A
        }

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
    "<title>SAP DM Setup</title>"
    "<style>"
    "body{font-family:sans-serif;background:#F2F4FA;color:#1D2D3E;"
    "padding:24px;max-width:420px;margin:auto}"
    "h1{color:#0070F2;font-size:20px;margin-bottom:4px}"
    "p{color:#4A6080;font-size:13px;margin-top:0}"
    "label{display:block;margin-top:16px;font-size:14px;font-weight:600}"
    "select,input[type=password],input[type=text]{"
    "width:100%;padding:10px;margin-top:6px;"
    "border:1px solid #DDE1EA;border-radius:8px;font-size:15px;"
    "box-sizing:border-box;background:#fff}"
    "button.main{margin-top:24px;width:100%;padding:14px;background:#0070F2;"
    "color:#fff;border:none;border-radius:8px;font-size:16px;cursor:pointer}"
    "button.main:active{background:#0050BE}"
    "button.main.ok{background:#107E3E}"
    "button.main.ok:active{background:#0A5C2C}"
    "a.rescan{display:inline-block;margin-top:12px;color:#0070F2;font-size:13px}"
    ".tabs{display:flex;gap:2px;margin-bottom:20px;background:#E3E6EF;"
    "border-radius:8px;padding:3px}"
    ".tab{flex:1;padding:10px;border:none;border-radius:6px;background:transparent;"
    "font-size:14px;cursor:pointer;color:#4A6080;font-family:sans-serif}"
    ".tab.active{background:#fff;color:#0070F2;font-weight:600;box-shadow:0 1px 3px rgba(0,0,0,.1)}"
    ".pane{display:none}"
    "#dropzone{border:2px dashed #DDE1EA;border-radius:8px;padding:32px 16px;"
    "text-align:center;color:#8090A8;margin-top:16px;cursor:pointer;"
    "transition:all .15s}"
    "#dropzone.over{border-color:#0070F2;background:#E8F2FF;color:#0070F2}"
    "#dropzone.ready{border-color:#107E3E;background:#E3F5EC;color:#107E3E}"
    "#fname{margin-top:8px;font-size:13px;color:#4A6080}"
    "#upload-status{margin-top:12px;font-size:14px;padding:10px;border-radius:8px;"
    "display:none}"
    "#upload-status.ok{background:#E3F5EC;color:#107E3E;display:block}"
    "#upload-status.err{background:#FFE8E8;color:#BB0000;display:block}"
    "</style></head><body>"
    "<h1>SAP DM Setup</h1>"
    "<div class='tabs'>"
    "<button class='tab active' onclick=\"showTab('wifi',this)\">WiFi</button>"
    "<button class='tab' onclick=\"showTab('cfg',this)\">Config</button>"
    "</div>"
    "<div id='wifi' style='display:block'>"
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
    "<button type='submit' class='main'>Connect</button>"
    "</form>"
    "<a class='rescan' href='/rescan'>Rescan networks</a>"
    "</div>"
    "<div id='cfg' class='pane'>"
    "<p><strong>Available configs (flash + SD):</strong></p>"
    "<div id='cfgList'><em style='color:#8090A8'>Loading...</em></div>"
    "<div id='cfgStatus' style='margin-top:8px;font-size:13px'></div>"
    "<hr style='border:none;border-top:1px solid #DDE1EA;margin:16px 0'>"
    "<p>Upload a new config (saves as sapdm_config.json and reboots):</p>"
    "<div id='dropzone' onclick='document.getElementById(\"fInput\").click()'>"
    "Tap to select JSON file"
    "</div>"
    "<div id='fname'></div>"
    "<input type='file' id='fInput' accept='.json,application/json' "
    "style='display:none' onchange='filePicked(this)'>"
    "<button class='main ok' id='upBtn' onclick='doUpload()' "
    "style='display:none'>Upload &amp; Reboot</button>"
    "<div id='upload-status'></div>"
    "</div>"
    "<script>"
    "var _file=null;"
    "function showTab(id,btn){"
    "var ps=['wifi','cfg'];"
    "for(var i=0;i<ps.length;i++){"
    "var p=document.getElementById(ps[i]);"
    "if(p)p.style.display=(ps[i]===id)?'block':'none';}"
    "var ts=document.getElementsByClassName('tab');"
    "for(var i=0;i<ts.length;i++){"
    "ts[i].style.background=(ts[i]===btn)?'#fff':'transparent';"
    "ts[i].style.color=(ts[i]===btn)?'#0070F2':'#4A6080';"
    "ts[i].style.fontWeight=(ts[i]===btn)?'600':'normal';}"
    "if(id==='cfg')loadCfgList();}"
    "function loadCfgList(){"
    "var el=document.getElementById('cfgList');"
    "el.innerHTML='<em style=\"color:#8090A8\">Loading...</em>';"
    "var xhr=new XMLHttpRequest();"
    "xhr.open('GET','/list-configs',true);"
    "xhr.onreadystatechange=function(){"
    "if(xhr.readyState!==4)return;"
    "if(xhr.status===200){"
    "var files=JSON.parse(xhr.responseText);"
    "if(!files.length){el.innerHTML='<em>No config files found.</em>';return;}"
    "var html='';"
    "for(var i=0;i<files.length;i++){"
    "var f=files[i];"
    "var cls=f.active?'main ok':'main';"
    "var lbl=f.name+(f.src==='sd'?' [SD]':'')+(f.active?' (active)':'');"
    "html+='<button class=\"'+cls+'\" style=\"margin-top:8px\" "
    "onclick=\"setCfg(\\''+f.name+'\\',\\''+f.src+'\\')\">'+lbl+'</button>';}"
    "el.innerHTML=html;}"
    "else{el.innerHTML='<em style=\"color:#BB0000\">Storage not available.</em>';}};"
    "xhr.send();}"
    "function setCfg(name,src){"
    "var st=document.getElementById('cfgStatus');"
    "st.style.color='#4A6080';st.textContent='Setting...';"
    "var xhr=new XMLHttpRequest();"
    "xhr.open('POST','/set-config',true);"
    "xhr.setRequestHeader('Content-Type','application/json');"
    "xhr.onreadystatechange=function(){"
    "if(xhr.readyState!==4)return;"
    "if(xhr.status===200){"
    "st.style.color='#107E3E';"
    "st.textContent='Active config set to '+name+'. Reboot to apply.';"
    "loadCfgList();}"
    "else{st.style.color='#BB0000';st.textContent='Failed: '+xhr.responseText;}};"
    "xhr.send(JSON.stringify({file:name,src:src}));}"
    "function filePicked(inp){"
    "if(!inp.files.length)return;"
    "_file=inp.files[0];"
    "var dz=document.getElementById('dropzone');"
    "dz.style.borderColor='#107E3E';"
    "dz.style.background='#E3F5EC';"
    "dz.style.color='#107E3E';"
    "dz.textContent=_file.name+' ('+Math.round(_file.size/1024)+'KB)';"
    "document.getElementById('upBtn').style.display='block';"
    "document.getElementById('upload-status').style.display='none';}"
    "function doUpload(){"
    "if(!_file)return;"
    "var btn=document.getElementById('upBtn');"
    "btn.disabled=true;btn.textContent='Uploading...';"
    "var st=document.getElementById('upload-status');"
    "st.style.display='none';"
    "var xhr=new XMLHttpRequest();"
    "xhr.open('POST','/upload-config',true);"
    "xhr.setRequestHeader('Content-Type','application/json');"
    "xhr.onreadystatechange=function(){"
    "if(xhr.readyState!==4)return;"
    "if(xhr.status===200){"
    "st.style.display='block';st.style.background='#E3F5EC';"
    "st.style.color='#107E3E';st.textContent='Saved! Rebooting...';}"
    "else{"
    "st.style.display='block';st.style.background='#FFE8E8';"
    "st.style.color='#BB0000';st.textContent='Failed: '+xhr.responseText;"
    "btn.disabled=false;btn.textContent='Retry Upload';}};"
    "var reader=new FileReader();"
    "reader.onload=function(e){xhr.send(e.target.result);};"
    "reader.readAsText(_file);}"
    "</script>"
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

// ─── HTML escape helper ───────────────────────────────────────────────────────
// SSIDs are attacker-controlled (any nearby AP) and get embedded in the portal
// page — escape them so a hostile network name cannot inject markup.
static void html_escape(char *dst, const char *src, int max)
{
    int i = 0;
    for (; *src && i < max - 7; src++) {
        switch (*src) {
        case '&':  i += snprintf(dst + i, max - i, "&amp;");  break;
        case '<':  i += snprintf(dst + i, max - i, "&lt;");   break;
        case '>':  i += snprintf(dst + i, max - i, "&gt;");   break;
        case '"':  i += snprintf(dst + i, max - i, "&#34;");  break;
        case '\'': i += snprintf(dst + i, max - i, "&#39;");  break;
        default:   dst[i++] = *src;                           break;
        }
    }
    dst[i] = '\0';
}

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

    char opts[1600] = {0};
    int  pos = 0;
    for (int i = 0; i < s_cached_ap_count && pos < (int)sizeof(opts) - 260; i++) {
        char esc[33 * 6];   // worst case: every char escaped
        html_escape(esc, s_cached_aps[i].ssid, sizeof(esc));
        pos += snprintf(opts + pos, sizeof(opts) - pos,
                        "<option value='%s'>%s (%d dBm)</option>",
                        esc, esc, s_cached_aps[i].rssi);
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

    char status[72];
    snprintf(status, sizeof(status), "Connecting to %.48s...", raw_ssid);
    ui_wifi_setup_set_ap_status(status);
    s_portal_connecting = true;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, HTML_OK);

    wifi_manager_connect(raw_ssid, raw_pass, NULL);
    return ESP_OK;
}

static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

// ─── Active-config write (shared by /upload-config and /set-config) ──────────

static bool write_whole_file(const char *path, const char *data, int len)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    bool ok = (fwrite(data, 1, len, f) == (size_t)len);
    fclose(f);
    return ok;
}

// Persist a validated config blob as the active config in both stores.
// Returns NULL on success, or a static error string for the HTTP response.
static const char *save_active_config(const char *data, int len)
{
    bool spiffs_saved = portal_spiffs_ready() &&
                        write_whole_file(PORTAL_SPIFFS_CONFIG_PATH, data, len);

    bool sd_present = portal_sd_mount();
    bool sd_saved   = false;
    if (sd_present) {
        sd_saved = write_whole_file(PORTAL_SD_CONFIG_PATH, data, len);
        portal_sd_unmount();
    }

    // An inserted SD card outranks SPIFFS at boot — if it exists but could not
    // be written, the stale card config would mask this upload. Fail loudly.
    if (sd_present && !sd_saved) {
        return spiffs_saved
            ? "Saved to flash, but the inserted SD card failed to write - remove or replace the card, then retry"
            : "Cannot write config to SD card or flash";
    }
    if (!spiffs_saved && !sd_saved)
        return "Cannot save config (flash and SD card both unavailable)";

    ESP_LOGI(TAG, "active config saved (%d bytes, spiffs=%d sd=%d)",
             len, spiffs_saved, sd_saved);
    return NULL;
}

// ─── POST /upload-config ──────────────────────────────────────────────────────

static esp_err_t post_upload_config_handler(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > PORTAL_JSON_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_OK;
    }
    char *body = malloc(len + 1);
    if (!body) { httpd_resp_send_500(req); return ESP_OK; }

    int received = 0, ret;
    while (received < len) {
        ret = httpd_req_recv(req, body + received, len - received);
        if (ret <= 0) { free(body); httpd_resp_send_500(req); return ESP_OK; }
        received += ret;
    }
    body[received] = '\0';

    // Reject broken uploads before persisting anything — a bad file would
    // otherwise silently boot the device into the embedded default config.
    if (!config_json_valid(body)) {
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
            "Not a valid config (JSON with machines/tanks/people) - nothing saved");
        return ESP_OK;
    }

    const char *save_err = save_active_config(body, received);
    free(body);
    if (save_err) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, save_err);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "config uploaded (%d bytes) — rebooting", received);
    ui_wifi_setup_set_ap_status("Config saved — rebooting...");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
    return ESP_OK;
}

// ─── GET /list-configs ────────────────────────────────────────────────────────

#define LIST_BUF_CAP 2048

// Append every *.json in dirpath to the JSON array in buf as
// {"name":...,"src":...,"active":...}. Returns the new write position.
static int list_json_dir(const char *dirpath, const char *src_tag,
                         char *buf, int pos, bool *first)
{
    DIR *dir = opendir(dirpath);
    if (!dir) return pos;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // FAT VFS always returns DT_UNKNOWN — filter by .json extension only
        const char *name = entry->d_name;
        size_t nlen = strlen(name);
        if (nlen < 5 || strcasecmp(name + nlen - 5, ".json") != 0) continue;
        if (pos > LIST_BUF_CAP - 160) break;  // safety cap

        // The active config is the SPIFFS copy unless an SD card overrides it;
        // both copies are kept in sync by save_active_config, so tagging the
        // flash one is accurate either way.
        bool active = (strcmp(src_tag, "flash") == 0 &&
                       strcmp(name, "sapdm_config.json") == 0);
        if (!*first) buf[pos++] = ',';
        *first = false;
        pos += snprintf(buf + pos, LIST_BUF_CAP - pos,
                        "{\"name\":\"%s\",\"src\":\"%s\",\"active\":%s}",
                        name, src_tag, active ? "true" : "false");
    }
    closedir(dir);
    return pos;
}

static esp_err_t get_list_configs_handler(httpd_req_t *req)
{
    // Heap-allocate response buffer — stack is too precious (only ~17 KB internal SRAM free)
    char *buf = malloc(LIST_BUF_CAP);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    int  pos   = 0;
    bool first = true;
    buf[pos++] = '[';

    if (portal_spiffs_ready())
        pos = list_json_dir("/spiffs", "flash", buf, pos, &first);

    if (portal_sd_mount()) {
        pos = list_json_dir("/sdcard", "sd", buf, pos, &first);
        portal_sd_unmount();
    }

    buf[pos++] = ']';
    buf[pos]   = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    ESP_LOGI(TAG, "GET /list-configs: %d bytes", pos);
    free(buf);
    return ESP_OK;
}

// ─── POST /set-config ─────────────────────────────────────────────────────────

// Extract a quoted string value for `key` out of a flat JSON body.
// Returns length, or -1 when the key is absent / value too long.
static int json_get_str(const char *body, const char *key, char *out, int cap)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *k = strstr(body, pat);
    if (!k) return -1;
    const char *q1 = strchr(k + strlen(pat), '"');
    if (!q1) return -1;
    q1++;
    const char *q2 = strchr(q1, '"');
    if (!q2 || q2 - q1 >= cap) return -1;
    int len = (int)(q2 - q1);
    memcpy(out, q1, len);
    out[len] = '\0';
    return len;
}

static esp_err_t post_set_config_handler(httpd_req_t *req)
{
    char body[160] = {0};
    int got = httpd_req_recv(req, body, sizeof(body) - 1);
    if (got <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body"); return ESP_OK; }
    body[got] = '\0';

    // Body: {"file":"name.json","src":"flash"|"sd"} (src defaults to flash)
    char filename[64], src[8] = "flash";
    int flen = json_get_str(body, "file", filename, sizeof(filename));
    if (flen < 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing file field"); return ESP_OK; }
    json_get_str(body, "src", src, sizeof(src));
    bool from_sd = (strcmp(src, "sd") == 0);

    // Validate: must end in .json, no slashes, no ..
    if (flen < 5 || strcasecmp(filename + flen - 5, ".json") != 0 ||
        strchr(filename, '/') || strstr(filename, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_OK;
    }

    // Read the source file into RAM (≤32 KB), then persist it as the active
    // config in both stores via the same path the upload endpoint uses.
    char src_path[96];
    snprintf(src_path, sizeof(src_path), "%s/%s", from_sd ? "/sdcard" : "/spiffs", filename);

    if (from_sd && !portal_sd_mount()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD not available");
        return ESP_OK;
    }
    if (!from_sd && !portal_spiffs_ready()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash storage not available");
        return ESP_OK;
    }

    char *data = malloc(PORTAL_JSON_MAX);
    int   dlen = -1;
    if (data) {
        FILE *f = fopen(src_path, "r");
        if (f) {
            dlen = (int)fread(data, 1, PORTAL_JSON_MAX - 1, f);
            fclose(f);
            if (dlen >= PORTAL_JSON_MAX - 1) dlen = -2;  // too big — refuse truncation
            else data[dlen] = '\0';
        }
    }
    if (from_sd) portal_sd_unmount();

    if (!data) { httpd_resp_send_500(req); return ESP_OK; }
    if (dlen == -1) {
        free(data);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_OK;
    }
    if (dlen < 0 || !config_json_valid(data)) {
        free(data);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File is not a valid config");
        return ESP_OK;
    }

    const char *save_err = save_active_config(data, dlen);
    free(data);
    if (save_err) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, save_err);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "set-config: %s (%s) -> active config", filename, src);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// ─── Stop timer callback ──────────────────────────────────────────────────────

static void stop_timer_cb(TimerHandle_t t)
{
    (void)t;
    wifi_portal_stop();
    wifi_manager_stop_ap();
}

// ─── Portal WiFi event handler ────────────────────────────────────────────────

static void portal_event_handler(void *arg, esp_event_base_t base,
                                 int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ui_wifi_setup_set_ap_status("Phone connected");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && s_portal_connecting) {
        wifi_event_sta_disconnected_t *ev = data;
        if (ev->reason != WIFI_REASON_ASSOC_LEAVE) {
            s_portal_connecting = false;
            ui_wifi_setup_set_ap_status("Connection failed - try again");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && s_portal_connecting) {
        s_portal_connecting = false;
        ui_wifi_setup_set_ap_status("Connected!");
        // Stop the portal a moment after showing success
        if (s_stop_timer) xTimerStart(s_stop_timer, 0);
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void wifi_portal_start(void)
{
    if (s_server) return;

    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    // 20 KB — /list-configs and /set-config traverse FAT via readdir/fread,
    // which lands 6–8 KB of driver + DMA callback frames on top of our own.
    // The old 12 KB overflowed under interrupt nesting and crashed the device
    // as soon as the client hit "list configs on SD".
    cfg.stack_size        = 20480;
    cfg.max_uri_handlers  = 8;
    cfg.uri_match_fn      = httpd_uri_match_wildcard;
    cfg.lru_purge_enable  = true;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    static const httpd_uri_t root      = { .uri = "/",              .method = HTTP_GET,  .handler = get_root_handler };
    static const httpd_uri_t rescan    = { .uri = "/rescan",        .method = HTTP_GET,  .handler = get_rescan_handler };
    static const httpd_uri_t conn      = { .uri = "/connect",       .method = HTTP_POST, .handler = post_connect_handler };
    static const httpd_uri_t upload    = { .uri = "/upload-config", .method = HTTP_POST, .handler = post_upload_config_handler };
    static const httpd_uri_t list_cfg  = { .uri = "/list-configs",  .method = HTTP_GET,  .handler = get_list_configs_handler };
    static const httpd_uri_t set_cfg   = { .uri = "/set-config",    .method = HTTP_POST, .handler = post_set_config_handler };
    static const httpd_uri_t redir     = { .uri = "/*",             .method = HTTP_GET,  .handler = redirect_handler };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &rescan);
    httpd_register_uri_handler(s_server, &conn);
    httpd_register_uri_handler(s_server, &upload);
    httpd_register_uri_handler(s_server, &list_cfg);
    httpd_register_uri_handler(s_server, &set_cfg);
    httpd_register_uri_handler(s_server, &redir);

    s_stop_timer = xTimerCreate("portal_stop", pdMS_TO_TICKS(1500),
                                pdFALSE, NULL, stop_timer_cb);

    // Pre-populate scan cache immediately so the portal form is ready when phone connects
    s_cached_ap_count = 0;
    wifi_manager_scan(portal_scan_done_cb);

    dns_start();

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                portal_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                portal_event_handler, NULL);

    ui_wifi_setup_set_ap_status("Hotspot active - connect to " PROV_AP_SSID);

    ESP_LOGI(TAG, "captive portal started at http://192.168.4.1/");
}

void wifi_portal_stop(void)
{
    if (!s_server) return;
    dns_stop();
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                  portal_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                  portal_event_handler);
    httpd_stop(s_server);
    s_server = NULL;
    if (s_stop_timer) {
        xTimerDelete(s_stop_timer, 0);
        s_stop_timer = NULL;
    }
    ESP_LOGI(TAG, "captive portal stopped");
}
