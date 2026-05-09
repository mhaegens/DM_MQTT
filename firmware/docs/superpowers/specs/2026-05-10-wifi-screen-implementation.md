# WiFi Screen Implementation

## Context

The WiFi setup screen (`ui_wifi_setup.c`) was built as a UI skeleton in Phase 8 — all buttons log stubs to console, no backend calls are wired. The WiFi backend (`wifi_simple.c`) connects using hardcoded credentials from `config.h` with no scanning, no NVS persistence, and a placeholder RSSI of -55. This spec describes making both screen modes fully functional and replacing the stub backend with a proper WiFi manager.

## Decisions

- **Password entry**: Dedicated push screen (VIEW_WIFI_PASSWORD), full-height LVGL keyboard, network name in header, Connect button at bottom. Matches existing push/pop nav pattern.
- **Captive portal**: Scanned network list rendered as a `<select>` dropdown in the web form. ESP32 scans from AP mode before serving the page.
- **Scan screen layout**: Connected card at top (real SSID, real RSSI, IP, Forget button); Scan button + scrollable network list below.
- **Open networks**: Connect directly on tap — no password screen.
- **NVS persistence**: Credentials saved on every successful connect. Loaded on boot to auto-reconnect.
- **Backend split**: `wifi_manager.c` (STA + scan + NVS) + `wifi_portal.c` (soft-AP + HTTP server).

## Files

### Deleted
- `main/wifi_simple.c`

### New
| File | Purpose |
|---|---|
| `main/wifi_manager.c/.h` | STA connect, scan, NVS, RSSI, AP start/stop |
| `main/wifi_portal.c/.h` | Soft-AP HTTP server, scan-list web form, credential POST handler |
| `main/ui/ui_wifi_password.c/.h` | Password entry screen (VIEW_WIFI_PASSWORD = 9) |

### Modified
| File | Changes |
|---|---|
| `main/ui/ui_wifi_setup.c` | Wire all stub callbacks; add connected card; populate scan list; hotspot flow |
| `main/ui/ui_main.c/.h` | Add VIEW_WIFI_PASSWORD (= 9) to ViewId enum; add `ui_push_wifi_password(ssid)` |
| `main/data_model.h/.c` | Add `char g_wifi_ssid[33]`; populate in WiFi event handler |
| `main/main.c` | Replace `wifi_simple_init()` → `wifi_manager_init()` |

## wifi_manager.c

### Public API (`wifi_manager.h`)

```c
typedef struct {
    char  ssid[33];
    int8_t rssi;
    bool  open;          // true = no password required
} wifi_scan_entry_t;

typedef void (*wifi_scan_cb_t)(const wifi_scan_entry_t *results, int count);
typedef void (*wifi_connect_cb_t)(bool success, const char *ip);

void  wifi_manager_init(void);
void  wifi_manager_scan(wifi_scan_cb_t cb);
void  wifi_manager_connect(const char *ssid, const char *password, wifi_connect_cb_t cb);
void  wifi_manager_forget(void);
int   wifi_manager_get_rssi(void);
void  wifi_manager_start_ap(void);
void  wifi_manager_stop_ap(void);
```

### Behaviour

