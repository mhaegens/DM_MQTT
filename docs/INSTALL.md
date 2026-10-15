# SAP DM MQTT Demo Stack: Installation Guide

> Deployment hostnames, addresses and tunnel names below are examples. Supply your own values locally; private site configuration is not versioned.

This guide walks you through setting up the complete demo server stack from scratch. You do not need prior Linux, Docker, or networking experience. Every command is shown exactly as you should type it. Follow each numbered step in order.

---

## What you are building

Two programs run together on a small virtual server (called an LXC container) inside Proxmox:

| Service | What it does | Where it is reachable |
|---|---|---|
| **Mosquitto** | Receives and delivers MQTT messages | Port 1883 (local TCP) and port 9001 (WebSocket) |
| **Dashboard** (nginx) | Shows a live MQTT monitor and a config editor in your browser | `/dashboard` and `/editor` |

Internet access is provided by **your existing Cloudflare Tunnel** already running on the Proxmox host (the same one that serves `jellyfin.example.com` and other services). You will add one hostname rule pointing at Nginx on this LXC's port 80. No second cloudflared instance is needed.

---

## Part 1: Create the Proxmox LXC

An **LXC** is a lightweight virtual machine. Proxmox is the hypervisor (the software that manages virtual machines). You create the LXC using a script that handles all the configuration automatically.

### Step 1.1: Open the Proxmox web interface

Open a browser and go to your Proxmox web UI: `https://192.0.2.2:8006`. Log in as `root`.

### Step 1.2: Open the Proxmox Shell

In the left panel, click on the name of your Proxmox node (for example `pve`). Then click **Shell** in the top-right area. A terminal window opens inside the browser.

> **Note:** You are now typing commands directly on the Proxmox host, not inside a container yet.

### Step 1.3: Run the LXC creation script

Paste the following single command into the Proxmox shell and press Enter:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/mhaegens/DM_MQTT/main/proxmox/create-lxc.sh)
```

The script will ask you a few questions. The default values (shown in square brackets) are fine for most setups. Press Enter to accept each default, or type your own value.

| Question | Default | When to change it |
|---|---|---|
| LXC container ID | `200` | If you already have an LXC with ID 200 |
| Container hostname | `sapdm-demo` | If you want a different name |
| IP address | `192.0.2.10/24` | If that IP is already taken on your network |
| Gateway | `192.0.2.1` | If your router has a different IP |
| Network bridge | `vmbr0` | Almost never |
| Storage target | `local-lvm` | If your Proxmox uses a different storage name |
| Root password | *(required)* | Type any password, it will not be shown on screen |

When it asks `Proceed? [y/N]`, type `y` and press Enter.

### What the script does

- Downloads the Debian 13 (Trixie) container template (if not already cached)
- Creates an LXC with 2 CPU cores, 2 GB RAM, and 16 GB disk
- Enables **nesting** (Docker requires this to run inside an LXC)
- Starts the container
- Runs `install.sh` inside it automatically

This takes about 2 to 5 minutes depending on your internet speed.

✅ **You should see** a summary ending with `Access URLs:` and three green `[ OK ]` lines.

### Step 1.4: Find the LXC IP address

At the end of the script output, you will see a line like:

```
Dashboard : http://192.0.2.10
```

Note this IP address. You will need it throughout this guide. Replace `<lxc-ip>` with your actual IP in every step below.

You can also find the IP at any time by running this in the Proxmox shell:

```bash
pct exec 200 -- hostname -I
```

(Replace `200` with your LXC ID if you chose a different one.)

---

## Part 2: Install the stack

> **Note:** If you ran `create-lxc.sh` in Part 1, the install script already ran automatically inside the LXC. You can skip to **Step 2.4** to verify the result. Only follow Steps 2.1 to 2.3 if you are installing on an existing Debian 13 server without using the Proxmox script.

### Step 2.1: Open a terminal on your computer

- **Mac:** Press `Cmd + Space`, type `Terminal`, press Enter.
- **Windows:** Press `Win + R`, type `cmd`, press Enter. (Or use Windows Terminal or PowerShell.)
- **Linux:** Open your system terminal.

### Step 2.2: Connect to the LXC over SSH

SSH (Secure Shell) lets you type commands on a remote machine as if you were sitting in front of it. Type:

```bash
ssh root@<lxc-ip>
```

Replace `<lxc-ip>` with the IP address from Step 1.4. For example:

```bash
ssh root@192.0.2.10
```

When prompted, type the root password you chose in Step 1.3 and press Enter. (The password will not be shown on screen.)

✅ **You should see** a prompt like `root@sapdm-demo:~#`.

