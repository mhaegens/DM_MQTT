# SAP DM MQTT Shop Floor Simulator

Physical demo rig connecting an ESP32-P4 edge device to SAP Digital Manufacturing cloud via MQTT — no middleware required.

The device runs a full touchscreen UI showing machines, tanks, operators, and live KPIs. All state is published as MQTT telemetry through a Cloudflare-tunnelled Mosquitto broker directly into SAP DM.

---

## Hardware

- **ESP32-P4** (Waveshare ESP32-P4-WIFI6-Touch-LCD-4B "Smart 86 Box")
- 720×720 MIPI-DSI touch display (ST7703)
- WiFi via ESP32-C6 SDIO companion chip
- SD card slot for JSON config
- BOOT button (GPIO35) for navigation

---

## Architecture

```
ESP32-P4 device
  └── firmware/          ESP-IDF firmware (this repo)
        ├── JSON config   SD card or embedded default
        ├── WiFi          STA + soft-AP provisioning
        ├── MQTT          WSS → mqttfordm.haegens.be
        └── UI            LVGL 9 on 720×720 DSI panel

Cloudflare Tunnel
  └── Mosquitto broker   MQTT over WSS, public endpoint

SAP Digital Manufacturing
  └── IoT integration    Subscribes to MQTT topics
```

---

## Server Stack (Ubuntu 24.04 LXC)

### Install

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/mhaegens/DM_MQTT/main/install.sh)
```

Installs Docker, clones the repo to `/opt/sapdm-demo`, starts the full stack.

### Update

```bash
bash /opt/sapdm-demo/update.sh
```

### Services

| Service   | Port(s)    | Purpose |
|-----------|------------|---------|
| Mosquitto | 1883, 9001 | MQTT broker — TCP (internal) + WebSocket (tunnel) |
| Dashboard | 80         | nginx serving `/dashboard` (live monitor) and `/editor` (JSON config editor) |

Internet access is provided by the **existing Cloudflare Tunnel on the Proxmox host** — no cloudflared container runs in this stack. Add three ingress rules to your existing tunnel config pointing at this LXC's LAN IP (see `cloudflared/config.yml.example` and `docs/INSTALL.md` Part 3).

### Cloudflare Tunnel setup

Add these rules to your existing tunnel `config.yml` on the Proxmox host. Replace `192.168.x.x` with this LXC's LAN IP. Rule order matters — Cloudflare evaluates top-to-bottom and stops at the first match:

```yaml
- hostname: mqttfordm.haegens.be
  path: /editor.*
  service: http://192.168.x.x:80

- hostname: mqttfordm.haegens.be
  path: /dashboard.*
  service: http://192.168.x.x:80

- hostname: mqttfordm.haegens.be
  service: ws://192.168.x.x:9001
```

Then reload: `systemctl reload cloudflared`

---

## Key Decisions

**WSS over plain MQTT TCP:** The broker endpoint uses WebSocket over TLS on port 443. SAP DM and browser clients connect directly over the public internet without a VPN or firewall change. Port 443 is open in every enterprise network.

**Docker Compose on an LXC:** A single `docker compose up` starts the full stack reproducibly. The install script handles Docker setup, repo clone, and volume creation. No VM overhead, no manual service configuration.

**JSON config on SD card:** The demo scenario (machines, tanks, operators, metrics) lives in a single JSON file. Swapping scenarios for a different customer requires copying a new file to the SD card and rebooting. No reflashing or recompile needed.

**PSRAM for large data arrays:** All entity arrays and display framebuffers are allocated in SPIRAM. The ESP32-P4 has ~8 MB of PSRAM; internal SRAM is reserved for DMA operations (SDIO driver ring buffers and the LVGL draw buffer).

---

## Firmware

See [firmware/README.md](firmware/README.md) for build, flash, and configuration instructions.

---

## Demo Screens

| Tab       | What it shows                                                  |
|-----------|----------------------------------------------------------------|
| Home      | 4 KPI tiles + WiFi/MQTT connection status                      |
| Machines  | Up to 10 machine cards with live metrics, start/stop controls  |
| Tanks     | Up to 3 tanks with wave animation, drag-to-adjust fill level   |
| People    | Operator badge carousel — physical NFC mode + virtual select   |
| Debug     | Broker/IP/RAM stats + rolling MQTT publish log                 |
| WiFi      | Network scan, join, soft-AP hotspot toggle, QR code            |

All demo data (machines, tanks, operators, topics) is driven by a JSON config file on the SD card — no reflashing needed to change the demo scenario.

---

## Documentation

| Document | What it covers |
|----------|---------------|
| [docs/architecture.md](docs/architecture.md) | System design, key decisions, task map, memory layout, thread safety |
| [docs/mqtt-topic-reference.md](docs/mqtt-topic-reference.md) | Full MQTT topic schema, payload formats, SAP DM integration |
| [docs/config-schema.md](docs/config-schema.md) | JSON config file fields, capacity limits, annotated example |
| [docs/server-setup.md](docs/server-setup.md) | Server stack install, Cloudflare Tunnel setup, verification |
| [docs/troubleshooting.md](docs/troubleshooting.md) | Known issues with root causes and fixes |
| [docs/demo-runbook.md](docs/demo-runbook.md) | Operational guide for running the rig at a demo |
| [firmware/docs/firmware-architecture.md](firmware/docs/firmware-architecture.md) | Firmware boot sequence, tasks, data layer, UI architecture |
| [firmware/docs/sdkconfig-reference.md](firmware/docs/sdkconfig-reference.md) | Rationale for every non-obvious sdkconfig.defaults entry |
| [dashboard/README.md](dashboard/README.md) | Dashboard and editor web apps |
