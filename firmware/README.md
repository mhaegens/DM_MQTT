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

### Font files

Pre-generated IBM Plex Sans + IBM Plex Mono font files are included in `fonts/`. To regenerate:

```bash
npm install -g lv_font_conv
bash generate_fonts.sh
```

---

## Build & Flash

```bash
cd firmware
idf.py set-target esp32p4
idf.py build
idf.py flash
idf.py monitor
```

---

## Configuration

Demo content is driven by a JSON file on the SD card: `/config/demo.json`

If no SD card is present the firmware falls back to an embedded default config (`main/default_config.json`).

### Config structure

```json
{
  "mqtt_broker": "wss://mqttfordm.haegens.be/mqtt",
  "wifi_ssid": "YourNetwork",
  "wifi_password": "secret",
  "machines": [...],
  "tanks": [...],
  "operators": [...]
}
```

Full schema and example: [`main/default_config.json`](main/default_config.json)

### WiFi

On first boot (or after SD config change) the device connects to the configured WiFi network.
A soft-AP hotspot can be toggled from the WiFi tab for provisioning. After connecting a phone
to the hotspot, a captive portal serves the credential entry page.

> **Known issue:** iPhone captive portal detection completes but the portal page does not
> always load. WiFi scanning from the WiFi tab also has a known issue. Both are tracked below.

---

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
- **PSRAM:** machine/tank/badge arrays allocated in SPIRAM; draw buffer stays in internal SRAM
- **Double framebuffer:** `BSP_LCD_DPI_BUFFER_NUMS=2` + `AVOID_TEAR` + `FULL_REFRESH` — both 720×720 framebuffers live in PSRAM, swapped on vsync
- **BOOT button:** GPIO35 — short press = back/home, long press = detail step
- **SD card:** SDIO 4-bit on GPIO39–44

---

## Known issues

| # | Area | Description |
|---|------|-------------|
| 1 | Display | Occasional cyan frame flash under LVGL full-refresh + avoid-tearing mode. Root cause: LVGL port does not pre-initialise PSRAM framebuffers before first render. |
| 2 | WiFi | Soft-AP hotspot visible on iPhone but captive portal page does not load — DHCP or DNS redirect issue on the ESP32-C6 SDIO path. |
| 3 | WiFi | WiFi scan from the WiFi tab returns no results — scan callback not firing over the SDIO hosted path. |
| 4 | Machines | Vertical scroll on the machines tab is sluggish. Suspected cause: LVGL full-refresh mode repaints the entire 720×720 framebuffer on every scroll tick even for a small dirty region. |

---

## Memory profile (post-init)

| Region   | Free      | Notes                                      |
|----------|-----------|--------------------------------------------|
| PSRAM    | ~7 MB     | Framebuffers + data arrays + hosted tasks  |
| Int SRAM | ~114 KB   | Reserve 80 KB for DMA (esp_hosted SDIO)    |
