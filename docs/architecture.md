# Architecture: SAP DM MQTT Shop Floor Simulator

This document explains how the system fits together and why each major architectural choice was made. It is the entry point for anyone making changes to any layer of the project.

---

## System Layers

```
┌────────────────────────────────────────────────────────────┐
│  ESP32-P4 Edge Device                                      │
│  720x720 touchscreen, WiFi via ESP32-C6 SDIO companion     │
│  Runs firmware: UI + simulator + MQTT client               │
└──────────────────────────┬─────────────────────────────────┘
                           │ MQTT over WSS (port 443)
                           │ wss://mqttfordm.haegens.be
┌──────────────────────────▼─────────────────────────────────┐
│  Cloudflare Tunnel                                         │
│  Zero Trust ingress, no open firewall ports required       │
│  Routes /editor and /dashboard to nginx, WS to Mosquitto   │
└──────────┬───────────────────────────────┬─────────────────┘
           │ WebSocket MQTT               │ HTTP
┌──────────▼──────────┐       ┌───────────▼──────────────────┐
│  Mosquitto (broker) │       │  Nginx (web server)          │
│  TCP 1883 (LAN)     │       │  /dashboard: live monitor     │
│  WS 9001 (tunnel)  │       │  /editor: JSON config editor  │
└──────────┬──────────┘       └──────────────────────────────┘
           │ MQTT subscribe
┌──────────▼──────────┐
│  SAP Digital Mfg    │
│  IoT integration    │
│  (and browser)      │
└─────────────────────┘
```

All three server components (Mosquitto, Nginx, Cloudflared) run as Docker containers on a single Ubuntu 24.04 LXC host using Docker Compose.

---

## Key Decisions

### Why WSS instead of plain MQTT TCP

The public broker endpoint uses WebSocket over TLS (`wss://`) on port 443. This means SAP DM can connect to the broker directly over the public internet without requiring a VPN, firewall hole, or static IP. Port 443 is always open in enterprise networks. Plain TCP MQTT on port 1883 is used only on the LAN for local tools.

### Why Docker Compose on an LXC, not bare metal

Docker Compose makes the stack fully reproducible. A single `docker compose up -d` starts Mosquitto, Nginx, and Cloudflared in the correct order with all networking pre-wired. The entire install is scripted in `install.sh`. Using a Proxmox LXC instead of a VM avoids hypervisor overhead while still keeping the stack isolated from the host OS.

### Why no reverse proxy like Traefik or Caddy

The routing logic is exactly three rules: `/editor` goes to Nginx, `/dashboard` goes to Nginx, everything else goes to Mosquitto as WebSocket. Traefik adds complexity with no benefit here. Cloudflared itself handles the routing via ingress rules.

### Why JSON config on SD card

The entire demo scenario (machines, tanks, operators, simulation profiles) is in a single JSON file. Changing the scenario for a different customer requires only copying a new file to the SD card and rebooting. No reflashing, no recompile. The firmware falls back to an embedded default if no SD card is present.

---

## Cloudflare Tunnel Ingress Rules

The `config.yml` for Cloudflared contains three rules. The order is critical because Cloudflared stops at the first match and applies it.

```yaml
ingress:
  - hostname: mqttfordm.haegens.be
    path: /editor.*
    service: http://dashboard:80
  - hostname: mqttfordm.haegens.be
    path: /dashboard.*
    service: http://dashboard:80
  - hostname: mqttfordm.haegens.be
    service: ws://mosquitto:9001
```

The path-specific rules must come before the catch-all. If the catch-all rule is placed first, it matches every request for the hostname and forwards it to Mosquitto. The dashboard and editor become unreachable with no error message: the browser just gets a WebSocket upgrade response instead of HTML.

---

## FreeRTOS Task Map

| Task | Stack | Priority | CPU | Wake condition |
|------|-------|----------|-----|---------------|
| LVGL render (BSP-managed) | 20 KB | BSP default | 1 | 20 ms period |
| sim | 8 KB | 5 | 0 | 1500 ms delay |
| btn | 2 KB | 5 | 0 | 20 ms poll |
| con (console) | 2 KB | 3 | 0 | 50 ms poll |
| MQTT internal (esp_mqtt) | esp_mqtt default | esp_mqtt default | 0 | event-driven |
| esp_hosted SDIO tasks | SPIRAM | — | — | SDIO interrupt |

LVGL is pinned to CPU 1 (`cfg.lvgl_port_cfg.task_affinity = 1`). All other tasks run on CPU 0. This separation means MQTT reconnect loops and WiFi events cannot stall the display.

The LVGL task stack is 20 KB, not the BSP default of 7 KB. The `ui_machines_create` function builds a deep LVGL object hierarchy that exhausts the default stack.

---

## Memory Layout

### PSRAM (Octal SPI, 200 MHz, ~8 MB total)

All large data structures are allocated in PSRAM using `heap_caps_malloc(MALLOC_CAP_SPIRAM)`:

- `g_machines[]`: up to 10 Machine structs, each with up to 8 Metric structs
- `g_tanks[]`: up to 3 Tank structs
- `g_badges[]`: up to 12 Badge structs
- Display framebuffers: two 720 x 720 x 2 bytes = ~1 MB each, allocated by the BSP DPI driver