### Step 2.3: Run the installer

Paste this single command and press Enter:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/mhaegens/DM_MQTT/main/install.sh)
```

This command downloads and runs the installer in one step. It will:

1. Install Docker (the program that runs the three services)
2. Download the repo to `/opt/sapdm-demo`
3. Pull the Docker images (downloads Mosquitto, Cloudflared, and nginx)
4. Start all three containers
5. Print the access URLs

This takes 1 to 3 minutes.

✅ **You should see** a table of containers followed by `[ OK ]  Stack is up.`

### Step 2.4: Verify all containers are running

Run:

```bash
docker compose -f /opt/sapdm-demo/docker-compose.yml ps
```

✅ **You should see** two rows, each with `running` in the Status column:

```
NAME                 STATUS
sapdm-dashboard      running
sapdm-mosquitto      running
```

---

## Part 3: Add ingress rules to your existing Cloudflare Tunnel

You already have a Cloudflare Tunnel running on the Proxmox host that serves other services like `jellyfin.example.com`. You do not need to create a new tunnel or run cloudflared again inside this LXC. Instead, add one hostname rule routing `mqtt.example.com` to Nginx on the new LXC.

Nginx handles both protocols: normal browser requests at the root redirect to `/dashboard/`, while WebSocket upgrades at `/` and `/mqtt` reach Mosquitto. The public MQTT URL stays `wss://mqtt.example.com`.

### Step 3.1: Find your existing tunnel config file

On the **Proxmox host** (in the Proxmox shell or via SSH to the host), run:

```bash
cloudflared tunnel list
```

Note the name of the tunnel you use (for example `proxmox-tunnel` or similar). Then find its config file:

```bash
cloudflared tunnel info <your-tunnel-name>
```

The config file is usually at `/etc/cloudflared/config.yml` or `~/.cloudflared/config.yml`. Open it with:

```bash
nano /etc/cloudflared/config.yml
```

### Step 3.2: Route the hostname to Nginx

Install the current Nginx configuration first. Inside `ingress:`, replace older rules for this MQTT hostname with the rule below, above the final `service: http_status:404` rule. Preserve all unrelated hostnames.

```yaml
  - hostname: mqtt.example.com
    service: http://192.0.2.10:80
```

Do not leave an earlier rule sending this hostname directly to port 9001: a browser root request would reach the MQTT-only listener and can return 502.

Save the file with `Ctrl + X`, then `Y`, then Enter.

### Step 3.3: Add the DNS record

Run this on the Proxmox host:

```bash
cloudflared tunnel route dns demo-tunnel mqtt.example.com
```

This creates the CNAME record in Cloudflare automatically. You should see `Added CNAME mqtt.example.com` in the output.

### Step 3.4: Reload the tunnel

```bash
systemctl reload cloudflared
```

If that does not work on your setup, restart it instead:

```bash
systemctl restart cloudflared
```

### Step 3.5: Verify the tunnel routes traffic

From any device, open:

```
https://mqtt.example.com/
```

✅ **You should see** the MQTT monitor dashboard loading over HTTPS.

---

## Part 4: Verify everything works

Work through these checks in order. Each one confirms a different part of the stack.

### Step 4.1: Open the dashboard in a browser

On any device on the same network as the LXC, open:

```
http://<lxc-ip>/dashboard/
```

