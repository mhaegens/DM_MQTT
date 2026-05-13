# Server Stack Setup

The server stack runs three Docker containers on a single Debian 13 (Trixie) LXC (or VM): Mosquitto (MQTT broker), Nginx (web dashboard and editor), and Cloudflared (public tunnel). A single command installs and starts everything.

This setup differs from typical self-hosted setups in one important way: Cloudflared does not run as its own container. Instead, the three ingress rules for this stack are added to an existing Cloudflare Tunnel that is already running on the Proxmox host.

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

The stack uses an existing Cloudflare Tunnel running on the Proxmox host. You add three ingress rules for the demo hostname to that tunnel's config.

### Step 1: Find your tunnel config

```bash
cloudflared tunnel info <your-tunnel-name>
```

The config is usually at `~/.cloudflared/config.yml` or `/etc/cloudflared/config.yml`.

### Step 2: Add the ingress rules

Open the config and add these three rules to the `ingress:` list. Replace `192.168.x.x` with the LXC's LAN IP.

```yaml
ingress:
  # ... your existing rules above ...

  # SAP DM MQTT demo stack
  - hostname: mqttfordm.haegens.be
    path: /editor.*
    service: http://192.168.x.x:80

  - hostname: mqttfordm.haegens.be
    path: /dashboard.*
    service: http://192.168.x.x:80

  - hostname: mqttfordm.haegens.be
    service: ws://192.168.x.x:9001

  # ... your existing catch-all rule below (must stay last) ...
```

**Rule ordering is critical.** Cloudflare evaluates rules top-to-bottom and stops at the first match. The `/editor.*` and `/dashboard.*` path rules must come before the pathless WebSocket catch-all. If the catch-all is listed first, it matches every request for the hostname and forwards everything to Mosquitto. The dashboard and editor become unreachable with no error message.

### Step 3: Reload the tunnel

```bash
cloudflared tunnel reload
# or
systemctl restart cloudflared
```

### Step 4: DNS

In the Cloudflare dashboard, add a CNAME record for `mqttfordm.haegens.be` pointing to your tunnel ID (e.g. `<tunnel-id>.cfargotunnel.com`).

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

All three services (mosquitto, dashboard, cloudflared) should show `Up`.

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

# Live cloudflared log
docker compose -f /opt/sapdm-demo/docker-compose.yml logs -f cloudflared

# Nginx access log
docker compose -f /opt/sapdm-demo/docker-compose.yml logs -f dashboard
```

Common error patterns:

| Message | Cause | Fix |
|---------|-------|-----|
| `cloudflared: unable to authenticate tunnel` | Credentials JSON missing or wrong tunnel ID | Check `cloudflared/` directory and tunnel ID in config |
| `cloudflared: no ingress rule matched` | Ingress rule ordering issue | Confirm path rules precede catch-all |
| `mosquitto: client disconnected` with no prior connect | TLS handshake failure (client trying TLS on a non-TLS port) | Confirm client is connecting to port 9001 via WSS tunnel, not directly |
| `mosquitto: error: bind: Address already in use` | Port 1883 or 9001 already occupied on the host | Check `ss -tlnp | grep 188` for conflicting processes |
