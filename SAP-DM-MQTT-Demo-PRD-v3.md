# SAP DM MQTT Shop Floor Simulator

## Product Requirements Document

**Version 3.0 | April 2026**

| Field        | Value                                                    |
|--------------|----------------------------------------------------------|
| Author       | Demo Operator                                          |
| Domain       | SAP Digital Manufacturing — Presales & Solution Advisory |
| Status       | Draft                                                    |
| Last Updated | April 2026                                               |

### Changelog

| Version | Changes |
|---------|---------|
| 1.0 | Initial device-side document (ESP32 + ILI9341) |
| 1.1 | Added Docker Compose stack, Node-RED, web dashboard, Nginx, WebSocket config, Proxmox LXC topology |
| 1.2 | Removed SAP Production Connector; SAP DM connects directly to Mosquitto via Cloudflare Tunnel |
| 2.0 | Device upgrade to ESP32-P4 Smart 86 Box; LVGL UI; NFC operator login; extended per-machine and per-tank telemetry; physical display scale constraints |
| 3.0 | Full merge of device-side (v2.0) and server-side (v1.2) into single end-to-end document |

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Proxmox Infrastructure Topology](#3-proxmox-infrastructure-topology)
4. [Demo Stack: Docker Compose](#4-demo-stack-docker-compose)
5. [Cloudflare Tunnel](#5-cloudflare-tunnel)
6. [Hardware Component Specifications](#6-hardware-component-specifications)
7. [Physical Display Constraints](#7-physical-display-constraints)
8. [Firmware Screen Specifications](#8-firmware-screen-specifications)
9. [Metric Definitions](#9-metric-definitions)
10. [Web Dashboard](#10-web-dashboard)
11. [Node-RED](#11-node-red)
12. [Setup and Configuration Guide](#12-setup-and-configuration-guide)
13. [MQTT Topic Design](#13-mqtt-topic-design)
14. [SAP DM Integration](#14-sap-dm-integration)
15. [Troubleshooting Guide](#15-troubleshooting-guide)
16. [Open Items and Future Enhancements](#16-open-items-and-future-enhancements)
17. [Glossary](#17-glossary)

---

## 1. Project Overview

This document describes the complete architecture, components, and implementation plan for a physical SAP Digital Manufacturing (SAP DM) MQTT demo rig. The system demonstrates shop floor data flowing from physical edge hardware through a self-hosted MQTT broker directly into SAP DM cloud in real time, with no middleware layer required.

The demo is designed for presales and enablement contexts, illustrating how machine signals, rich telemetry, and operator identification are captured at the edge and surfaced in SAP DM. It runs entirely on demo-tunnel infrastructure with no dependency on corporate VPN or customer network.

### 1.1 Objectives

- Demonstrate live shop floor data ingestion into SAP DM using standard MQTT protocol
- Connect SAP DM directly to Mosquitto via Cloudflare Tunnel — no SAP Production Connector or middleware required
- Simulate multiple machine types (CNC, filling, packaging) with realistic multi-signal telemetry per asset type
- Simulate process tank levels with animated liquid UI and per-tank sub-metrics (temperature, pressure, flow, pH)
- Enable operator identification via NFC badge scan on the physical device
- Provide a browser-accessible web dashboard for real-time monitoring from a laptop during demos
- Use Node-RED for visual flow management, additional simulation, and fallback publishing
- Run all server-side components in a single Docker Compose stack that starts and stops as a unit
- Present a visually compelling SAP UI5 Horizon-compliant interface, legible and operable at 4-inch physical screen size

### 1.2 Non-Goals

- No SAP Production Connector or other middleware layer
- No PLC or SCADA connectivity; all signals are simulated
- SAP DM master data management is out of scope; the tenant is pre-configured
- No user authentication on the dashboard or physical device
- No data persistence or historian functionality

---

## 2. System Architecture

### 2.1 Layer Overview

| Layer | Component          | Technology                     | Responsibility                                                         |
|-------|--------------------|--------------------------------|------------------------------------------------------------------------|
| 1     | Physical Device    | ESP32-P4 Smart 86 Box          | Signal simulation, NFC scan, MQTT publishing via Cloudflare WSS        |
| 2     | MQTT Broker        | Mosquitto (Docker)             | Message routing; TCP 1883 (internal), WebSocket 9001                   |
| 3     | Internet Exposure  | Cloudflare Tunnel (Docker)     | Exposes Mosquitto WebSocket endpoint to ESP32-P4 and SAP DM cloud      |
| 4     | Flow Management    | Node-RED (Docker)              | Visual flows, simulation fallback, dashboard panels                    |
| 5     | Web Dashboard      | Custom HTML via Nginx (Docker) | Browser-based real-time monitoring and manual publish UI               |
| 6     | Cloud Application  | SAP DM (BTP)                   | Subscribes to MQTT topics; production execution and visibility         |

### 2.2 Data Flow

```
ESP32-P4 (WiFi, any internet connection)
  --> Cloudflare Tunnel (WSS) --> Mosquitto WebSocket :9001
                                     --> Node-RED subscriber (TCP :1883)
                                     --> Web Dashboard (WebSocket :9001 via LAN)

SAP DM (cloud)
  --> Cloudflare Tunnel (WSS) --> Mosquitto WebSocket :9001

Browser (on LAN)
  --> Nginx :80 --> dashboard HTML
      dashboard --> Mosquitto WebSocket :9001

Node-RED
  --> Mosquitto TCP :1883
```

### 2.3 Why Cloudflare Tunnel Instead of SAP Production Connector

SAP DM's native IoT/MQTT connectivity can subscribe directly to a broker over MQTT WebSocket (WSS). Cloudflare Tunnel exposes Mosquitto's WebSocket listener on a public hostname with TLS handled by Cloudflare — no port forwarding, no firewall changes, no certificates to manage. This eliminates the SAP Production Connector entirely: no Java runtime, no middleware configuration, no separate mapping layer. The tunnel runs as a lightweight Docker container in the same Compose stack.

### 2.4 Connectivity Model

- ESP32-P4 connects to any WiFi or phone hotspot with internet access (WiFi 6 via onboard ESP32-C6 co-processor)
- Connects to Mosquitto WebSocket via `wss://mqtt.example.com` — the same Cloudflare Tunnel endpoint used by SAP DM
- No VPN or port forwarding required; the device needs only outbound internet access
- Cloudflare Tunnel runs as a container and establishes an outbound connection to the Cloudflare edge — no inbound ports required on the demo-tunnel router
- SAP DM connects to the public Cloudflare hostname over WSS (port 443)
- Browser dashboard connects to Mosquitto WebSocket directly on the local LAN (port 9001)

> **Note:** The Cloudflare Tunnel only exposes the WebSocket listener. Plain TCP port 1883 is used internally by Node-RED and local tools only, and is never exposed to the internet.

> **Firmware note:** The ESP32-P4 firmware must use an MQTT client library with WebSocket transport support to connect via the Cloudflare Tunnel. Plain TCP MQTT (port 1883) is not routed through the tunnel.

---

## 3. Proxmox Infrastructure Topology

All demo infrastructure lives in dedicated LXC containers that can be stopped from Proxmox when not in use.

| LXC           | Purpose                             | Always On | Notes                            |
|---------------|-------------------------------------|-----------|----------------------------------|
| `sapdm-demo`  | Full demo stack via Docker Compose  | No        | Start before demo, stop after    |

### 3.1 sapdm-demo LXC Specifications

| Property | Value                                  |
|----------|----------------------------------------|
| Base OS  | Ubuntu 24.04 LTS                       |
| RAM      | 2 GB                                   |
| Disk     | 16 GB                                  |
| CPU      | 2 cores                                |
| Network  | Static LAN IP (e.g., `192.168.1.60`)   |
| Docker   | Docker Engine + Docker Compose plugin  |
| Nesting  | Enabled (required for Docker in LXC)   |

---

## 4. Demo Stack: Docker Compose

All demo services are defined in a single `docker-compose.yml` at `/opt/sapdm-demo/`. The entire stack starts and stops as one unit.

### 4.1 Services

| Service      | Image                        | Ports (host:container) | Purpose                                              |
|--------------|------------------------------|------------------------|------------------------------------------------------|
| `mosquitto`  | eclipse-mosquitto:2          | 1883:1883, 9001:9001   | MQTT broker; TCP + WebSocket                         |
| `cloudflared`| cloudflare/cloudflared:latest| —                      | Tunnel to expose Mosquitto WSS to SAP DM             |
| `nodered`    | nodered/node-red:latest      | 1880:1880              | Flow editor, simulation, optional dashboard          |
| `dashboard`  | nginx:alpine                 | 80:80                  | Serves custom web dashboard HTML                     |

### 4.2 Directory Layout

```
/opt/sapdm-demo/
  docker-compose.yml
  mosquitto/
    config/
      mosquitto.conf
    data/
    log/
  cloudflared/
    config.yml
    <tunnel-id>.json
  nodered/
    data/
  dashboard/
    html/
      index.html
```

### 4.3 docker-compose.yml

```yaml
version: "3.8"

services:

  mosquitto:
    image: eclipse-mosquitto:2
    container_name: sapdm-mosquitto
    restart: unless-stopped
    ports:
      - "1883:1883"   # internal only; ESP32-P4, Node-RED, CLI tools
      - "9001:9001"   # WebSocket; browser dashboard (LAN) + Cloudflare Tunnel
    volumes:
      - ./mosquitto/config/mosquitto.conf:/mosquitto/config/mosquitto.conf
      - ./mosquitto/data:/mosquitto/data
      - ./mosquitto/log:/mosquitto/log

  cloudflared:
    image: cloudflare/cloudflared:latest
    container_name: sapdm-cloudflared
    restart: unless-stopped
    command: tunnel --config /etc/cloudflared/config.yml run
    volumes:
      - ./cloudflared:/etc/cloudflared
    depends_on:
      - mosquitto

  nodered:
    image: nodered/node-red:latest
    container_name: sapdm-nodered
    restart: unless-stopped
    ports:
      - "1880:1880"
    volumes:
      - ./nodered/data:/data
    depends_on:
      - mosquitto

  dashboard:
    image: nginx:alpine
    container_name: sapdm-dashboard
    restart: unless-stopped
    ports:
      - "80:80"
    volumes:
      - ./dashboard/html:/usr/share/nginx/html:ro

networks:
  default:
    name: sapdm-net
```

### 4.4 Mosquitto Configuration

`/opt/sapdm-demo/mosquitto/config/mosquitto.conf`:

```
# Plain TCP -- ESP32-P4, Node-RED, CLI tools (internal only)
listener 1883
allow_anonymous true

# WebSocket -- browser dashboard + Cloudflare Tunnel for SAP DM
listener 9001
protocol websockets
allow_anonymous true

persistence false
log_dest stdout
```

> **Note:** For demos on shared or customer networks, enable password authentication on Mosquitto and restrict port 1883 to trusted hosts. See section 16.1.

### 4.5 Lifecycle Commands

```bash
cd /opt/sapdm-demo

docker compose up -d                          # start everything
docker compose down                           # stop everything
docker compose logs -f                        # follow all logs
docker compose logs -f cloudflared            # tunnel logs only
docker compose restart nodered               # restart a single service
docker compose pull && docker compose up -d   # update all images
```

---

## 5. Cloudflare Tunnel

### 5.1 Purpose

Cloudflare Tunnel (`cloudflared`) creates an outbound-only encrypted connection from the demo-tunnel to Cloudflare's edge network. It exposes Mosquitto's WebSocket listener on a public hostname (e.g., `mqtt.yourdomain.com`) without any inbound firewall rules or port forwarding. SAP DM connects to this public hostname over WSS. Cloudflare handles TLS termination; Mosquitto itself requires no certificate.

A Cloudflare account managing the target domain is required. The free tier is sufficient.

### 5.2 Setup Steps

1. Log in to the Cloudflare dashboard and navigate to Zero Trust > Networks > Tunnels
2. Create a new tunnel; assign a name (e.g., `sapdm-demo`)
3. Copy the tunnel credentials JSON file to `/opt/sapdm-demo/cloudflared/`
4. Create `/opt/sapdm-demo/cloudflared/config.yml`:

```yaml
tunnel: <your-tunnel-id>
credentials-file: /etc/cloudflared/<your-tunnel-id>.json

ingress:
  - hostname: mqtt.yourdomain.com
    service: ws://mosquitto:9001
  - service: http_status:404
```

5. Cloudflare automatically creates a CNAME DNS record for `mqtt.yourdomain.com` pointing to the tunnel
6. Start the stack; verify the tunnel shows as **Active** in the Cloudflare dashboard
7. Test from an external network: `wscat -c wss://mqtt.yourdomain.com`

### 5.3 Security Considerations

- Port 1883 (plain TCP) is never exposed through the tunnel; only WebSocket on 9001 is proxied
- Cloudflare provides TLS between SAP DM and the Cloudflare edge; the tunnel protocol encrypts the leg between the edge and the `cloudflared` container
- For customer-facing demos on shared networks, add Cloudflare Access policies to restrict which source IPs or identities may connect to `mqtt.yourdomain.com`
- If Mosquitto authentication is enabled, configure a dedicated SAP DM MQTT user via `mosquitto_passwd` and supply the credentials in both `mosquitto.conf` and the SAP DM IoT connectivity settings

---

## 6. Hardware Component Specifications

### 6.1 Physical Device — Primary Board

| Property           | Value                                                             |
|--------------------|-------------------------------------------------------------------|
| Board              | Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (Smart 86 Box)             |
| Microcontroller    | ESP32-P4, dual-core RISC-V LX7 @ 400 MHz + LP core @ 40 MHz     |
| Display            | 4-inch IPS 720x720, 5-point capacitive touch, RGB parallel bus   |
| WiFi               | WiFi 6 (802.11ax) via onboard ESP32-C6 co-processor              |
| Bluetooth          | BLE 5.0 via ESP32-C6                                             |
| Memory             | 32 MB PSRAM, 16 MB Flash                                         |
| Camera interface   | MIPI-CSI (available, not used in current scope)                  |
| Audio              | ES8311 codec + ES7210 echo cancellation + dual microphones       |
| IMU                | QMI8658 6-axis                                                   |
| RTC                | PCF85063                                                         |
| PMIC               | AXP2101                                                          |
| Power              | USB-C (power + programming + log output)                         |
| Form factor        | 86mm wall-panel box with integrated enclosure                    |
| Price              | ~65-75 USD                                                       |

> **Why P4 over S3:** The ESP32-S3 Smart 86 Box (480x480) was evaluated. The P4 variant was selected for the higher 720x720 resolution, 32 MB PSRAM for LVGL rendering headroom, WiFi 6, and BLE 5.0. The 86 Box form factor provides a professional enclosure suitable for table-top demos without requiring a custom 3D print.

> **Firmware note:** The ESP32-P4 uses RISC-V architecture and requires ESP-IDF or Arduino IDE with ESP32 board support including P4 targets. The display uses an RGB parallel bus at 720x720 resolution, which requires LVGL 9 with the Waveshare BSP rather than TFT_eSPI.

### 6.2 NFC/RFID Module

| Property  | Value                                                          |
|-----------|----------------------------------------------------------------|
| Module    | Elechouse PN532 NFC/RFID v3                                    |
| Protocol  | NFC ISO14443A/B, MIFARE Classic 1K/4K, FeliCa                 |
| Interface | I2C (SDA, SCL, VCC 3.3V, GND — 4 wires)                       |
| Read range| ~3-5 cm                                                        |
| Antenna   | Onboard PCB coil; NFC penetrates 1-2mm plastic                 |
| Price     | ~6-8 USD                                                       |

The PN532 connects to the ESP32-P4 expansion header via I2C with no conflict with the display bus. It is mounted flush behind the rear panel of the 86 Box enclosure using double-sided foam tape, with the antenna coil facing outward. NFC signals penetrate the ABS plastic casing without attenuation; no enclosure modification is required.

**NFC demo scenario:** The operator taps a MIFARE badge to the device instead of manually identifying themselves. The ESP32-P4 reads the card UID, maps it to an operator identity, and publishes an MQTT operator login event. In SAP DM terms this maps to operator confirmation on an SFC operation, tying production activity to a named individual.

### 6.3 Firmware Stack

| Property      | Value                                              |
|---------------|----------------------------------------------------|
| IDE           | Arduino IDE 2.x or ESP-IDF + VS Code               |
| Board package | ESP32 Arduino with ESP32-P4 target support          |
| Display lib   | LVGL 9 with Waveshare ESP32-P4 BSP                 |
| Touch lib     | GT911 capacitive driver (via BSP)                  |
| NFC lib       | Elechouse PN532 Arduino library                    |
| MQTT lib      | PubSubClient by Nick O'Leary                       |
| WiFi          | ESP32 WiFi stack via ESP32-C6 SDIO bridge          |
| Broker target | Mosquitto WebSocket via `wss://mqtt.example.com` (Cloudflare Tunnel) |

### 6.4 Network Connectivity

The ESP32-P4 connects to any WiFi or phone hotspot with internet access. It reaches the MQTT broker via the Cloudflare Tunnel at `wss://mqtt.example.com` — no VPN or dedicated network configuration required on the device side.

### 6.5 Complete Hardware BOM

| # | Item                                      | Notes                             | Est. Price      |
|---|-------------------------------------------|-----------------------------------|-----------------|
| 1 | Waveshare ESP32-P4-WIFI6-Touch-LCD-4B     | Main board + 86 Box enclosure     | ~65-75 USD      |
| 2 | Elechouse PN532 NFC/RFID v3 module        | I2C mode, no headers soldered     | ~6-8 USD        |
| 3 | USB-C to USB-C cable, 1m, 60W rated       | Power + programming from MacBook  | ~8 USD          |
| 4 | I2C Dupont cables, female-female x4       | PN532 to expansion header         | ~1-2 USD        |
| 5 | MIFARE Classic NFC cards/fobs x5          | Demo operator badges              | ~3-5 USD        |
| 6 | Desktop acrylic stand (optional)          | 30-45 degree presentation angle   | ~8-15 USD       |
|   | **Total**                                 |                                   | **~91-113 USD** |

---

## 7. Physical Display Constraints

The ESP32-P4 Smart 86 Box has a 4-inch physical screen at 720x720 pixels, yielding approximately 255 PPI. At this pixel density, standard desktop or web UI scales are illegible and untappable with a finger. All firmware UI sizing must derive from physical millimetre targets.

### 7.1 Physical Scale Reference

| Element               | Physical target | Pixel equivalent at 255 PPI |
|-----------------------|-----------------|-----------------------------|
| Minimum touch target  | 8.0 mm          | ~80 px                      |
| Smaller touch target  | 6.5 mm          | ~65 px                      |
| Body text / values    | 2.6 mm          | ~26 px                      |
| Large metric value    | 3.2 mm          | ~32 px                      |
| Hero metric number    | 4.0 mm          | ~40 px                      |
| Label / caption       | 1.5 mm          | ~15 px (minimum)            |
| Shell bar height      | 6.4 mm          | ~64 px                      |
| Tab bar height        | 7.2 mm          | ~72 px                      |
| Breadcrumb bar height | 4.4 mm          | ~44 px                      |

### 7.2 Design Consequences

- All interactive elements (buttons, metric tiles, tab items) are a minimum of 64px tall
- Primary action buttons are 80px tall
- Font sizes below 15px are used only for non-interactive labels
- Cards carry at minimum 12px internal padding to prevent crowding at physical scale
- The 2x2 machine card grid is the maximum density for the machines screen; a 3-column layout is not viable
- The tanks screen uses the full available height for the liquid visualisation; sub-metrics appear in a compact strip below each tank rather than competing for vertical space

---

## 8. Firmware Screen Specifications

The UI follows SAP UI5 Fiori Horizon design language adapted for embedded LVGL. IBM Plex Sans is used as the body typeface and IBM Plex Mono for all metric values, topics, and status labels. The theme is Horizon light mode: white card surfaces on a light blue-grey background (#F2F4FA). SAP blue (#0070F2) is the primary accent. Semantic colour tokens: green (#107E3E) for OK, orange (#E76500) for warning, red (#BB0000) for error.

Navigation uses a bottom tab bar (5 tabs, 72px tall). A breadcrumb bar (44px) appears above detail pages with a clearly labelled back path. Every tappable metric tile shows a small chevron indicator.

### 8.1 Navigation Structure

```
Home
Machines
  --> Machine metric tile --> Metric detail page --> back to Machines
Tanks
  --> Tank body (level)   --> Level detail page  --> back to Tanks
  --> Sub-metric chip     --> Metric detail page  --> back to Tanks
Debug Log
NFC / Operator
```

### 8.2 Shell Bar (64px)

Left: SAP badge (44x44 blue rounded rect with SAP wordmark), screen title, subtitle.
Right: MQTT status pill, WiFi status pill (Object Status component style).
Bottom border: 2px solid SAP blue (Horizon accent line).

### 8.3 Home Screen

- 2x2 KPI card grid: Machines Running, Pulses Today, MQTT Messages, WiFi RSSI
- Connection status rows: MQTT broker address + status badge, device IP, active operator
- Bottom tab bar with Home active

### 8.4 Machines Screen

Each asset is a card in a 2x2 grid. Card content is asset-type-specific:

**CNC-01 / CNC-02:**
- Sparkline tile: Spindle RPM with live rolling history
- Arc gauge row: Spindle Load %, Tool Wear %
- Value tile row: Feed Rate (mm/min), Temperature (°C)

**FILL-01:**
- Sparkline tile: Flow Rate (L/min) with live rolling history
- Arc gauge: Fill Pressure (bar)
- Value tile: Temperature (°C)

**PACK-01:**
- Sparkline tile: Line Speed (units/min) with live rolling history
- Arc gauge: OEE (%)
- Value tile: Temperature (°C)

All cards show: machine name, asset type, animated running/stopped status dot, pulse counter, START/STOP button (80px), PULSE button (80px). Every metric tile is tappable and opens a metric detail page.

### 8.5 Tanks Screen

The screen is divided into two zones:

**Zone 1 — Hero liquid visualisation (fills all available height above the strip):**
Three side-by-side animated tanks. Fill percentage is displayed as large text overlaid on the liquid. Dragging vertically adjusts level and publishes to MQTT throttled at 400ms. Tapping the tank body opens the level detail page.

Liquid rendering: two-frequency sine wave surface with slosh physics responding to drag direction, bubble particles rising through the liquid column, splash ring animation on touch, colour keyed to fill percentage (blue above 40%, orange 15-40%, red below 15%), linear gradient for depth.

**Zone 2 — Sub-metric indicator strip (compact, below each tank):**
One tappable chip per non-level metric for that tank. Each chip shows a coloured status dot, label, current value, and unit. Tapping opens the metric detail page.

Per-tank metrics are independent and not shared:

| Tank  | Sub-metrics                                        |
|-------|----------------------------------------------------|
| RAW   | Temperature (°C), Flow In (L/min)                  |
| PROD  | Temperature (°C), Pressure (bar)                   |
| WASTE | Temperature (°C), pH Level, Flow Out (L/min)       |

### 8.6 Metric Detail Page

Triggered by tapping any metric tile, arc gauge, sparkline, or tank sub-metric chip. The breadcrumb bar shows the parent entity name and a back button.

**Header card:** metric label, entity name, MQTT topic, large current value with colour coding, unit.

**Visualisation card:** type depends on the metric:

| Metric type          | Visualisation                                          |
|----------------------|--------------------------------------------------------|
| RPM, flow, speed     | Full-width sparkline, 40-point history, gradient fill  |
| Load, OEE, pressure  | Large semicircular arc gauge, threshold colour coding  |
| Temperature          | Vertical thermometer SVG with scale markings           |
| pH                   | Colour spectrum band with animated pointer             |
| Tank level           | Full-width liquid tank with wave physics               |
| Feed rate            | Horizontal bullet chart with zone markers              |

**Set Value section:**
- Integer/decimal metrics (RPM, feed rate, temperature, flow): SAP StepInput (minus button, numeric field, plus button); 80px tall; 32px font
- Percentage/ratio metrics (load, OEE, pressure, pH, level): SAP Slider; 28px thumb; range labels

**Generate Random section:**

For sparkline metrics (RPM, flow rate, line speed, flow in, flow out):
- Controls: Min, Max, Interval selector (0.5s / 1s / 2s / 5s / 10s)
- Start Series: fires immediately then repeats at the chosen interval, appending each value to the sparkline history and animating the chart in real time; publishes each value to MQTT
- Stop Series: halts the generator
- Animated status indicator shows the active interval
- Generator automatically stops on navigation away from the detail page

For all other metrics:
- Controls: Min, Max
- Single Generate button: fires one random value

### 8.7 Debug Log Screen

- Connection metadata grid: broker address, MQTT state, device IP, RSSI
- Rolling log panel (7 lines, dark terminal background): MQTT publish events, NFC scan events, connection events; newest entry highlighted, older entries dimmed
- All entries mirrored to UART at 115200 baud
- Simulate MQTT Publish button for demo testing

### 8.8 NFC / Operator Screen

- Animated NFC ring with concentric circles and rotating dashed ring
- Instruction text: "Tap Badge to Identify Operator"
- Last scanned operator card: name, role, UID, active badge
- Stats: last scan time, scans today
- Simulate Badge Scan button triggers ring animation and publishes a test operator event to `factory/operators/login`

---

## 9. Metric Definitions

Full list of all simulated signals with properties.

| Key          | Label          | Unit   | Min | Max  | Step | Visualisation | Input  | MQTT topic suffix   |
|--------------|----------------|--------|-----|------|------|---------------|--------|---------------------|
| rpm          | Spindle RPM    | RPM    | 0   | 8000 | 50   | sparkline     | step   | /rpm                |
| spindleLoad  | Spindle Load   | %      | 0   | 100  | 1    | arc gauge     | slider | /spindle_load       |
| toolWear     | Tool Wear      | %      | 0   | 100  | 1    | arc gauge     | slider | /tool_wear          |
| feedRate     | Feed Rate      | mm/min | 0   | 600  | 10   | bullet chart  | step   | /feed_rate          |
| machTemp     | Temperature    | °C     | 15  | 80   | 0.5  | thermometer   | step   | /temperature        |
| flowRate     | Flow Rate      | L/min  | 0   | 25   | 0.1  | sparkline     | step   | /flow_rate          |
| fillPres     | Pressure       | bar    | 0   | 5    | 0.1  | arc gauge     | slider | /fill_pressure      |
| lineSpeed    | Line Speed     | u/min  | 0   | 80   | 1    | sparkline     | step   | /line_speed         |
| oee          | OEE            | %      | 0   | 100  | 1    | arc gauge     | slider | /oee                |
| level        | Tank Level     | %      | 0   | 100  | 1    | tank visual   | slider | /level              |
| tankTemp     | Temperature    | °C     | 5   | 70   | 0.5  | thermometer   | step   | /temperature        |
| pressure     | Pressure       | bar    | 0   | 5    | 0.1  | arc gauge     | slider | /pressure           |
| flowIn       | Flow In        | L/min  | 0   | 20   | 0.1  | sparkline     | step   | /flow_in            |
| flowOut      | Flow Out       | L/min  | 0   | 20   | 0.1  | sparkline     | step   | /flow_out           |
| ph           | pH Level       | pH     | 0   | 14   | 0.1  | pH band       | slider | /ph                 |

### 9.1 Threshold Colour Coding

| Metric       | Green         | Orange          | Red            |
|--------------|---------------|-----------------|----------------|
| spindleLoad  | below 40%     | 40-70%          | above 70%      |
| toolWear     | below 40%     | 40-70%          | above 70%      |
| oee          | above 80%     | 60-80%          | below 60%      |

---

## 10. Web Dashboard

### 10.1 Purpose

A single-page HTML file served by Nginx at `http://192.168.1.60`. Connects to Mosquitto via MQTT over WebSocket on port 9001 using the MQTT.js browser library. Accessible from any device on the local LAN, providing a second visible surface during demos on a nearby laptop.

### 10.2 Features

- Live machine status cards with colour-coded RUNNING/STOPPED indicators
- Animated tank level gauges updating from `factory/tanks/+/level` topics in real time
- Publish panel: manually send status, pulse, and tank level messages to any asset
- Live message log showing all `factory/#` traffic with timestamps
- WebSocket connection status indicator

### 10.3 Access URLs

| URL                               | Service                                          |
|-----------------------------------|--------------------------------------------------|
| `http://192.168.1.60`             | Custom web dashboard                             |
| `http://192.168.1.60:1880`        | Node-RED flow editor                             |
| `http://192.168.1.60:1880/ui`     | Node-RED dashboard UI                            |
| `wss://mqtt.yourdomain.com`       | Mosquitto public endpoint for SAP DM             |

---

## 11. Node-RED

### 11.1 Purpose

Runs as a persistent service in the Compose stack, connecting to Mosquitto on the internal Docker network at `mosquitto:1883`. Provides visual flow inspection, simulation capability as a fallback when the ESP32-P4 is unavailable, and optional dashboard panels.

### 11.2 Key Nodes

| Node              | Role                                                         |
|-------------------|--------------------------------------------------------------|
| `mqtt in`         | Subscribe to `factory/#`                                     |
| `mqtt out`        | Publish simulated signals                                    |
| `function`        | Parse payloads, apply logic                                  |
| `debug`           | Live message inspection in the flow editor                   |
| `dashboard: gauge`| Tank level visualisation                                     |
| `dashboard: led`  | Machine running/stopped status indicator                     |

### 11.3 Simulation Fallback Flows

Pre-build Node-RED flows that replicate the ESP32-P4 device signals for each asset type. These allow a full demo to run without the physical device, for example when demoing remotely or before the device firmware is ready. Each flow publishes to the same topic structure as the device.

---

## 12. Setup and Configuration Guide

### 12.1 Provision the sapdm-demo LXC on Proxmox

Run `proxmox/create-lxc.sh` from the Proxmox host shell. It prompts for LXC ID, IP, storage, and other settings, then creates the container with the correct configuration (nesting enabled for Docker), starts it, and runs `install.sh` inside automatically.

```bash
# On the Proxmox host:
bash <(curl -fsSL https://raw.githubusercontent.com/mhaegens/DM_MQTT/main/proxmox/create-lxc.sh)
```

To provision manually instead, create a Ubuntu 24.04 LXC with 2 GB RAM, 16 GB disk, 2 cores, a static LAN IP, and nesting enabled in the LXC options, then run `install.sh` inside it.

### 12.2 Configure Cloudflare Tunnel

1. Create a tunnel named `sapdm-demo` in the Cloudflare Zero Trust dashboard
2. Download the tunnel credentials JSON and place it in `/opt/sapdm-demo/cloudflared/`
3. Create `config.yml` as defined in section 5.2
4. Start the stack and verify the tunnel is Active in the Cloudflare dashboard
5. Test externally: `wscat -c wss://mqtt.yourdomain.com`

### 12.3 ESP32-P4 Connectivity

No VPN required. The ESP32-P4 connects directly to `wss://mqtt.example.com` over any internet connection using MQTT over WebSocket. The Cloudflare Tunnel handles TLS; the device needs only outbound internet access.

Ensure the Cloudflare Tunnel is active (section 12.2) and the ESP32-P4 firmware uses an MQTT client library with WebSocket transport support.

### 12.4 Start the Demo Stack

```bash
cd /opt/sapdm-demo
docker compose up -d
docker compose ps          # verify all four services are Up
docker compose logs -f     # confirm no errors
```

### 12.5 Configure SAP DM MQTT Connectivity

1. In SAP DM, navigate to IoT or MQTT connectivity settings
2. Set broker URL to `wss://mqtt.yourdomain.com` (port 443)
3. Configure topic subscriptions to match `factory/#`
4. Map incoming topic payloads to SAP DM production objects (work centers, SFCs, data collection parameters) per the signal mapping in section 14.2
5. Verify connectivity by publishing a test message from Node-RED and confirming receipt in SAP DM logs

### 12.6 ESP32-P4 Firmware Setup

1. Install Arduino IDE 2.x or ESP-IDF + VS Code
2. Add ESP32 board support URL to Arduino IDE preferences:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Install the ESP32-P4 board target (check Waveshare wiki for the correct package version)
4. Download the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B BSP from the Waveshare wiki
5. Install LVGL 9 via the component manager or Arduino Library Manager
6. Install via Library Manager: PubSubClient (Nick O'Leary), Elechouse PN532 library
7. Configure LVGL display driver to use the Waveshare BSP RGB interface initialisation
8. Set `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_BROKER` in the firmware config header
   - `MQTT_BROKER` = static LAN IP of the `sapdm-demo` LXC (e.g., `192.168.1.60`)
   - `MQTT_PORT` = 1883
9. Flash via USB-C; open serial monitor at 115200 baud to verify WiFi and MQTT connection

### 12.7 PN532 Wiring to ESP32-P4 Expansion Header

Set the PN532 DIP switches to I2C mode before wiring.

| PN532 pin | ESP32-P4 expansion header              |
|-----------|----------------------------------------|
| SDA       | I2C SDA (confirm GPIO from BSP/schematic) |
| SCL       | I2C SCL (confirm GPIO from BSP/schematic) |
| VCC       | 3.3V                                   |
| GND       | GND                                    |

> The PN532 default I2C address is 0x24. Confirm the exact GPIO numbers from the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B schematic or wiki.

---

## 13. MQTT Topic Design

Topics follow the pattern `factory/{asset-type}/{asset-id}/{signal}`.

### 13.1 Port Reference

| Port     | Protocol          | Exposed To             | Used By                              |
|----------|-------------------|------------------------|--------------------------------------|
| 1883     | MQTT TCP          | LAN only (internal)    | Node-RED, local CLI tools            |
| 9001     | MQTT WebSocket    | LAN + Cloudflare Tunnel| Browser dashboard, ESP32-P4, SAP DM  |
| 443 (WSS)| MQTT WebSocket TLS| Internet               | ESP32-P4 + SAP DM via `mqtt.example.com` |

### 13.2 Machine Topics

| Topic                                   | Payload               | Trigger                       | Subscribers                     |
|-----------------------------------------|-----------------------|-------------------------------|----------------------------------|
| `factory/machines/{id}/status`          | `RUNNING` or `STOPPED`| Toggle button tap             | SAP DM, Node-RED, web dashboard  |
| `factory/machines/{id}/pulse`           | Integer string        | PULSE button or auto 5s       | SAP DM, Node-RED, web dashboard  |
| `factory/machines/{id}/rpm`             | Float string          | Live telemetry tick           | SAP DM, Node-RED                 |
| `factory/machines/{id}/spindle_load`    | Float string          | Live telemetry tick           | SAP DM, Node-RED                 |
| `factory/machines/{id}/tool_wear`       | Float string          | Value set or generated        | SAP DM, Node-RED                 |
| `factory/machines/{id}/feed_rate`       | Float string          | Value set or generated        | SAP DM, Node-RED                 |
| `factory/machines/{id}/temperature`     | Float string          | Live telemetry tick           | SAP DM, Node-RED                 |
| `factory/machines/{id}/flow_rate`       | Float string          | Live telemetry (FILL-01)      | SAP DM, Node-RED                 |
| `factory/machines/{id}/fill_pressure`   | Float string          | Value set or generated        | SAP DM, Node-RED                 |
| `factory/machines/{id}/line_speed`      | Float string          | Live telemetry (PACK-01)      | SAP DM, Node-RED                 |
| `factory/machines/{id}/oee`             | Float string          | Value set or generated        | SAP DM, Node-RED                 |

### 13.3 Tank Topics

| Topic                              | Payload      | Trigger                        | Subscribers                    |
|------------------------------------|--------------|--------------------------------|--------------------------------|
| `factory/tanks/{id}/level`         | Float string | Drag throttled 400ms + release | SAP DM, Node-RED, web dashboard|
| `factory/tanks/{id}/temperature`   | Float string | Value set or interval generated| SAP DM, Node-RED               |
| `factory/tanks/{id}/flow_in`       | Float string | Value set or interval generated| SAP DM, Node-RED               |
| `factory/tanks/{id}/pressure`      | Float string | Value set or interval generated| SAP DM, Node-RED               |
| `factory/tanks/{id}/flow_out`      | Float string | Value set or interval generated| SAP DM, Node-RED               |
| `factory/tanks/{id}/ph`            | Float string | Value set or interval generated| SAP DM, Node-RED               |

### 13.4 Operator Topic

| Topic                      | Payload                                       | Trigger        |
|----------------------------|-----------------------------------------------|----------------|
| `factory/operators/login`  | JSON: `{uid, name, role, plant, ts}`          | NFC badge scan |

### 13.5 Simulated Assets and Their Signals

| Asset   | Topic prefix              | Type    | Signals                                          |
|---------|---------------------------|---------|--------------------------------------------------|
| CNC-01  | `factory/machines/cnc01`  | Machine | status, pulse, rpm, spindle_load, tool_wear, feed_rate, temperature |
| CNC-02  | `factory/machines/cnc02`  | Machine | status, pulse, rpm, spindle_load, tool_wear, feed_rate, temperature |
| FILL-01 | `factory/machines/fill01` | Machine | status, pulse, flow_rate, fill_pressure, temperature |
| PACK-01 | `factory/machines/pack01` | Machine | status, pulse, line_speed, oee, temperature      |
| RAW     | `factory/tanks/raw`       | Tank    | level, temperature, flow_in                      |
| PROD    | `factory/tanks/prod`      | Tank    | level, temperature, pressure                     |
| WASTE   | `factory/tanks/waste`     | Tank    | level, temperature, ph, flow_out                 |

### 13.6 Payload Format

All payloads are plain UTF-8 strings except operator login (JSON). Decimal values use one decimal place (e.g., `73.5`). Machine status uses literal `RUNNING` or `STOPPED`. Pulse count is a cumulative decimal integer (e.g., `42`). SAP DM's native MQTT mapping configuration is responsible for parsing and binding to production objects.

---

## 14. SAP DM Integration

SAP DM connects directly to Mosquitto via the Cloudflare Tunnel public hostname over WSS. No middleware layer is involved.

### 14.1 Connection Parameters

| Parameter      | Value                                                       |
|----------------|-------------------------------------------------------------|
| Broker URL     | `wss://mqtt.yourdomain.com`                                 |
| Port           | 443 (standard WSS)                                          |
| Protocol       | MQTT over WebSocket Secure                                  |
| Authentication | Anonymous by default; username/password if Mosquitto auth is enabled |
| Subscription   | `factory/#`                                                 |

### 14.2 Signal Mapping

| MQTT Topic                            | SAP DM Object / API              | Field Mapping                   | Notes                                              |
|---------------------------------------|----------------------------------|---------------------------------|----------------------------------------------------|
| `machines/{id}/status = RUNNING`      | Start SFC Operation              | Work center = asset ID          | SFC must exist and be in queue                     |
| `machines/{id}/status = STOPPED`      | Complete or pause SFC            | Work center = asset ID          | Depends on demo scenario                           |
| `machines/{id}/pulse = {n}`           | Report Yield / Data Collection   | Actual quantity = 1 per pulse   | Cumulative count for shop floor visibility         |
| `machines/{id}/rpm = {v}`            | Custom Data Collection           | Parameter = spindle_rpm         | Requires custom DC parameter definition            |
| `machines/{id}/spindle_load = {v}`   | Custom Data Collection           | Parameter = spindle_load_pct    | Threshold alerting configurable in SAP DM          |
| `machines/{id}/tool_wear = {v}`      | Custom Data Collection           | Parameter = tool_wear_pct       | Can trigger tool change workflow                   |
| `machines/{id}/oee = {v}`            | Custom Data Collection           | Parameter = oee_pct             | KPI visibility in SAP DM production dashboard      |
| `machines/{id}/flow_rate = {v}`      | Custom Data Collection           | Parameter = flow_rate_l_min     | Filling line quality signal                        |
| `machines/{id}/line_speed = {v}`     | Custom Data Collection           | Parameter = line_speed_u_min    | Packaging throughput signal                        |
| `tanks/{id}/level = {v}`             | Custom Data Collection           | Parameter = tank_level_pct      | Requires custom DC parameter per tank              |
| `tanks/{id}/ph = {v}`               | Custom Data Collection           | Parameter = ph_value            | Quality-relevant; can trigger hold workflow        |
| `tanks/{id}/pressure = {v}`         | Custom Data Collection           | Parameter = pressure_bar        | Process safety signal                              |
| `operators/login = {json}`           | Assign Operator to SFC           | Personnel number from UID map   | Requires operator master data pre-configured       |

---

## 15. Troubleshooting Guide

### 15.1 Server-Side Issues

| Symptom                                  | Likely Cause                              | Resolution                                                                          |
|------------------------------------------|-------------------------------------------|-------------------------------------------------------------------------------------|
| Containers not starting                  | Nesting not enabled on LXC                | Enable nesting in Proxmox LXC options and restart                                   |
| Browser dashboard cannot connect         | WebSocket listener missing or wrong port  | Verify `listener 9001 / protocol websockets` in `mosquitto.conf`; check port 9001 in Compose |
| Node-RED cannot reach broker             | Docker network issue                      | Use service name `mosquitto` not `localhost`; both services must be in `sapdm-net`  |
| Cloudflare Tunnel shows inactive         | Credentials missing or config error       | Check credentials JSON path in `config.yml`; run `docker compose logs cloudflared`  |
| SAP DM cannot connect via tunnel         | Wrong WSS URL or tunnel not active        | Confirm tunnel is Active in Cloudflare dashboard; test with `wscat -c wss://mqtt.yourdomain.com` |
| SAP DM receives no messages              | Topic subscription mismatch               | Verify SAP DM is subscribed to `factory/#`; publish test from Node-RED and check SAP DM logs |
| Messages arriving but not mapped         | DC parameters not defined in SAP DM       | Create custom data collection parameters per signal in SAP DM master data           |

### 15.2 Device-Side Issues

| Symptom                                  | Likely Cause                              | Resolution                                                                          |
|------------------------------------------|-------------------------------------------|-------------------------------------------------------------------------------------|
| No display on boot                       | LVGL BSP not initialised                  | Verify BSP display init call matches Waveshare P4 example firmware                  |
| Display shows but touch unresponsive     | GT911 driver not initialised              | Check BSP GT911 I2C init; confirm GPIO assignments match schematic                  |
| PN532 not detected on I2C scan           | DIP switches in wrong mode or wiring error| Confirm module is set to I2C mode; check SDA/SCL wiring and 3.3V supply             |
| NFC card reads fail                      | Card type unsupported or antenna blocked  | Verify MIFARE Classic card; remove metal objects from behind the module              |
| ESP32-P4 WiFi never connects             | SDIO bridge firmware issue                | Ensure firmware uses the WiFi-via-C6 SDIO stack, not native P4 WiFi calls           |
| MQTT shows disconnected on device        | Cloudflare Tunnel down or hostname wrong  | Check `docker logs sapdm-cloudflared`; verify tunnel is Active in Cloudflare dashboard |
| MQTT rc=-2 on device                     | WSS URL or port unreachable               | Test with `wscat -c wss://mqtt.example.com` from a laptop on the same network  |
| LVGL rendering slow or tearing           | Double buffering not configured           | Enable double buffering in LVGL config; use DMA for RGB transfer via BSP             |

### 15.3 Useful Diagnostic Commands

```bash
# Verify all containers are running
docker compose ps

# Follow all container logs
docker compose logs -f

# Follow tunnel logs only
docker compose logs -f cloudflared

# Subscribe to all factory topics from inside the LXC
docker exec sapdm-mosquitto mosquitto_sub -h localhost -t 'factory/#' -v

# Publish a test machine status message
docker exec sapdm-mosquitto mosquitto_pub -h localhost -t 'factory/machines/cnc01/status' -m 'RUNNING'

# Publish a test RPM value
docker exec sapdm-mosquitto mosquitto_pub -h localhost -t 'factory/machines/cnc01/rpm' -m '4850'

# Test WebSocket connectivity locally
curl -i -N -H "Upgrade: websocket" -H "Connection: Upgrade" http://192.168.1.60:9001

# Test public WSS endpoint (requires wscat: npm install -g wscat)
wscat -c wss://mqtt.yourdomain.com

# Test public WSS endpoint from outside (requires wscat: npm install -g wscat)
wscat -c wss://mqtt.example.com
```

---

## 16. Open Items and Future Enhancements

### 16.1 Known Limitations

- Arduino library ecosystem for ESP32-P4 is newer than for ESP32-S3; some libraries may require patching or migration to ESP-IDF
- Mosquitto TCP port 1883 and Node-RED port 1880 carry no authentication; acceptable for LAN use but must be addressed before demos on shared networks
- Cloudflare Tunnel exposes port 9001 without authentication by default; Cloudflare Access policies must be added before any customer-facing demo on a public network
- SAP DM IoT/MQTT connectivity configuration details (DC parameter definitions, SFC mapping) depend on tenant version and are not fully documented here; requires per-tenant configuration
- No authentication on Node-RED editor; anyone on the LAN can edit flows

### 16.2 Potential Enhancements

**Security and hardening:**
- Cloudflare Access policy on the tunnel hostname to restrict source IPs (e.g., SAP BTP egress range only)
- MQTTS (port 8883) on Mosquitto for encrypted internal traffic
- Basic HTTP auth on Nginx for shared network scenarios
- Mosquitto username/password authentication with separate credentials for SAP DM, Node-RED, and the web dashboard

**Device enhancements:**
- In-app WiFi SSID/password configuration on the device to avoid reflashing per location
- QR code on the Home screen linking to the web dashboard URL
- Persist configuration values to ESP32-P4 flash using the NVS API
- PACK-01 reject counter with visual alert when rejects exceed threshold
- Configurable auto-pulse interval per machine via long-press settings screen

**Infrastructure enhancements:**
- Traefik reverse proxy to consolidate all services under a single port with path routing
- MQTT topic browser screen showing a live tree of all active topics and last-seen payloads
- Node-RED simulation fallback flows pre-built for all asset types

**Demo scenario extensions:**
- LilyGO T4-S3 AMOLED (2.41-inch) as a companion device showing a single KPI in close-up for additional table presence
- OPC-UA simulator screen on the ESP32-P4 for side-by-side protocol comparison
- MCP server on SAP DM side as an extension into agentic AI demo scenarios
- SAP Joule integration via the MCP layer to demonstrate natural language queries against live shop floor data

---

## 17. Glossary

| Term                | Definition                                                                                                   |
|---------------------|--------------------------------------------------------------------------------------------------------------|
| SAP DM              | SAP Digital Manufacturing: cloud-native MES on SAP BTP for production execution and visibility               |
| MQTT                | Message Queuing Telemetry Transport: lightweight publish-subscribe protocol for IoT and machine connectivity  |
| Mosquitto           | Open-source MQTT broker maintained by the Eclipse Foundation                                                 |
| Cloudflare Tunnel   | Outbound-only encrypted tunnel that exposes a local service to the internet via a Cloudflare public hostname; no inbound firewall rules required |
| cloudflared         | The Cloudflare Tunnel client daemon; runs as a Docker container in the Compose stack                         |
| WSS                 | WebSocket Secure: WebSocket protocol over TLS (port 443); used by SAP DM to connect to Mosquitto via the tunnel |
| ESP32-P4            | Espressif dual-core RISC-V 400 MHz microcontroller with MIPI display/camera, USB 2.0 OTG; no onboard radio  |
| ESP32-C6            | Espressif co-processor providing WiFi 6 and BLE 5 for the P4 via SDIO on the Waveshare board                |
| Smart 86 Box        | Waveshare product line in an 86mm wall-panel form factor with integrated IPS display and enclosure           |
| PN532               | NXP NFC/RFID controller supporting ISO14443A/B, MIFARE, FeliCa; used for operator badge scanning            |
| MIFARE              | NXP contactless smart card standard widely used for access control and industrial badges                     |
| LVGL                | Light and Versatile Graphics Library: open-source embedded GUI library; version 9 used on ESP32-P4           |
| BSP                 | Board Support Package: Waveshare-provided driver layer for display, touch, and peripheral initialisation     |
| Cloudflare Tunnel   | Outbound-only encrypted tunnel from the demo-tunnel to Cloudflare's edge, exposing Mosquitto WebSocket publicly  |
| Proxmox VE          | Open-source virtualization platform running LXC containers and KVM VMs                                       |
| LXC                 | Linux Container: lightweight OS-level virtualization used in Proxmox                                         |
| Docker Compose      | Tool for defining and running multi-container Docker applications from a single YAML file                    |
| Node-RED            | Flow-based programming tool for wiring IoT services; runs as a Docker container in the demo stack           |
| WebSocket           | Protocol enabling full-duplex browser-to-server communication; used by MQTT.js and SAP DM to reach Mosquitto |
| MQTT.js             | JavaScript MQTT client library that runs in the browser via WebSocket                                        |
| SFC                 | Shop Floor Control object in SAP DM representing a unit or batch moving through production                   |
| PPD                 | Production Process Designer: SAP DM tool for configuring operation routings                                  |
| BTP                 | SAP Business Technology Platform: cloud PaaS on which SAP DM is hosted                                      |
| QoS                 | Quality of Service level in MQTT (0 = at most once, 1 = at least once, 2 = exactly once)                    |
| OEE                 | Overall Equipment Effectiveness: composite KPI measuring availability, performance, and quality              |
| NVS                 | Non-Volatile Storage: ESP32 key-value store in flash for persisting configuration across reboots             |
| ISA-95              | International standard defining the interface between enterprise and control systems (MES/ERP boundary)      |
| MCP                 | Model Context Protocol: open protocol for connecting AI models to external tools and data sources            |

---

*Confidential — for internal reference only*
