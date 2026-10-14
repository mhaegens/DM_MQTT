// wifi_portal.c - Captive portal: HTTP server at 192.168.4.1 for WiFi provisioning.
#include "wifi_portal.h"
#include "wifi_manager.h"
#include "ui/ui_wifi_setup.h"
#include "config.h"
#include "config_loader.h"
#include "config_store.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static const char *TAG = "wifi_portal";

static httpd_handle_t s_server          = NULL;
static TimerHandle_t  s_stop_timer      = NULL;
static bool           s_portal_connecting = false;

// SPIFFS/SD access and active-config persistence live in config_store.c —
// shared with the on-device Configuration screen so both behave identically.
#define PORTAL_JSON_MAX           (32 * 1024)

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
    // Keep stale results rather than none: an empty result is almost always a
    // scan that raced AP bring-up or channel switching, not an empty ether.
    if (count == 0 && s_cached_ap_count > 0) {
        ESP_LOGW(TAG, "scan returned 0 APs — keeping %d cached", s_cached_ap_count);
        return;
    }
    s_cached_ap_count = count < PORTAL_SCAN_MAX ? count : PORTAL_SCAN_MAX;
    for (int i = 0; i < s_cached_ap_count; i++) s_cached_aps[i] = results[i];
    ESP_LOGI(TAG, "scan cache updated: %d APs", s_cached_ap_count);
}

// Kick a scan; wifi_manager serializes concurrent requests. Called from portal
// start, the first phone association, every load of the "Scanning..." page and
// /rescan — so one failed or empty scan can never strand the portal on the
// scanning screen (the page auto-refreshes and each load retries).
static void portal_kick_scan(void)
{
    if (wifi_manager_scan(portal_scan_done_cb))
        ESP_LOGI(TAG, "portal scan started");
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
    "<p>Select a network. Saved networks connect without entering the password again.</p>"
    "<form method='post' action='/connect'>"
    "<label>Network<select name='ssid'>";

static const char HTML_TAIL[] =
    "</select></label>"
    "<label>Password"
    "<div style='position:relative'>"
    "<input type='password' name='password' id='pwd' "
    "placeholder='Leave empty for saved or open networks'>"
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
    "<p>Add a profile to your permanent library. Choose whether to use it now:</p>"
    "<div id='dropzone' onclick='document.getElementById(\"fInput\").click()'>"
    "Tap to select JSON file"
    "</div>"
    "<div id='fname'></div>"
    "<input type='file' id='fInput' accept='.json,application/json' "
    "style='display:none' onchange='filePicked(this)'>"
    "<button class='main ok' id='upBtn' onclick='doUpload()' "
    "style='display:none'>Save profile</button>"
    "<label><input type='checkbox' id='activateUpload' checked style='width:auto'> Use this profile now and restart</label>"
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
    "el.textContent='';"
    "files.forEach(function(f){"
    "var b=document.createElement('button');"
    "b.className=f.active?'main ok':'main';b.style.marginTop='8px';"
    "b.textContent=f.name+(f.src==='sd'?' [SD]':' [Flash]')+(f.active?' (active)':'')+(f.valid===false?' - Needs attention':'');"
    "b.disabled=f.valid===false;b.onclick=function(){setCfg(f.name,f.src);};"
    "el.appendChild(b);});}"
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
    "document.getElementById('upBtn').style.display='block';document.getElementById('upBtn').disabled=false;document.getElementById('upBtn').textContent='Save profile';"
    "document.getElementById('upload-status').style.display='none';}"
    "function doUpload(){"
    "if(!_file)return;"
    "var btn=document.getElementById('upBtn');"
    "btn.disabled=true;btn.textContent='Uploading...';"
    "var st=document.getElementById('upload-status');"
    "st.style.display='none';"
    "var xhr=new XMLHttpRequest();"
    "xhr.open('POST','/upload-config?name='+encodeURIComponent(_file.name)+'&activate='+(document.getElementById('activateUpload').checked?'1':'0'),true);"
    "xhr.setRequestHeader('Content-Type','application/json');"
    "xhr.onreadystatechange=function(){"
    "if(xhr.readyState!==4)return;"
    "if(xhr.status===200){"
    "st.style.display='block';st.style.background='#E3F5EC';"
    "st.style.color='#107E3E';var result=JSON.parse(xhr.responseText);st.textContent='Saved as '+result.name+' on '+result.storage+(result.activated?'. Restarting...':'. Kept in your profile library.');loadCfgList();}"
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
    "<meta http-equiv='refresh' content='4'>"
    "<title>SAP DM WiFi Setup</title>"
    "<style>body{font-family:sans-serif;background:#F2F4FA;color:#1D2D3E;"
    "padding:40px;text-align:center}"
    "h1{color:#0070F2}p{color:#4A6080}"
    "a{color:#0070F2}</style></head><body>"
    "<h1>Scanning...</h1>"
    "<p>Network scan is in progress.<br>"
    "This page refreshes automatically.<br>"
    "<a href='/'>Refresh now</a></p>"
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

static void send_network_option(httpd_req_t *req, const char *ssid, bool saved, int rssi)
{
    char escaped[33 * 6], option[480];
    html_escape(escaped, ssid, sizeof(escaped));
    int length = snprintf(option, sizeof(option),
                          "<option value='%s'>%s%s (%d dBm)</option>",
                          escaped, escaped, saved ? " - Saved" : "", rssi);
    httpd_resp_send_chunk(req, option, length < (int)sizeof(option) ? length : sizeof(option) - 1);
}

static esp_err_t get_root_handler(httpd_req_t *req)
{
    wifi_saved_entry_t saved[WIFI_KNOWN_MAX] = {0};
    int saved_count = wifi_manager_saved_networks(saved, WIFI_KNOWN_MAX);
    if (s_cached_ap_count == 0) {
        portal_kick_scan();
        if (!saved_count) {
            httpd_resp_set_type(req, "text/html");
            httpd_resp_sendstr(req, HTML_SCANNING);
            return ESP_OK;
        }
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, HTML_HEAD, HTTPD_RESP_USE_STRLEN);
    for (int i = 0; i < s_cached_ap_count; ++i)
        send_network_option(req, s_cached_aps[i].ssid,
                            wifi_manager_is_saved(s_cached_aps[i].ssid), s_cached_aps[i].rssi);
    for (int i = 0; i < saved_count; ++i) {
        bool visible = false;
        for (int j = 0; j < s_cached_ap_count; ++j)
            if (!strcmp(saved[i].ssid, s_cached_aps[j].ssid)) visible = true;
        if (!visible) send_network_option(req, saved[i].ssid, true, -99);
    }
    httpd_resp_send_chunk(req, HTML_TAIL, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t get_rescan_handler(httpd_req_t *req)
{
    s_cached_ap_count = 0;
    portal_kick_scan();
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

    if (raw_pass[0] || !wifi_manager_connect_saved(raw_ssid, NULL))
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

    // Keep the upload forever as a named template (?name=<original filename>,
    // SD card preferred) so it stays in the profile list across switches...
    char raw_name[CFG_TPL_NAME_MAX] = {0};
    char query[160];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char enc[128];
        if (httpd_query_key_value(query, "name", enc, sizeof(enc)) == ESP_OK)
            url_decode(raw_name, enc, sizeof(raw_name));
    }
    char tpl_name[CFG_TPL_NAME_MAX];
    config_store_sanitize_tpl_name(raw_name, tpl_name, sizeof(tpl_name));

    bool saved_to_sd = false, activate = true;
    char activation[8], stored_name[CFG_TPL_NAME_MAX];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "activate", activation, sizeof(activation)) == ESP_OK &&
        !strcmp(activation, "0")) activate = false;
    const char *save_err = config_store_import(tpl_name, body, received, &saved_to_sd,
                                              stored_name, sizeof(stored_name));

    // ...and make it the active config for the reboot below.
    if (!save_err && activate) save_err = config_store_save_active(body, received);
    free(body);
    if (save_err) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, save_err);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "profile imported as %s (%d bytes), SD=%d activate=%d", stored_name, received, saved_to_sd, activate);
    ui_wifi_setup_set_ap_status(activate ? "Profile saved - rebooting..." : "Profile saved in your library");
    char response[180];
    snprintf(response, sizeof(response), "{\"ok\":true,\"name\":\"%s\",\"storage\":\"%s\",\"activated\":%s}",
             stored_name, saved_to_sd ? "sd" : "flash", activate ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    if (activate) { vTaskDelay(pdMS_TO_TICKS(600)); esp_restart(); }
    else { int count; ConfigTemplate *all = config_store_list_all(&count); free(all); }

    return ESP_OK;
}

