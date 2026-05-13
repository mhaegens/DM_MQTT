# Dashboard

The dashboard directory contains two single-page web apps served by Nginx, and the Nginx configuration.

```
dashboard/
  html/
    dashboard/        Live MQTT monitor (subscribes to factory/#)
    editor/           JSON config editor (upload / download sapdm_config.json)
    index.html        Prototype mockup (static, not served in production)
    MOCKUP_REFERENCE.md  Full design specification for the prototype
  nginx/
    default.conf      Nginx routing rules
```

---

## What Each App Does

### `/dashboard/` — Live MQTT Monitor

Connects to the Mosquitto broker via WebSocket MQTT and subscribes to `factory/#`. Displays a live view of all machines and tanks, updating in real time as the device publishes telemetry.

Built as a single HTML file with no build step. Dependencies load from CDN:
- `mqtt.js` (Paho-compatible MQTT over WebSocket)
- IBM Plex Sans and IBM Plex Mono from Google Fonts

### `/editor/` — JSON Config Editor

A single-page editor for `sapdm_config.json`. Load a config file, edit it in a text area with basic JSON formatting, and download the result. The downloaded file can be copied to an SD card or loaded into the SPIFFS partition.

---

## How the Dashboard Connects to MQTT

The dashboard uses WebSocket MQTT (`mqtt.js`) to connect to the broker. The connection target is the same Cloudflare Tunnel endpoint used by the firmware:

```
wss://mqttfordm.haegens.be
```

No credentials are required (`allow_anonymous true` in `mosquitto.conf`). The dashboard subscribes to `factory/#` on connection. All machine metrics, tank metrics, and operator login events appear as they are published by the device.

For the full MQTT topic reference, see [docs/mqtt-topic-reference.md](../docs/mqtt-topic-reference.md).

---

## Nginx Routing

`nginx/default.conf` routes requests:

```nginx
location /dashboard/ {
    alias /usr/share/nginx/html/dashboard/;
}
location /editor/ {
    alias /usr/share/nginx/html/editor/;
}
location / {
    return 404;
}
```

The root path returns 404. Only `/dashboard/` and `/editor/` are served. The Cloudflare Tunnel has separate ingress rules that route `/dashboard.*` and `/editor.*` to this Nginx instance on port 80. WebSocket MQTT connections go directly to Mosquitto on port 9001 via the tunnel catch-all rule.

Nginx is only needed because the dashboard and editor require file serving. If you need to add more pages, add a new `location` block here and add a corresponding ingress rule in the Cloudflare Tunnel config.

---

## Local Development

To run the dashboard locally without the Cloudflare Tunnel:

1. Start the server stack:

   ```bash
   cd /opt/sapdm-demo
   docker compose up -d
   ```

2. Open in a browser:

   ```
   http://<LXC-IP>/dashboard/
   http://<LXC-IP>/editor/
   ```

The dashboard will connect to `ws://localhost:9001` for MQTT in local development. You may need to edit the broker URL in `dashboard/index.html` if the dashboard is hardcoded to the tunnel hostname.

---

## Design System

The dashboard uses the same design tokens as the LVGL firmware UI (`ui_theme.h`), ensuring visual consistency between the device screen and the browser:

| Token | Value | Use |
|-------|-------|-----|
| `--hi` | `#0070F2` | SAP blue, primary accent |
| `--ok` | `#107E3E` | Green, healthy / within threshold |
| `--err` | `#BB0000` | Red, alert / over threshold |
| `--bg` | `#F2F4FA` | Page background |
| `--tx` | `#1D2D3E` | Primary text |

For the full design specification including layout grids, animation specs, and component details, see [html/MOCKUP_REFERENCE.md](html/MOCKUP_REFERENCE.md).
