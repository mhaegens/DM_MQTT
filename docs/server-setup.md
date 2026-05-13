# Server Stack Setup

The server stack runs two Docker containers on a single Debian 13 (Trixie) LXC: Mosquitto (MQTT broker) and Nginx (web dashboard and editor). Public internet access is provided by the existing `homelab` Cloudflare Tunnel running on `pve-homelab` — no cloudflared container is needed in the LXC. A single command installs and starts everything.

This setup matches the homelab architecture: `cloudflared` runs as a host service on `pve-homelab` (`192.168.0.200`), and the demo stack adds its ingress rules to `/etc/cloudflared/config.yml` on that host.

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

After the install, the dashboard and editor are accessible on LAN at `http://<LXC-IP>/dashboard/` and `http://<LXC-IP>/editor/`.

---

## Cloudflare Tunnel Setup

The demo stack reuses the existing `homelab` tunnel running on `pve-homelab`. Add the ingress rules to `/etc/cloudflared/config.yml` on the Proxmox host.

### Step 1: Edit the tunnel config on pve-homelab

```bash
# On pve-homelab
nano /etc/cloudflared/config.yml
```

Add these rules above the final `http_status:404` catch-all. Replace `192.168.0.60` with the LXC IP if you used a different one.

```yaml
ingress:
  # ... existing rules ...

  # SAP DM MQTT demo stack
  - hostname: mqttfordm.haegens.be
    path: /editor.*
    service: http://192.168.0.60:80

  - hostname: mqttfordm.haegens.be
    path: /dashboard.*
    service: http://192.168.0.60:80

  - hostname: mqttfordm.haegens.be
    service: ws://192.168.0.60:9001

  - service: http_status:404
```

**Rule ordering is critical.** The `/editor.*` and `/dashboard.*` path rules must come before the pathless WebSocket catch-all. If the catch-all is listed first, it matches every request for the hostname and forwards everything to Mosquitto — the dashboard and editor become unreachable with no error.

### Step 2: Create the DNS record

```bash
cloudflared tunnel route dns homelab mqttfordm.haegens.be
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
curl -I https://mqttfordm.haegens.be/dashboard/
# expected: HTTP/2 200
```

**MQTT over public tunnel (requires mosquitto-clients 2.0+):**

```bash
mosquitto_sub \
  -h mqttfordm.haegens.be -p 443 \
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

What it does: `git pull origin main`, `docker compose pull`, `docker compose up -d --remove-orphans`.

Mosquitto data and logs are in named volumes and survive restarts.

---

## Log Access

```bash
# Live Mosquitto log
docker compose -f /opt/sapdm-demo/docker-compose.yml logs -f mosquitto

# Nginx access log
docker compose -f /opt/sapdm-demo/docker-compose.yml logs -f dashboard

# Cloudflared log (runs on pve-homelab host, not in the LXC)
# On pve-homelab:
journalctl -u cloudflared -f
```

Common error patterns:

| Message | Cause | Fix |
|---------|-------|-----|
| `cloudflared: no ingress rule matched` | Ingress rule ordering issue in `/etc/cloudflared/config.yml` on pve-homelab | Confirm path rules precede WebSocket catch-all |
| `mosquitto: client disconnected` with no prior connect | TLS handshake failure (client trying TLS on a non-TLS port) | Confirm client is connecting to port 9001 via WSS tunnel, not directly |
| `mosquitto: error: bind: Address already in use` | Port 1883 or 9001 already occupied on the host | Check `ss -tlnp | grep 188` for conflicting processes |
