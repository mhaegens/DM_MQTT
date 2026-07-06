# SAP DM MQTT Simulator — ESP32-P4 Firmware

ESP-IDF firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (Smart 86 Box).
Displays a full shop-floor simulator UI on the 720×720 MIPI-DSI touch screen
and publishes MQTT telemetry to `wss://mqttfordm.haegens.be` via Cloudflare Tunnel.

---

## Prerequisites

### ESP-IDF v5.3+

```bash
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && git checkout v5.3 && ./install.sh esp32p4
source ~/esp/esp-idf/export.sh
```

### Fonts

The UI uses LVGL's built-in Montserrat family (16/18/22/24, enabled in
`sdkconfig.defaults`). The old pre-generated IBM Plex files were incompatible
with LVGL 9's font format and have been removed; if custom fonts are wanted
again, generate them with `lv_font_conv` **without** `--compressed`
(see [../docs/troubleshooting.md](../docs/troubleshooting.md), Issue 7).

---

## Boot Sequence

`app_main` runs in this order: NVS init, network subsystem init, PSRAM entity allocation (`data_init_psram`), JSON config load, BSP display start (LVGL render task pinned to CPU 1), root screen background color pre-set, UI build, WiFi init, sim task start, button and console tasks. MQTT starts only after WiFi gets an IP.

Full details: [docs/firmware-architecture.md](docs/firmware-architecture.md#boot-sequence).

---

## Build & Flash

```bash
cd firmware
idf.py set-target esp32p4
idf.py build
idf.py flash
idf.py monitor
```

The avoid-tear **direct-mode patch** for `esp_lvgl_port` is applied automatically
at configure time by the top-level `CMakeLists.txt` (look for
`esp_lvgl_port: applied direct-mode avoid-tear patch` in the configure output).
No manual patching after `idf.py update-dependencies` anymore.

---

## Configuration

Demo content is driven by `sapdm_config.json`, looked up in this order:

1. **SD card** — `/sdcard/sapdm_config.json` (an inserted card is an explicit override)
2. **SPIFFS** — `/spiffs/sapdm_config.json` (factory image + captive-portal uploads)
3. **Embedded default** — compiled into the firmware (`main/default_config.c`)

The captive portal (WiFi tab → Setup via Phone → Config) can upload a new
config or activate one of the bundled scenarios (`config_brewery.json`,
`config_bakery.json`, `config_ev_battery.json`). Uploads are validated
(parseable JSON with `machines`/`tanks`/`people`) and written to **both**
SPIFFS and the SD card when one is inserted, so the active store always
carries the latest upload. No SD card is required for any of this.

Schema reference: [`main/spiffs_image/sapdm_config.annotated.json`](main/spiffs_image/sapdm_config.annotated.json)

### WiFi

On first boot (or after SD config change) the device connects to the configured WiFi network.
A soft-AP hotspot can be toggled from the WiFi tab for provisioning. After connecting a phone
to the hotspot, a captive portal (with a DNS hijack responder on UDP/53) serves the credential
entry page. WiFi scanning uses a blocking scan in a worker task and reconnects the previous
network automatically when the scan finishes.

---

## Task Map

| Task | Stack | CPU | Wake |
|------|-------|-----|------|
| LVGL render (BSP) | 20 KB | 1 | 16ms period (direct mode — dirty regions only) |
| sim | 8 KB | 0 | 3000ms (`SIM_TICK_MS`) |
| btn | 4 KB | 0 | 20ms poll |
| con (console) | 2 KB | 0 | 50ms poll |
| MQTT internal (esp_mqtt) | esp_mqtt default | 0 | event-driven |

LVGL is on CPU 1. All other application tasks are on CPU 0. All MQTT publishes
go through `esp_mqtt_client_enqueue`, so no task ever blocks on the WSS socket.

## Project structure

```
main/
  main.c                 App entry — BSP, data, WiFi, MQTT, sim, UI init
  config.h               Compile-time defaults (broker URI, GPIO pins)
  config_loader.c/h      SD card JSON loader with embedded fallback
  default_config.json    Embedded default demo scenario
  data.c/h               All machine/tank/operator state in PSRAM
  mqtt_task.c/h          esp_mqtt client — WSS to Cloudflare tunnel
  sim_task.c/h           1.5 s random-walk simulation for all metrics
  wifi_manager.c/h       STA connect, scan, NVS credential storage, soft-AP
  ui/
    ui_theme.h            Design tokens — SAP Fiori Horizon colors + fonts
    ui_main.c/h           Shell bar, tab bar, screen transitions
    ui_home.c/h           Home: 4 KPI tiles + connection status
    ui_machines.c/h       Machines: scrollable grid, live metrics, controls
    ui_tanks.c/h          Tanks: wave animation, drag-to-adjust fill
    ui_nfc.c/h            People: badge carousel + physical NFC ring
    ui_detail.c/h         Detail: 6 viz types, step input, slider
    ui_wifi_setup.c/h     WiFi: scan list, join dialog, AP toggle, QR
    ui_debug.c/h          Debug: stat cards + rolling MQTT log
    ui_screenshot.c/h     UART screenshot tool ('S' key)
```

---

## Hardware notes

- **Display:** MIPI-DSI ST7703 720×720 — uses Waveshare BSP, not generic RGB
- **WiFi:** ESP32-C6 SDIO companion chip — do not use native P4 WiFi APIs
- **PSRAM:** machine/tank/badge arrays allocated in SPIRAM; draw buffer stays in internal SRAM. `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=81920` reserves 80 KB of internal SRAM for the SDIO DMA ring buffers used by the esp_hosted driver.
- **sdkconfig.defaults:** rationale for every non-obvious entry is in [docs/sdkconfig-reference.md](docs/sdkconfig-reference.md).
- **Double framebuffer:** `BSP_LCD_DPI_BUFFER_NUMS=2` + `AVOID_TEAR` + **direct mode** — both 720×720 framebuffers live in PSRAM, swapped on vsync; LVGL redraws only dirty regions. The required esp_lvgl_port patch is applied automatically by CMake.
- **Ghost scrolling:** the machines grid swaps its live widget trees for pre-rendered RGB565 images during scroll gestures, turning each scroll frame into a few memcpy blits instead of a full software repaint. Kill switch: `UI_SCROLL_GHOSTS` in `main/config.h`.
- **BOOT button:** GPIO35 — short press = back/home, long press = detail step
- **SD card:** SDIO 4-bit on GPIO39–44

---

## Known issues

For root causes and fixes: [../docs/troubleshooting.md](../docs/troubleshooting.md)

| # | Area | Description |
|---|------|-------------|
| 1 | Display | Occasional cyan frame flash on boot. Mitigated by pre-setting root screen background before UI init. |
| 2 | WiFi | iPhone captive portal auto-launch: the DNS responder now answers only A queries (AAAA/HTTPS get empty NOERROR), which was the suspected trigger for iOS distrust; needs a fresh on-device retest. Fallback: navigate to `http://192.168.4.1`. |
| 3 | Display | Direct-mode rendering + ghost scrolling need on-device validation: verify no tearing/cyan regressions and pixel-identical cards at scroll end. Reverts: `UI_SCROLL_GHOSTS 0` in `main/config.h`; for direct mode restore the full-refresh patch + `CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH=y` and set `timer_period_ms` back to 33. |

---

## Memory profile (post-init)

| Region   | Free      | Notes                                      |
|----------|-----------|--------------------------------------------|
| PSRAM    | ~7 MB     | Framebuffers + data arrays + hosted tasks  |
| Int SRAM | ~114 KB   | Reserve 80 KB for DMA (esp_hosted SDIO)    |
