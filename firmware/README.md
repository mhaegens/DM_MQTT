# SAP DM MQTT Simulator — ESP32-P4 Firmware

ESP-IDF firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (Smart 86 Box).
Displays the full shop-floor simulator UI on the 720×720 MIPI-DSI touch screen
and publishes MQTT telemetry to `wss://mqttfordm.haegens.be` via Cloudflare Tunnel.

## Prerequisites

### 1. ESP-IDF v5.3+

```bash
# Install via the Espressif installer or:
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp/idf
cd ~/esp/idf && git checkout v5.3 && ./install.sh esp32p4
source ~/esp/idf/export.sh
```

### 2. Waveshare BSP

Download the **ESP32-P4-WIFI6-Touch-LCD-4B BSP** from the Waveshare wiki:
- Search for "ESP32-P4-WIFI6-Touch-LCD-4B" on waveshare.com
- Download the ESP-IDF BSP component
- Extract into `firmware/components/waveshare_bsp/`

The BSP initialises the MIPI-DSI display panel and GT911 touch controller.

### 3. Generate font files

Install `lv_font_conv` (requires Node.js):

```bash
npm install -g lv_font_conv
```

Download IBM Plex Sans and IBM Plex Mono OTF files, then run from `firmware/`:

```bash
# Run generate_fonts.sh (creates firmware/fonts/*.c and *.h)
bash generate_fonts.sh
```

Or use the pre-generated font files if included in this repo.

### 4. Edit credentials

```bash
# Edit firmware/main/config.h:
#define DEFAULT_WIFI_SSID     "your-network"
#define DEFAULT_WIFI_PASSWORD "your-password"
```

After the first flash, credentials can be updated via the captive-portal
provisioning screen (Phase 8) without reflashing.

## Build & Flash

```bash
cd firmware
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/tty.usbserial-* flash monitor
```

Monitor output at 115200 baud. All MQTT publish events are mirrored to UART.

## Architecture

```
main.c              App entry — inits BSP, data model, WiFi, MQTT, sim, UI
data_model.c/h      All machine/tank/operator state + metric history buffers
mqtt_task.c/h       esp_mqtt over WSS → mqttfordm.haegens.be
sim_task.c/h        1.5 s random-walk simulation for all machine metrics
wifi_simple.c       Phase 3 WiFi init (replaced by wifi_prov.c in Phase 8)
hw/buttons.c/h      BOOT button (GPIO9) short/long press → decrement/increment
hw/audio.h          Audio stub — ES8311 I2S (Phase 9, gated by compile flag)
ui/ui_theme.h       All design tokens (colors, fonts, sizes) — SAP Fiori Horizon
ui/ui_main.c/h      LVGL init, shell bar, tab bar, screen transitions
ui/ui_home.c/h      Home: 2×2 KPI grid + connection status rows
ui/ui_machines.c/h  Machines: 2×2 cards with sparklines, arc gauges (Phase 4)
ui/ui_tanks.c/h     Tanks: wave physics + drag-to-adjust (fully implemented)
ui/ui_debug.c/h     Debug: MQTT log panel + broker info
ui/ui_nfc.c/h       NFC: badge carousel + ring animation (Phase 7)
ui/ui_detail.c/h    Detail: all 6 viz types + step input + slider (Phase 6)
```

## Implementation Phases

| Phase | Content | Status |
|---|---|---|
| 1 | Project scaffold, BSP + display baseline, LVGL init | ✅ This commit |
| 2 | Theme tokens, fonts, shell + tab navigation | 🔜 |
| 3 | WiFi + esp_mqtt WSS connection | 🔜 |
| 4 | Home + Machines + sim task + Debug screens | 🔜 |
| 5 | Tanks screen with wave physics | ✅ Implemented |
| 6 | Detail view (all 6 viz types) | 🔜 |
| 7 | NFC screen + PN532 task + badge carousel + BOOT button | 🔜 |
| 8 | WiFi captive-portal provisioning | 🔜 |
| 9 | Audio feedback (ES8311 click/chime tones) | 🔜 |
| 10 | QR code on Home screen linking to web dashboard | 🔜 |

## Key Hardware Notes

- Display: **MIPI-DSI** (not RGB parallel — PRD has an error). Must use Waveshare BSP.
- WiFi: via **ESP32-C6 SDIO bridge** — do not use native P4 WiFi APIs.
- I2C bus: SDA=GPIO7, SCL=GPIO8 — shared by GT911 touch (0x5D) and PN532 NFC (0x24).
- BOOT button: GPIO9 — also I2S DSDIN. Button and audio are mutually exclusive.
- SD card: SDIO 4-bit, GPIO39–44. Available but not needed (fonts compiled in).