// ─── GET /list-configs ────────────────────────────────────────────────────────

static esp_err_t get_list_configs_handler(httpd_req_t *req)
{
    int count = 0;
    ConfigTemplate *profiles = config_store_list_all(&count);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "[", 1);
    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) break;
        cJSON_AddStringToObject(item, "name", profiles[i].name);
        cJSON_AddStringToObject(item, "src", profiles[i].from_sd ? "sd" : "flash");
        cJSON_AddBoolToObject(item, "active", profiles[i].active);
        cJSON_AddBoolToObject(item, "valid", profiles[i].valid);
        char *json = cJSON_PrintUnformatted(item);
        cJSON_Delete(item);
        if (!json) break;
        if (i) httpd_resp_send_chunk(req, ",", 1);
        httpd_resp_send_chunk(req, json, strlen(json));
        free(json);
    }
    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);
    free(profiles);
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

    // config_store_activate validates the filename, reads + validates the
    // file, and persists it to both stores — same path the on-device
    // Configuration screen uses.
    const char *err = config_store_activate(filename, from_sd);
    if (err) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err);
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
        // The AP is demonstrably up now. The scan kicked at portal start races
        // AP bring-up on the C6 and often comes back empty — this is the
        // reliable moment to retry it, before the phone loads the page.
        if (s_cached_ap_count == 0) portal_kick_scan();
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
    portal_kick_scan();

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