- **`wifi_manager_init()`**: Initialises ESP-IDF WiFi stack in `WIFI_MODE_STA`. Registers event handler (same logic as current `wifi_simple.c`). Loads NVS namespace `"wifi"`, keys `"ssid"` and `"pass"`. If found, copies SSID into `g_wifi_ssid` and calls `esp_wifi_connect()`.
- **`wifi_manager_scan(cb)`**: Calls `esp_wifi_scan_start(NULL, false)` (async). On `WIFI_EVENT_SCAN_DONE`, reads up to 20 APs via `esp_wifi_scan_get_ap_records()`, builds `wifi_scan_entry_t[]`, fires `cb` on the LVGL task via `lv_async_call`. Deduplicates hidden SSIDs (empty SSID string → skip).
- **`wifi_manager_connect(ssid, password, cb)`**: Saves credentials to NVS immediately. Sets `esp_wifi_config` and calls `esp_wifi_connect()`. On `IP_EVENT_STA_GOT_IP` fires `cb(true, ip)`; on `WIFI_EVENT_STA_DISCONNECTED` with reason != `ASSOC_LEAVE` fires `cb(false, NULL)`. Updates `g_wifi_ssid`, `g_wifi_rssi`, `g_device_ip`, calls `ui_notify_wifi_status()`.
- **`wifi_manager_forget()`**: Erases NVS keys, calls `esp_wifi_disconnect()`, clears `g_wifi_ssid` / `g_wifi_connected` / `g_device_ip`, calls `ui_notify_wifi_status(false, -99, "0.0.0.0")`.
- **`wifi_manager_get_rssi()`**: Calls `esp_wifi_sta_get_rssi()` when connected; returns `g_wifi_rssi` as fallback.
- **`wifi_manager_start_ap()`**: Switches to `WIFI_MODE_APSTA`. Configures AP with SSID `PROV_AP_SSID`, open auth, channel 6. Sets static IP `192.168.4.1`.
- **`wifi_manager_stop_ap()`**: Switches back to `WIFI_MODE_STA`.

### NVS

Namespace `"wifi"`, keys `"ssid"` (string, max 32) and `"pass"` (string, max 64). Uses `nvs_flash_init()` already called by `main.c`; manager opens its own handle per operation.

## wifi_portal.c

### Public API (`wifi_portal.h`)

```c
void wifi_portal_start(void);
void wifi_portal_stop(void);
```

### Behaviour

- **`wifi_portal_start()`**: Starts `esp_http_server` on port 80. Registers two URI handlers: `GET /` and `POST /connect`. Also registers catch-all redirect handler → `http://192.168.4.1/` (captive portal redirect for iOS/Android).
- **`GET /`**: Calls synchronous `esp_wifi_scan_start(NULL, true)` (blocking, safe from HTTP task). Builds an HTML page with inline CSS (dark theme, SAP blue accent) containing: device title, a `<select>` with found SSIDs, a password `<input type="password">`, and a Submit button. Page is a single self-contained string constant with a `%s` placeholder for the `<option>` list.
- **`POST /connect`**: Parses URL-encoded body for `ssid` and `password` fields (max 32/64 bytes). Calls `wifi_manager_connect(ssid, password, NULL)`. Serves a plain "Connecting to {ssid}…" response page. Schedules `wifi_portal_stop()` + `wifi_manager_stop_ap()` via a one-shot FreeRTOS timer after 1500 ms (gives the HTTP response time to flush).
- **`wifi_portal_stop()`**: Calls `httpd_stop()`, sets server handle to NULL.

## ui_wifi_password.c

### Screen

ViewId = 9 (`VIEW_WIFI_PASSWORD`). Pushed over `VIEW_WIFI_SETUP` via `ui_push_wifi_password(ssid)`.

### Layout

```
s_screen (flex column)
├── hdr (48 px) — breadcrumb bar reused; label = "← {ssid}"
└── body (flex column, gap=12, padding=16)
    ├── s_pwd_ta    — lv_textarea, password mode, placeholder "Password"
    ├── s_kbd       — lv_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, linked to s_pwd_ta
    └── s_connect_btn — "Connect to {ssid}", full width, COL_HI fill
```

### Behaviour

- `ui_push_wifi_password(ssid)` stores SSID in a static `s_pending_ssid[33]` buffer, updates the header label and connect button text, then pushes the screen.
- Connect button callback: reads `lv_textarea_get_text(s_pwd_ta)`, calls `wifi_manager_connect(s_pending_ssid, password, connect_result_cb)`. Disables button, sets label "Connecting…".
- `connect_result_cb(success, ip)` (called on LVGL task via `lv_async_call`): on success pops back to WiFi setup screen and triggers a scan refresh; on failure re-enables button, sets label "Failed — retry".
- Back arrow: `ui_pop_detail()` — clears textarea.

