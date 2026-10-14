# SAP DM MQTT Simulator — ESP32-P4 Firmware

ESP-IDF firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (Smart 86 Box).
Displays a full shop-floor simulator UI on the 720×720 MIPI-DSI touch screen
and publishes MQTT telemetry over WSS to a deployment-configured broker.

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

**The SD card is the home of the profile library.** At boot, any bundled
config missing from the card (`config_brewery.json`, `config_bakery.json`,
`config_ev_battery.json`, the annotated schema doc, and the active config)
is copied onto it — card copies are never overwritten, so edits made on the
card survive. With no card inserted everything keeps working from flash,
falling back to the embedded default when flash is empty too.

The captive portal (WiFi tab → Setup via Phone → Config) can upload a new
config or activate any profile from the list. Uploads are validated
(parseable JSON with `machines`/`tanks`/`people`), stored **permanently as a
named profile under their original filename** (SD card preferred, flash
fallback) so they stay in the list across profile switches and reboots, and
optionally activated. Different uploads with the same name receive a numbered
filename; identical re-uploads reuse the existing profile. The on-device list
is paginated rather than capped at twelve files. Invalid saved files remain
visible with a warning. Older unnamed active configurations are archived when
they are not already represented in the library.

Activating a profile writes it to `sapdm_config.json` on
**both** SPIFFS and the SD card, so the two stores stay in sync; if a card
that was present stops responding during a save, the portal reports an error
instead of letting the stale card copy win at the next boot. The profile
list shows each file once — the SD copy wins when it exists on both stores.
Writes use a staged file, byte verification and recoverable backup before
replacement. SD power is enabled through the board's VO4 rail and GPIO45;
the card stays mounted on slot 0 throughout the boot.

Schema reference: [`main/spiffs_image/sapdm_config.annotated.json`](main/spiffs_image/sapdm_config.annotated.json)

An optional top-level `ui` block tunes the Machines tab rendering:

```json
"ui": { "indicator_scale": 100 }
```

`indicator_scale` is a percent (50–200, default 100) applied to the indicator
size caps on machine cards — the arc gauge diameter and the sparkline strip
height. 100 matches the HTML PoC proportions; lower it for denser cards,
raise it for bigger gauges. Cards always fill their fixed grid slot: the
scale moves the caps, and the layout still degrades gracefully (gauges fall
back to value tiles, tiles collapse to one-line rows) when many metrics
compete for space.

### WiFi

The device remembers up to ten networks in NVS, with the most recently successful
connection preferred. New credentials are saved only after getting an IP address;
failed joins/password changes never replace a working saved password. The one
network stored by older firmware is migrated automatically. When the preferred
network is unavailable, the controller tries the other saved networks and backs
off between cycles. At capacity, the least recently used entry is replaced.

The WiFi screen lists saved networks even when they are not nearby. Tap a network
name to reconnect without typing its password. Use **Password** to update it or
**Forget** to remove that network only. Nearby scan results mark remembered networks
as **Saved**. The phone portal also accepts an empty password for a saved network.
The known-network API returns names only; passwords stay inside the manager.
A soft-AP hotspot can be toggled from the WiFi tab for provisioning. After connecting a phone
to the hotspot, a captive portal (with a DNS hijack responder on UDP/53) serves the credential
entry page. WiFi scanning uses a blocking scan in a worker task and reconnects the previous
network automatically when the scan finishes.

---

## Task Map

| Task | Stack | CPU | Wake |
|------|-------|-----|------|
| LVGL render (BSP) | 20 KB | 1 | 16ms period (direct mode — dirty regions only) |
| sim | 8 KB | 0 | 100ms observation; 3000ms simulation |
| mqtt_tx | 6 KB | 0 | event-driven |
| wifi_ctrl | 6 KB | 0 | commands and connection timeouts |
| btn | 4 KB | 0 | 20ms poll |
| con (console) | 2 KB | 0 | 50ms poll |
| MQTT internal (esp_mqtt) | esp_mqtt default | 0 | event-driven |

LVGL is on CPU 1. All other application tasks are on CPU 0. MQTT producers copy into bounded application storage; the dedicated transmitter
worker performs socket writes. WiFi connection/scan commands and successful-network
persistence run in a separate controller worker, so touch callbacks never wait for them.

## Project structure