✅ **You should see** a live MQTT monitor page with a connection status indicator.

### Step 4.2: Open the config editor

```
http://<lxc-ip>/editor/
```

✅ **You should see** a JSON configuration editor page.

### Step 4.3: Send a test MQTT message

This step requires the `mosquitto-clients` package. If it is not installed on your computer, you can run this test from the LXC itself (in your SSH session):

```bash
mosquitto_pub -h <lxc-ip> -p 1883 -t test/hello -m "world"
```

✅ **You should see** the message `world` appear in the dashboard browser tab you opened in Step 4.1. It may take 1 to 2 seconds.

### Step 4.4: Test the tunnel URL

Open a browser on any device (including a phone with mobile data, not connected to your LAN):

```
https://mqtt.example.com/
```

✅ **You should see** the same MQTT monitor dashboard, now served through the Cloudflare Tunnel over HTTPS.

> **Note:** If this does not work, confirm that Part 3 is complete and that `journalctl -u cloudflared` on the Proxmox host shows `Registered tunnel connection`.

### Step 4.5: Run the smoke test

In your SSH session on the LXC, run:

```bash
bash /opt/sapdm-demo/scripts/smoke-test.sh
```

This script checks every component automatically: containers running, HTTP endpoints responding, MQTT pub/sub working, WebSocket port open, and tunnel connection.

✅ **You should see** a result like:

```
  Result: 5/5 checks passed
  All checks passed.
```

⚠️ If any check shows `FAIL`, read the message next to it. The most common issues are: a container that is not running (`docker compose up -d` to fix), or the tunnel URL not reachable (check Step 3.5).

---

## Part 5: Connecting the ESP32 device

Once the server stack is running and the tunnel is active, the ESP32-P4 demo device can connect to it.

### Step 5.1: Start WiFi provisioning on the device

The device needs to know which WiFi network to join before it can reach the MQTT broker.

1. Hold the **provisioning button** on the device (check the hardware label). The screen shows a provisioning message and a hotspot name.
2. On your phone or laptop, open WiFi settings and connect to the network named `SAP-DM-Setup`.
3. A captive portal page opens automatically (similar to a hotel WiFi login page). If it does not open automatically, open a browser and go to `http://192.168.4.1`.
4. Select your WiFi network from the list, enter the password, and tap **Connect**.

✅ **You should see** the device screen update to show it is connecting to WiFi.

### Step 5.2: Wait for MQTT to connect

After the device connects to WiFi, it automatically connects to `mqtt.example.com` via WebSocket (port 9001) through the Cloudflare Tunnel.

✅ **Within about 10 seconds**, messages from the device appear in the dashboard at `https://mqtt.example.com/dashboard/`.

> **Note:** The device uses WebSocket (not plain TCP) to connect to MQTT. This is what allows the connection to go through the Cloudflare Tunnel. Plain MQTT on port 1883 is for local tools only.

### Step 5.3: Confirm the device is publishing

Open the dashboard and look for topics starting with `sapdm/`. You should see regular updates as the device publishes machine simulation data.

✅ **You should see** rows appearing in the dashboard table with topics like `sapdm/station/status` or similar, with timestamps updating every few seconds.

---

## Troubleshooting quick reference

| Symptom | Most likely cause | Fix |
|---|---|---|
| Dashboard returns 502 or blank | nginx container stopped | `docker compose -f /opt/sapdm-demo/docker-compose.yml up -d` |
| MQTT pub/sub test fails | Mosquitto container stopped | Same as above |
| Tunnel URL does not work | Ingress rules not added or DNS record missing | Redo Part 3 |
| Device does not show in dashboard | Device not connected to WiFi yet | Redo Step 5.1 |
| LXC IP unknown | Need to look it up | `pct exec 200 -- hostname -I` on Proxmox shell |

---

## Updating the stack

To pull the latest code and restart all services, SSH into the LXC and run:

```bash
bash /opt/sapdm-demo/update.sh
```