## ui_wifi_setup.c Changes

### Scan & Join mode

**Connected card** (new, top of scan panel):
- Shown when `g_wifi_connected == true`; hidden otherwise.
- Labels: SSID from `g_wifi_ssid`, RSSI from `wifi_manager_get_rssi()`, IP from `g_device_ip`.
- Forget button: calls `wifi_manager_forget()`, hides card, clears list.

**Scan button callback** (`scan_btn_cb`):
- Sets status label "Scanning…", disables button.
- Calls `wifi_manager_scan(scan_done_cb)`.

**`scan_done_cb(results, count)`** (called on LVGL task):
- Clears existing list children.
- For each entry: creates a row (`lv_obj`, flex row) with lock icon (`🔒` or `🔓`), SSID label, RSSI bar (4 blocks, coloured by signal strength).
- Row tap callback: if `entry.open` → call `wifi_manager_connect(ssid, "", connect_result_cb)`; else → `ui_push_wifi_password(ssid)`.
- Re-enables scan button, clears status label.

**`connect_result_cb`**: updates connected card labels on success; shows error in status label on failure.

### Setup via Phone mode

**Hotspot button callback** (`hotspot_btn_cb`):
- Calls `wifi_manager_start_ap()` then `wifi_portal_start()`.
- Changes button label to "■ Stop Hotspot", sets status label "Hotspot active — connect to SAP-DM-Setup".
- Second tap: calls `wifi_portal_stop()` + `wifi_manager_stop_ap()`, resets label and status.

Status label colours: `COL_OK` (green) when active, `COL_ERR` (red) on error, `COL_FG` (default) when inactive.

### `ui_wifi_setup_activate()`

Called when screen is pushed. Refreshes connected card from current `g_wifi_*` globals.

## data_model.h / data_model.c

Add:
```c
char g_wifi_ssid[33];   // currently connected SSID, "" when disconnected
```
Initialise to `""`. Populated by `wifi_manager.c` event handler alongside existing `g_wifi_connected`, `g_wifi_rssi`, `g_device_ip`.

## ui_main.h / ui_main.c

```c
// ui_main.h — add to ViewId enum
VIEW_WIFI_PASSWORD = 9,
VIEW_COUNT = 10,

// ui_main.h — add declaration
void ui_push_wifi_password(const char *ssid);
```

`ui_push_wifi_password()` follows the same pattern as `ui_push_wifi_setup()`: saves `g_prev_view`, hides all screens, shows `VIEW_WIFI_PASSWORD`, shows breadcrumb bar.

`g_screens[]` array and `ui_main_init()` call `ui_wifi_password_create()` alongside other screen creation calls.

## Verification

1. **Build**: `idf.py build` — zero new warnings, no other screens touched.
2. **Flash**: `idf.py flash monitor` — confirm boot log shows NVS load attempt and WiFi connect.
3. **Scan & Join flow**:
   - Open WiFi screen → connected card shows current SSID, real RSSI, real IP.
   - Tap "Scan Networks" → list populates with nearby SSIDs and signal bars.
   - Tap a secured network → password screen pushes with correct SSID in header.
   - Enter password, tap Connect → status transitions to "Connecting…" then success.
   - Reboot → device auto-connects to newly saved network (NVS verified).
   - Tap Forget → card disappears, device disconnects.
4. **Setup via Phone flow**:
   - Switch to "Setup via Phone" tab → tap "Start Hotspot".
   - Status label turns green: "Hotspot active — connect to SAP-DM-Setup".
   - Phone connects to `SAP-DM-Setup` AP, browses to `192.168.4.1`.
   - Web form shows scanned network dropdown.
   - Submit credentials → phone sees "Connecting…" page; ESP32 connects, AP stops.
5. **Screenshot**: `python3 tools/screenshot.py` after each flow to confirm visuals.
6. **Regression**: confirm Home, Machines, Tanks, Debug, NFC screens unaffected; MQTT reconnects after WiFi switch.