```
main/
  main.c                 App entry — BSP, data, WiFi, MQTT, sim, UI init
  config.h               Compile-time defaults (broker URI, GPIO pins)
  config_loader.c/h      SD card JSON loader with embedded fallback
  default_config.json    Embedded default demo scenario
  data.c/h               All machine/tank/operator state in PSRAM
  mqtt_task.c/h          esp_mqtt client — WSS to Cloudflare tunnel
  sim_task.c/h           3 s simulation, 100 ms change observation
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
    ui_debug.c/h          Debug: live latency, queue, memory, WiFi and SD status
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
| 2 | WiFi | Portal scan raced soft-AP bring-up: the single scan fired at portal start could return empty and nothing retried it, stranding the portal on "Scanning...". Now the scan retries in-task, re-kicks when the phone associates, and the auto-refreshing scanning page retries on every load. DNS auto-launch (A-queries-only) confirmed working on device. |
| 3 | Display | Direct-mode rendering + ghost scrolling need on-device validation: verify no tearing/cyan regressions and pixel-identical cards at scroll end. Reverts: `UI_SCROLL_GHOSTS 0` in `main/config.h`; for direct mode restore the full-refresh patch + `CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH=y` and set `timer_period_ms` back to 33. |

---

## Memory profile (post-init)

| Region   | Free      | Notes                                      |
|----------|-----------|--------------------------------------------|
| PSRAM    | ~29 MB    | Measured on the connected 32 MB board; profile dependent |
| Int SRAM | ~137 KB after init | Brewery profile; Debug shows current/minimum runtime memory |


## October 2026 latency audit

See [the audit](../docs/performance-audit-2026-10-14.md) for findings, validation, hardware limits and rollout. Telemetry now publishes only changes at its two-decimal wire precision, with a complete snapshot after reconnect. A dedicated transmitter protects LVGL from transport waits and bounds the backlog; discrete events use QoS 1. Diagnostics report device/broker loopback RTT and queue/counter information.

The pinned build targets ESP-IDF 5.4.1 and the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B. The default broker remains WSS; `SAP DM connection` in menuconfig can select a direct LAN/WireGuard MQTT URI. New defaults use 360 MHz, 25 ms MQTT read polling and a 15 s keepalive. Configure from the new defaults rather than carrying an old generated sdkconfig forward.

For a checkout path containing spaces, activate ESP-IDF and run `bash tools/build_check.sh`. The helper builds a copy in a temporary path and never flashes a device. On an existing device, use an application-only flash to preserve SPIFFS profiles.

The tank grid and tank overview share an allocation-free conical-vessel renderer with rounded shoulders, support legs, and a discharge outlet. Levels keep their original height-percentage meaning, and drag/target controls remain available. The device retains its original bitmap SAP logo.

Debug shows actual broker round trip, application queue wait/depth, message rate,
failed/rejected/suppressed counters, WiFi signal/IP, internal/PSRAM headroom,
SD capacity and profile count. Connection, Reconnect, Profiles and Check storage
perform their named actions. Its timer runs only while the page is visible.

For local USB diagnostics, `D` opens Debug, `T` opens Tanks, `C` opens Profiles,
`L` prints the profile catalog, `R` reconnects MQTT, `P` starts the phone portal,
and `S` captures the screen. Screen capture uses a temporary 20 KB worker instead
of rendering on the small console stack. The screenshot helper waits through
USB-triggered startup and supports `--screen tanks|debug|profiles`. Pixel bytes use raw buffered UART writes, and a CRC32 check rejects corrupted
transfers. The buffered console driver is initialized on first capture.

The malloc cutoff is 64 bytes, and large TLS buffers follow the PSRAM-aware
allocator. SPIFFS work/cache buffers are kept in internal RAM through a scoped
compile header, preserving the SDK and filesystem format. This avoids flash
read stalls and leaves DMA headroom when the portal and full UI run together.

## Private deployment settings

WiFi defaults are empty. Provision networks on the device; successful credentials stay in NVS. Set your MQTT URI through menuconfig, or create ignored `sdkconfig.local.defaults` with `CONFIG_DM_MQTT_BROKER_URI="wss://mqtt.example.com"` and `CONFIG_DM_MQTT_BROKER_PORT=0`. Replace the example hostname locally. The build helper layers this private file over the checked-in defaults. Never commit network passwords, generated sdkconfig, board backups or private deployment files.
