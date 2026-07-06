# Firmware Architecture

Deep-dive reference for the ESP32-P4 firmware. The [quick-start guide](../README.md) covers build and flash. This document covers the internal structure: boot sequence, task architecture, data layer, and how to extend the codebase.

---

## Boot Sequence

`app_main` in `main/main.c` runs these steps in order. Order matters: each step depends on the previous one.

1. **NVS flash init** (`nvs_flash_init`) — required before WiFi can load stored credentials. Erases and reinitializes NVS if the partition was previously formatted with a different layout.

2. **Network subsystem init** (`esp_netif_init`, `esp_event_loop_create_default`) — must come before WiFi, MQTT, or anything that posts to the default event loop.

3. **PSRAM entity allocation** (`data_init_psram`) — allocates `g_machines`, `g_tanks`, and `g_badges` arrays in SPIRAM using `heap_caps_malloc(MALLOC_CAP_SPIRAM)`. This must happen before `config_load` because `config_load` calls `populate_from_json`, which writes directly into these arrays.

4. **Config load** (`config_load`) — tries SPIFFS, then SD card, then falls back to the embedded default. Parses the JSON into the pre-allocated arrays.

5. **BSP display start** (`bsp_display_start_with_config`) — initializes the MIPI-DSI panel, allocates two PSRAM framebuffers, starts the LVGL port with the render task pinned to CPU 1, 16ms period, 20KB task stack. Rendering runs in direct mode: only dirty regions are redrawn (see [sdkconfig-reference.md](sdkconfig-reference.md#display)).

6. **Root screen background** — sets `lv_scr_act()` background to `0xF2F4FA` before the UI builds. This prevents the cyan flash from PSRAM garbage on the first frame.

7. **UI init** (`ui_main_init`) — builds the shell bar, tab bar, and all screen containers. Calls `ui_*_create` for each tab. Must run inside `bsp_display_lock/unlock`.

8. **WiFi init** (`wifi_manager_init`, `wifi_manager_set_on_ip`) — starts WiFi STA, loads NVS credentials, and registers the `on_wifi_ip` callback. MQTT does not start until this callback fires.

9. **Sim task start** (`sim_task_start`) — starts the 1.5s simulator task on CPU 0. Starts immediately; MQTT publishes only when `g_mqtt_connected` is true.

10. **Button and console tasks** (`xTaskCreatePinnedToCore`) — `btn_task` polls GPIO35 every 20ms, `console_task` polls stdin every 50ms for the screenshot trigger.

11. **Memory diagnostics** — logs PSRAM and internal SRAM free sizes. Warns if internal SRAM drops below 100 KB.

12. **Idle loop** — `app_main` enters a 1s delay loop. All work happens in the spawned tasks and the LVGL render task.

---

## Task Map

| Task | Stack | Priority | CPU | Wake |
|------|-------|----------|-----|------|
| LVGL render (BSP) | 20 KB | BSP default | 1 | 16ms period (direct mode) |
| sim | 8 KB | 5 | 0 | `SIM_TICK_MS` (3000ms) `vTaskDelay` |
| btn | 4 KB | 5 | 0 | 20ms `vTaskDelay` |
| con (console) | 2 KB | 3 | 0 | 50ms `vTaskDelay` |
| MQTT internal (esp_mqtt) | esp_mqtt default | esp_mqtt default | 0 | event-driven |
| esp_hosted SDIO tasks | SPIRAM | — | — | SDIO interrupt |

LVGL is on CPU 1. All other application tasks are on CPU 0. This keeps MQTT reconnect loops and WiFi events from stalling the display.

The LVGL task stack is set to 20 KB in `main.c`:

```c
cfg.lvgl_port_cfg.task_stack = 20480;
```

The default BSP stack of ~7 KB is not enough for `ui_machines_create`, which builds a deep LVGL object hierarchy with nested flex containers and multiple custom widgets.

---

## Data Layer

### Structs (defined in `ui/data.h`)

All entity data is accessed through three global pointer arrays allocated in PSRAM:

```c
extern Machine *g_machines;  // up to MAX_MACHINES (10)
extern Tank    *g_tanks;     // up to MAX_TANKS (3)
extern Badge   *g_badges;    // up to MAX_BADGES (12)
extern int     g_machine_count, g_tank_count, g_badge_count;
```

The `Metric` struct holds both definition data (key, label, unit, viz type, thresholds, sim profile) and runtime state (current value, history ring buffer of 32 entries, sine phase accumulator).

### Thread safety

`g_machines` and `g_tanks` are written by `sim_task` on CPU 0 and read by the LVGL render task on CPU 1. All access must be wrapped:

```c
DATA_LOCK();
// read or write g_machines / g_tanks
DATA_UNLOCK();
```

`DATA_LOCK()` and `DATA_UNLOCK()` are macros that call `xSemaphoreTake` and `xSemaphoreGive` on `g_data_mutex`. Using these macros (not the semphr functions directly) ensures consistent usage across the codebase.

`sim_task` holds the lock during the full `sim_tick()` call. UI refresh functions must acquire the lock before reading metric values.

### Global status flags

Several flags in `data.h` are written by tasks and read by the UI without the data mutex (they are `bool`/`int`, and reads are atomic on this architecture):

```c
extern bool  g_mqtt_connected;
extern int   g_mqtt_msg_count;
extern bool  g_wifi_connected;
extern char  g_wifi_ssid[33];
extern int   g_wifi_rssi;
extern char  g_device_ip[16];
extern char  g_last_pub_topic[128];
```

---

## Simulation Engine

`sim_task.c` runs a `SIM_TICK_MS` (3-second) tick loop on CPU 0:

```c
while (1) {
    DATA_LOCK();
    sim_tick();
    DATA_UNLOCK();
    if (mqtt_is_connected()) publish_all();
    vTaskDelay(pdMS_TO_TICKS(SIM_TICK_MS));
}
```

Each tick calls `sim_metric` for every metric of every running machine and every tank, except:

- tank level metrics (`metrics[0]`) — these only change via drag or the fill/drain action
- the metric currently open in the detail view (`g_edit_machine`/`g_edit_tank`/`g_edit_metric`) — the user owns it while editing; the hold is released by `ui_main` on any navigation away from the detail view

**Simulation profiles:**

| Profile | Behavior |
|---------|----------|
| `SIM_RANDOM_WALK` | Adds a random delta in `[-step, +step]`, clamps to `[rand_min, rand_max]` |
| `SIM_SINE` | `value = offset + amplitude * sin(phase_acc)`, phase increments by `2π/period_s * 1.5` per tick |
| `SIM_RAMP` | Adds `step` per tick, wraps at `max_val` back to `min_val` |
| `SIM_STEADY` | No change, no `metric_push` call (history stays flat) |

The pulse counter for running machines increments every 5 ticks (~7.5 seconds). `mqtt_pub_machine_pulse` is called on each increment.

---

## MQTT Client

`mqtt_task.c` initializes an `esp_mqtt` client with WSS transport and `disable_auto_reconnect = true`.

All publishes go through `esp_mqtt_client_enqueue` (never `esp_mqtt_client_publish`): publish performs a synchronous TLS/WSS write in the caller's context, which stalled the LVGL render task when publishes were triggered from touch callbacks (tank drags, Start/Stop taps). Enqueue hands the send to the MQTT task via the outbox. The message counter (`g_mqtt_msg_count`) is incremented on successful enqueue — `MQTT_EVENT_PUBLISHED` never fires for QoS 0.

The client registers a retained last-will on `factory/device/status` (`offline`), and publishes retained `online` on every connect, so the dashboard can distinguish a dead device from a quiet one.

On `MQTT_EVENT_DISCONNECTED`, a staged exponential backoff timer fires:
- Disconnect 1: retry after 5 seconds
- Disconnect 2: retry after 10 seconds
- Disconnect 3+: retry after 30 seconds

On `MQTT_EVENT_CONNECTED`, the disconnect counter resets to 0.

All UI updates from the MQTT event handler use `lv_async_call` to dispatch a small heap-allocated struct to the LVGL render task:

```c
typedef struct { bool connected; } mqtt_status_async_t;

static void mqtt_status_dispatch(void *p) {
    mqtt_status_async_t *a = (mqtt_status_async_t *)p;
    ui_notify_mqtt_status(a->connected);
    free(a);
}
```

The `lv_async_call` pattern ensures `ui_notify_mqtt_status` runs on the LVGL task, not the MQTT event handler task.

---

## WiFi Manager

`wifi_manager.c` handles three scenarios:

1. **STA connect:** reads SSID and password from NVS (namespace `wifi`, keys `ssid` and `pass`). Falls back to compile-time defaults in `config.h` if NVS has no stored credentials.

2. **Soft-AP:** starts a hotspot named `SAP-DM-Setup`. A captive portal HTTP server runs on `192.168.4.1` (port 80) and serves a credential entry form. After the user submits credentials, they are saved to NVS and the device reconnects in STA mode.

3. **WiFi scan:** calls `esp_wifi_scan_start`. Due to a known issue with the ESP32-C6 SDIO path, the `WIFI_EVENT_SCAN_DONE` event is not reliably delivered. See [troubleshooting.md](../../docs/troubleshooting.md#issue-3-wifi-scan-returns-no-results).

WiFi event callbacks (connect, disconnect, scan done, IP acquired) all use `lv_async_call` trampolines to update the UI, because WiFi events fire on a different task than the LVGL render task.

The `on_wifi_ip` callback in `main.c` is called when `WIFI_EVENT_STA_GOT_IP` fires. It starts the MQTT client on the first IP event, and calls `mqtt_task_reconnect` on subsequent IP events (after WiFi reconnects).

---

## Config Loader

`config_loader.c` tries config sources in this order:

1. SPIFFS: mounts the SPIFFS partition and reads `/spiffs/sapdm_config.json`.
2. SD card: mounts SDIO 4-bit SD and reads `/sdcard/sapdm_config.json` (fallback if SPIFFS file is absent).
3. Embedded default: uses the `DEFAULT_CONFIG_JSON` string from `default_config.c`.

The cJSON library is used for parsing. To avoid exhausting internal SRAM, the cJSON allocator is overridden to use PSRAM:

```c
static void *psram_malloc(size_t sz) {
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
```

The cJSON tree is allocated in PSRAM for the duration of parsing, then freed after `populate_from_json` copies all values into the pre-allocated arrays.

`config_load` returns a `ConfigResult` enum value. `main.c` logs the result string on boot:

| Result | Meaning |
|--------|---------|
| `CONFIG_SPIFFS_OK` | SPIFFS config loaded |
| `CONFIG_OK` | SD card config loaded |
| `CONFIG_SD_NOT_FOUND` | No SD card, using default |
| `CONFIG_FILE_NOT_FOUND` | SD card present but no `sapdm_config.json` |
| `CONFIG_PARSE_ERROR` | JSON invalid, using default |
| `CONFIG_USING_DEFAULT` | Explicit default fallback |

---

## UI Architecture

### View system

Six view IDs are defined in `ui_main.h`:

```c
typedef enum {
    VIEW_HOME = 0,
    VIEW_MACHINES,
    VIEW_TANKS,
    VIEW_DEBUG,
    VIEW_NFC,
    VIEW_WIFI,
    VIEW_DETAIL,
    VIEW_TANK_OVW,
    VIEW_COUNT
} ViewId;
```

`g_content` is a full-screen container that is a permanent child of `lv_scr_act()`, alongside the shell bar and tab bar. Each screen module creates its content as a child of `g_content`. Navigation switches which child is visible by setting `LV_OBJ_FLAG_HIDDEN`.

### Navigation functions

| Function | Effect |
|----------|--------|
| `ui_switch_tab(ViewId)` | Switches to a top-level tab, calls deactivate on the previous tab and activate on the new one |
| `ui_push_detail(machine_idx, metric_idx)` | Navigates to the detail view for a specific metric |
| `ui_pop_detail()` | Returns from detail view to the previous tab |

### Activate / deactivate callbacks

Some screens start or stop timers when they are shown or hidden. Adding an activate/deactivate hook requires:

1. Implementing `ui_foo_activate(void)` and `ui_foo_deactivate(void)` in `ui_foo.c`.
2. Declaring them in `ui_main.c` as forward declarations.
3. Adding calls to both in the `ui_switch_tab` dispatch block.

Currently implemented for: `ui_tanks` (wave animation timer), `ui_debug` (refresh timer).

### Shell bar and tab bar

The shell bar (top) and tab bar (bottom) are built inside `bsp_display_lock/unlock` in `ui_main_init` and are permanent children of `lv_scr_act()`. They are never hidden or recreated. Screen content replaces only the `g_content` area.

### Theme tokens

`ui/ui_theme.h` defines all color and typography tokens as `lv_color_hex()` macros:

```c
#define COL_HI      lv_color_hex(0x0070F2)   // SAP blue
#define COL_OK      lv_color_hex(0x107E3E)   // green
#define COL_WARN    lv_color_hex(0xE76500)   // orange
#define COL_ERR     lv_color_hex(0xBB0000)   // red
#define COL_BG      lv_color_hex(0xF2F4FA)   // Fiori background
```

These mirror the CSS custom properties in the HTML dashboard (`--col-hi`, `--col-ok`, etc.) to maintain visual parity between the device UI and the browser dashboard.

---

## Screenshot Tool

Press `S` over the UART console (the `console_task` polls stdin every 50ms) to trigger `ui_screenshot_take()` in `ui_screenshot.c`. This uses the LVGL snapshot API to capture the current screen state and dumps raw RGB565 pixel data over UART.

Read from the host:

```bash
python3 tools/screenshot.py
# or with a specific port:
python3 tools/screenshot.py --port /dev/tty.usbserial-XXXX
```

Two `sdkconfig` settings are required:

1. `CONFIG_LV_USE_SNAPSHOT=y` — enables the LVGL snapshot API.
2. `CONFIG_NEWLIB_STDOUT_LINE_ENDING_LF=y` — disables CRLF translation. Without this, the `\n` bytes in the binary pixel data stream are silently converted to `\r\n`, corrupting the image.

---

## How to Add a New Screen

1. Create `main/ui/ui_foo.c` and `main/ui/ui_foo.h`.
2. Implement `void ui_foo_create(lv_obj_t *parent)`. Build all LVGL objects as children of `parent`.
3. Add `VIEW_FOO` to the `ViewId` enum in `ui_main.h`, before `VIEW_COUNT`.
4. Add a forward declaration `void ui_foo_create(lv_obj_t *parent);` near the top of `ui_main.c`.
5. Add icon and label to `TAB_ICONS[]` and `TAB_LABELS[]` in `ui_main.c`.
6. Call `ui_foo_create(g_content)` from `ui_main_init`, inside the display lock.
7. Add the view to the `ui_switch_tab` dispatch block.
8. If the screen uses a timer: implement `ui_foo_activate` and `ui_foo_deactivate`, add them as forward declarations in `ui_main.c`, and call them from the `ui_switch_tab` dispatch block.
9. Add `ui_foo.c` to `main/CMakeLists.txt` `SRCS` list.