After initialization: approximately 7 MB free PSRAM.

### Internal SRAM (~350 KB total on ESP32-P4)

Internal SRAM is required for DMA-capable operations:

- **LVGL draw buffer:** the software render buffer stays in internal SRAM (`buff_spiram = false` in `bsp_display_cfg_t`). Setting this to PSRAM freezes the UI because the display controller cannot read the render buffer via the same DMA path as the framebuffers.
- **SDIO DMA ring buffers:** `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=81920` reserves 80 KB of internal SRAM so `esp_hosted` can allocate its DMA ring buffers. If this reservation is too small, the SDIO driver fails at init with a DRAM allocation error.
- **NVS, networking stacks, FreeRTOS task stacks** for small tasks

After initialization: approximately 114 KB free internal SRAM. The boot log warns if this falls below 100 KB.

### Why small objects stay internal

`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=512`: any allocation smaller than 512 bytes goes to internal SRAM automatically, even when `CONFIG_SPIRAM_USE_MALLOC=y` is set. This avoids PSRAM latency for short-lived stack-like allocations (small structs, string buffers) while still letting `malloc` use PSRAM for large buffers.

---

## WiFi Architecture

The ESP32-P4 does not have a native WiFi radio. WiFi is handled by an ESP32-C6 companion chip on the same Waveshare board, connected to the P4 over 4-bit SDIO (CMD=19, CLK=18, D0-D3=14-17, RESET=54).

The `esp_wifi_remote` component proxies all WiFi API calls from the P4 over SDIO to the C6. From the application code perspective, the API looks identical to standard ESP-IDF WiFi. The difference shows up in two places:

1. **Scan callback delivery:** `esp_wifi_scan_start` reaches the C6 successfully, but the `WIFI_EVENT_SCAN_DONE` event is not reliably delivered back to the P4 application event loop. The WiFi scan tab shows no results because of this. See [troubleshooting.md](troubleshooting.md) for details.

2. **esp_hosted task allocation:** The SDIO driver tasks must be created in SPIRAM (`CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y`). Without this, `xTaskCreatePinnedToCore` fails during boot because internal SRAM is exhausted by the time the hosted driver initializes.

Soft-AP for WiFi provisioning runs on the C6 radio. The DHCP server and the captive portal HTTP server both run on the P4 (LwIP stack). The DNS redirect needed for iOS captive portal auto-detection is not yet implemented.

---

## MQTT Transport

The firmware uses `esp_mqtt` with WSS transport. `network.disable_auto_reconnect = true` is set intentionally. The built-in auto-reconnect hammers the broker with rapid connection attempts. Instead, the application implements staged exponential backoff using `esp_timer`:

- Attempt 1: retry after 5 seconds
- Attempt 2: retry after 10 seconds
- Attempt 3+: retry after 30 seconds

MQTT starts only after WiFi gets an IP address. The `on_wifi_ip` callback in `main.c` calls `mqtt_task_start()` on the first IP, and `mqtt_task_reconnect()` on subsequent WiFi reconnects.

---

## Config Load Priority

When the firmware boots, it looks for configuration in this order:

1. SPIFFS flash partition: `/spiffs/sapdm_config.json`
2. SD card: the SPIFFS path is tried first; SD card is a secondary fallback (see `config_loader.c`)
3. Embedded C string: `DEFAULT_CONFIG_JSON` in `default_config.c`

SPIFFS is checked first because the SPIFFS partition can be pre-loaded with a customer-specific scenario via `idf.py spiffsgen`. This lets the device arrive at a demo pre-configured without needing an SD card.

---

## LVGL Display Configuration

Three `sdkconfig.defaults` flags must be set together:

```
CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2
CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH=y
```

**Double buffering** (`BUFFER_NUMS=2`) allocates two full-resolution framebuffers in PSRAM. The DPI DMA displays from one while LVGL renders into the other, then they swap on vsync.

**Avoid tear** (`AVOID_TEAR`) makes the swap happen at vsync, eliminating horizontal tearing.

**Full refresh** (`FULL_REFRESH`) marks the entire framebuffer as dirty on every LVGL timer tick, forcing a full 720x720 repaint. This eliminates the partial-repaint gaps that caused cyan flashes (uninitialized PSRAM areas exposed through dirty-region holes). The cost is that every scroll event also triggers a full repaint. See the scroll lag entry in [troubleshooting.md](troubleshooting.md).

Removing any one of these three flags causes visual artifacts or a cyan flash.

---

## Thread Safety

LVGL is not thread-safe. Any LVGL object mutation from a task that is not the LVGL render task must go through `lv_async_call()`. This queues a callback into the LVGL event loop, where it executes safely on the render task.

In this firmware, `mqtt_task`, `wifi_manager`, and `sim_task` all run on CPU 0 while LVGL runs on CPU 1. All UI updates triggered by these tasks use `lv_async_call()`. Calling LVGL functions directly from these tasks causes race conditions that manifest as cyan screens, stuck labels, or hard faults.

The shared data arrays (`g_machines`, `g_tanks`) are protected by a FreeRTOS mutex: `DATA_LOCK()` / `DATA_UNLOCK()` in `data.h`. The `sim_task` holds the lock while updating metric values. UI code reading these arrays for rendering must also acquire the lock.
