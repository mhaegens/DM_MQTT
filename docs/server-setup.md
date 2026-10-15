# Server Stack Setup

> Deployment hostnames, addresses and tunnel names below are examples. Supply your own values locally; private site configuration is not versioned.

The server stack runs two Docker containers on a single Debian 13 (Trixie) LXC: Mosquitto (MQTT broker) and Nginx (web dashboard and editor). Public internet access is provided by the existing `demo-tunnel` Cloudflare Tunnel running on `virtualization-host` — no cloudflared container is needed in the LXC. A single command installs and starts everything.

This setup matches the demo-tunnel architecture: `cloudflared` runs as a host service on `virtualization-host` (`192.0.2.2`), and the demo stack adds its ingress rules to `/etc/cloudflared/config.yml` on that host.

---

## Prerequisites

- Debian 13 (Trixie) LXC or VM, accessible on the LAN
- Root or sudo access
- Port 80 open on the LXC (for Nginx, LAN access only)
- Port 1883 open on the LXC (for Mosquitto TCP, LAN only)
- Port 9001 open on the LXC (for Mosquitto WebSocket, LAN + Cloudflare Tunnel)
- A Cloudflare account with a domain you control
- An existing Cloudflare Tunnel running on the Proxmox host (or the same LXC)

---

## Install

Run this on the LXC as root:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/mhaegens/DM_MQTT/main/install.sh)
```

What `install.sh` does, in order:

1. **OS check:** warns if not Debian, fails if not root.
2. **Base packages:** installs `curl`, `git`, `ca-certificates`, `gnupg`.
3. **Docker Engine:** installs Docker via `get.docker.com` if not already present. Checks that the Docker Compose plugin is available.
4. **Repo clone:** clones `mhaegens/DM_MQTT` to `/opt/sapdm-demo`. If already cloned, runs `git pull`.
5. **Runtime directories:** creates `mosquitto/data/` and `mosquitto/log/` (Mosquitto volume mounts).
6. **Docker images + start:** runs `docker compose pull` then `docker compose up -d`.
7. **Status output:** prints `docker compose ps` and the LAN access URLs.

After the install, the full workbench is accessible on LAN; the dashboard and editor are at `http://<LXC-IP>/dashboard/` and `http://<LXC-IP>/editor/`.

---

## Cloudflare Tunnel Setup

The demo stack reuses the existing `demo-tunnel` tunnel running on `virtualization-host`. Add the ingress rules to `/etc/cloudflared/config.yml` on the Proxmox host.

### Step 1: Edit the tunnel config on virtualization-host

```bash
# On virtualization-host
nano /etc/cloudflared/config.yml
```

Add these rules above the final `http_status:404` catch-all. Replace `192.0.2.10` with the LXC IP if you used a different one.

```yaml
ingress:
  # ... existing rules ...

  # SAP DM MQTT demo stack
  - hostname: mqtt.example.com
    service: http://192.0.2.10:80

  - service: http_status:404
```

**Install the current Nginx configuration first.** Replace older path-specific and port 9001 rules for this hostname with this single port 80 rule. Nginx redirects ordinary root requests to `/dashboard/` and proxies WebSocket upgrades at `/` and `/mqtt` to Mosquitto. Existing ESP and SAP DM clients keep using `wss://mqtt.example.com`. Leave unrelated tunnel hostnames unchanged.

### Step 2: Create the DNS record

```bash
cloudflared tunnel route dns demo-tunnel mqtt.example.com
```

### Step 3: Reload the tunnel

```bash
systemctl restart cloudflared
```

---

## Mosquitto Configuration

`mosquitto/config/mosquitto.conf` sets up two listeners:

```
# TCP — for local tools (mosquitto_sub, mosquitto_pub, Node-RED)
listener 1883
allow_anonymous true

# WebSocket — for ESP32-P4, SAP DM, and the browser dashboard (all via Cloudflare Tunnel)
listener 9001
protocol websockets
allow_anonymous true

persistence false
log_dest stdout
```

`allow_anonymous true` means no authentication is required. This is intentional for demo simplicity. To add authentication, create a password file:

```bash
mosquitto_passwd -c /opt/sapdm-demo/mosquitto/config/passwd admin
```

Then add to `mosquitto.conf`:

```
password_file /mosquitto/config/passwd
allow_anonymous false
```

Restart Mosquitto and update the firmware `MQTT_BROKER_URI` in `config.h` to include credentials.

---

## Verification

After install and tunnel setup, verify each component:

**All containers running:**

```bash
docker compose -f /opt/sapdm-demo/docker-compose.yml ps
```

All two services (mosquitto, dashboard) should show `Up`.

**Mosquitto responding:**

```bash
mosquitto_sub -h localhost -p 1883 -t 'test' -C 1 &
mosquitto_pub -h localhost -p 1883 -t 'test' -m 'ping'
```

**Nginx responding:**

```bash
curl -s -o /dev/null -w "%{http_code}" http://localhost/dashboard/
# expected: 200
```

**Public tunnel responding:**

```bash
curl -I https://mqtt.example.com/dashboard/
# expected: HTTP/2 200
```

**MQTT over public tunnel (requires mosquitto-clients 2.0+):**

```bash
mosquitto_sub \
  -h mqtt.example.com -p 443 \
  --capath /etc/ssl/certs \
  --protocol-version mqttv311 \
  --ws \
  -t 'factory/#' -v -C 5
```

---

## Update

Run from anywhere on the LXC:

```bash
bash /opt/sapdm-demo/update.sh
```

What it does: pull source, pull Docker images, apply Compose, then validate and reload Nginx.

Mosquitto data and logs use host bind mounts. The current broker has `persistence false`; mounts alone do not persist broker sessions or message history.

---

## Log Access

```bash
# Live Mosquitto log
docker compose -f /opt/sapdm-demo/docker-compose.yml logs -f mosquitto

# Nginx access log
docker compose -f /opt/sapdm-demo/docker-compose.yml logs -f dashboard

# Cloudflared log (runs on virtualization-host host, not in the LXC)
# On virtualization-host:
journalctl -u cloudflared -f
```

Common error patterns:

| Message | Cause | Fix |
|---------|-------|-----|
| `cloudflared: no ingress rule matched` | Ingress rule ordering issue in `/etc/cloudflared/config.yml` on virtualization-host | Confirm path rules precede WebSocket catch-all |
| `mosquitto: client disconnected` with no prior connect | TLS handshake failure (client trying TLS on a non-TLS port) | Confirm client is connecting to port 9001 via WSS tunnel, not directly |
| `mosquitto: error: bind: Address already in use` | Port 1883 or 9001 already occupied on the host | Check `ss -tlnp | grep 188` for conflicting processes |

For the configuration studio, local runtime assets, same-origin MQTT proxy, latency measurements, and firmware rollout, see [the performance audit](performance-audit-2026-10-14.md) and [Dashboard README](../dashboard/README.md).

For LAN browser access, create ignored `dashboard/html/assets/site-config.local.js` containing only your public connection setting: `window.DM_MQTT_PUBLIC_BROKER = "wss://mqtt.example.com";`. HTTPS deployments otherwise derive WSS from their own hostname. Set `TUNNEL_URL=https://mqtt.example.com/dashboard/` when running the smoke check to include the public route.
