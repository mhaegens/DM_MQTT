# SAP DM MQTT Shop Floor Simulator

Physical demo rig connecting an ESP32-P4 edge device to SAP Digital Manufacturing cloud via MQTT — no middleware required.

See [SAP-DM-MQTT-Demo-PRD-v3.md](SAP-DM-MQTT-Demo-PRD-v3.md) for full architecture, hardware specs, and SAP DM integration details.

---

## Install (fresh Ubuntu 24.04 LXC)

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/mhaegens/DM_MQTT/main/install.sh)
```

This installs Docker, clones the repo to `/opt/sapdm-demo`, and starts the full stack.

## Update

```bash
bash /opt/sapdm-demo/update.sh
```

Pulls latest from `main`, updates images, restarts the stack.

---

## Stack

| Service      | Port(s)        | Purpose                                      |
|--------------|----------------|----------------------------------------------|
| Mosquitto    | 1883, 9001     | MQTT broker — TCP (internal) + WebSocket (LAN + tunnel) |
| Cloudflared  | —              | Exposes Mosquitto WSS to SAP DM cloud        |
| Node-RED     | 1880           | Flow management + simulation fallback        |
| Dashboard    | 80             | Web UI for live monitoring                   |

## Cloudflare Tunnel setup

After install, configure the tunnel once:

```bash
# 1. Copy your tunnel credentials JSON
cp /path/to/<tunnel-id>.json /opt/sapdm-demo/cloudflared/

# 2. Create config from template and edit
cp /opt/sapdm-demo/cloudflared/config.yml.example /opt/sapdm-demo/cloudflared/config.yml
nano /opt/sapdm-demo/cloudflared/config.yml   # fill in tunnel ID and hostname

# 3. Restart cloudflared
cd /opt/sapdm-demo && docker compose restart cloudflared
```
