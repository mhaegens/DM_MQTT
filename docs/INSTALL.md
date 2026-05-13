# SAP DM MQTT Demo Stack: Installation Guide

This guide walks you through setting up the complete demo server stack from scratch. You do not need prior Linux, Docker, or networking experience. Every command is shown exactly as you should type it. Follow each numbered step in order.

---

## What you are building

Three programs run together on a small virtual server (called an LXC container) inside Proxmox:

| Service | What it does | Where it is reachable |
|---|---|---|
| **Mosquitto** | Receives and delivers MQTT messages | Port 1883 (local) and port 9001 (WebSocket) |
| **Cloudflared** | Connects your server to the internet through Cloudflare | `mqttfordm.haegens.be` |
| **Dashboard** (nginx) | Shows a live MQTT monitor and a config editor in your browser | `/dashboard` and `/editor` |

---

## Part 1: Create the Proxmox LXC

An **LXC** is a lightweight virtual machine. Proxmox is the hypervisor (the software that manages virtual machines). You create the LXC using a script that handles all the configuration automatically.

### Step 1.1: Open the Proxmox web interface

Open a browser and go to your Proxmox web UI (for example `https://192.168.1.1:8006`). Log in as `root`.

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
| IP address | `192.168.1.60/24` | If that IP is already taken on your network |
| Gateway | `192.168.1.1` | If your router has a different IP |
| Network bridge | `vmbr0` | Almost never |
| Storage target | `local-lvm` | If your Proxmox uses a different storage name |
| Root password | *(required)* | Type any password, it will not be shown on screen |

When it asks `Proceed? [y/N]`, type `y` and press Enter.

### What the script does

- Downloads the Ubuntu 24.04 container template (if not already cached)
- Creates an LXC with 2 CPU cores, 2 GB RAM, and 16 GB disk
- Enables **nesting** (Docker requires this to run inside an LXC)
- Starts the container
- Runs `install.sh` inside it automatically

This takes about 2 to 5 minutes depending on your internet speed.

✅ **You should see** a summary ending with `Access URLs:` and three green `[ OK ]` lines.

### Step 1.4: Find the LXC IP address

At the end of the script output, you will see a line like:

```
Dashboard : http://192.168.1.60
```

Note this IP address. You will need it throughout this guide. Replace `<lxc-ip>` with your actual IP in every step below.

You can also find the IP at any time by running this in the Proxmox shell:

```bash
pct exec 200 -- hostname -I
```

(Replace `200` with your LXC ID if you chose a different one.)

---

## Part 2: Install the stack

> **Note:** If you ran `create-lxc.sh` in Part 1, the install script already ran automatically inside the LXC. You can skip to **Step 2.4** to verify the result. Only follow Steps 2.1 to 2.3 if you are installing on an existing Ubuntu 24.04 server without using the Proxmox script.

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
ssh root@192.168.1.60
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

✅ **You should see** three rows, each with `running` in the Status column:

```
NAME                 STATUS
sapdm-cloudflared    running
sapdm-dashboard      running
sapdm-mosquitto      running
```

> **Note:** `sapdm-cloudflared` may show a different status at this point because the tunnel is not configured yet. That is normal. You will fix it in Part 3.

---

## Part 3: Configure the Cloudflare Tunnel

The Cloudflare Tunnel makes your MQTT broker reachable from the internet without opening firewall ports. This is what allows the ESP32 device and SAP DM to connect from anywhere.

### Step 3.1: Log in to Cloudflare Zero Trust

Open a browser and go to [one.dash.cloudflare.com](https://one.dash.cloudflare.com). Log in with your Cloudflare account.

### Step 3.2: Create a new tunnel

1. In the left sidebar, click **Networks**, then click **Tunnels**.
2. Click **Create a tunnel**.
3. Choose **Cloudflared** as the connector type, then click **Next**.
4. Give the tunnel the name `sapdm-demo`, then click **Save tunnel**.
5. On the next screen, Cloudflare shows you a **Connector ID** and a credentials file. Click **Docker** to see the run command — you do not need to run it. Instead, look for a button or link that says **Download credentials** or shows a JSON block.

> **Note:** The tunnel credentials are a JSON file that acts like a password for your tunnel. Keep it safe and do not share it publicly.

### Step 3.3: Copy the credentials JSON to the LXC

You need to place the credentials file at `/opt/sapdm-demo/cloudflared/<tunnel-id>.json` on the LXC. There are two ways to do this.

**Option A: Copy-paste the content (easiest)**

On the LXC (in your SSH session), run:

```bash
nano /opt/sapdm-demo/cloudflared/<tunnel-id>.json
```

Replace `<tunnel-id>` with the UUID shown in your Cloudflare tunnel dashboard (it looks like `a1b2c3d4-...`). Paste the full JSON content from the Cloudflare dashboard, then press `Ctrl + X`, then `Y`, then Enter to save.

**Option B: Transfer the file from your computer using `scp`**

Open a **second terminal** on your computer (keep the SSH session open in the first one). Run:

```bash
scp /path/to/<tunnel-id>.json root@<lxc-ip>:/opt/sapdm-demo/cloudflared/
```

Replace `/path/to/<tunnel-id>.json` with the path to the file you downloaded, and `<lxc-ip>` with your LXC IP.

### Step 3.4: Configure the tunnel

In your SSH session on the LXC, run:

```bash
cp /opt/sapdm-demo/cloudflared/config.yml.example /opt/sapdm-demo/cloudflared/config.yml
nano /opt/sapdm-demo/cloudflared/config.yml
```

The file looks like this:

```yaml
tunnel: <your-tunnel-id>
credentials-file: /etc/cloudflared/<your-tunnel-id>.json
```

Replace both occurrences of `<your-tunnel-id>` with your actual tunnel UUID. Save the file with `Ctrl + X`, then `Y`, then Enter.

> **Note:** The hostname `mqttfordm.haegens.be` is already in the example config. If you are using a different hostname, update it in the `ingress` section as well. The rule ordering matters: the `/editor` and `/dashboard` path rules must stay above the WebSocket catch-all rule.

### Step 3.5: Restart cloudflared

```bash
docker compose -f /opt/sapdm-demo/docker-compose.yml restart cloudflared
```

Wait about 10 seconds, then move to Step 3.6.

### Step 3.6: Verify the tunnel is connected

```bash
docker logs sapdm-cloudflared 2>&1 | tail -20
```

✅ **You should see** a line containing:

```
Registered tunnel connection
```

If you see `error` or `failed` lines instead, double-check that the tunnel ID in `config.yml` matches the filename of your credentials JSON, and that the JSON file is not empty.

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
https://mqttfordm.haegens.be/dashboard/
```

✅ **You should see** the same MQTT monitor dashboard, now served through the Cloudflare Tunnel over HTTPS.

> **Note:** If this does not work, confirm that Part 3 is complete and that `docker logs sapdm-cloudflared` shows `Registered tunnel connection`.

### Step 4.5: Run the smoke test

In your SSH session on the LXC, run:

```bash
bash /opt/sapdm-demo/scripts/smoke-test.sh
```

This script checks every component automatically: containers running, HTTP endpoints responding, MQTT pub/sub working, WebSocket port open, and tunnel connection.

✅ **You should see** a result like:

```
  Result: 6/6 checks passed
  All checks passed.
```

⚠️ If any check shows `FAIL`, read the message next to it. The most common issues are: a container that is not running (`docker compose up -d` to fix), or the tunnel not yet configured (expected on first run, see Part 3).

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

After the device connects to WiFi, it automatically connects to `mqttfordm.haegens.be` via WebSocket (port 9001) through the Cloudflare Tunnel.

✅ **Within about 10 seconds**, messages from the device appear in the dashboard at `https://mqttfordm.haegens.be/dashboard/`.

> **Note:** The device uses WebSocket (not plain TCP) to connect to MQTT. This is what allows the connection to go through the Cloudflare Tunnel. Plain MQTT on port 1883 is for local tools only.

### Step 5.3: Confirm the device is publishing

Open the dashboard and look for topics starting with `sapdm/`. You should see regular updates as the device publishes machine simulation data.

✅ **You should see** rows appearing in the dashboard table with topics like `sapdm/station/status` or similar, with timestamps updating every few seconds.

---

## Troubleshooting quick reference

| Symptom | Most likely cause | Fix |
|---|---|---|
| `sapdm-cloudflared` not running | Missing or invalid `config.yml` | Redo Part 3 |
| Dashboard returns 502 or blank | nginx container stopped | `docker compose -f /opt/sapdm-demo/docker-compose.yml up -d` |
| MQTT pub/sub test fails | Mosquitto container stopped | Same as above |
| Tunnel URL does not work | Tunnel not registered | Check `docker logs sapdm-cloudflared` |
| Device does not show in dashboard | Device not connected to WiFi yet | Redo Step 5.1 |
| LXC IP unknown | Need to look it up | `pct exec 200 -- hostname -I` on Proxmox shell |

---

## Updating the stack

To pull the latest code and restart all services, SSH into the LXC and run:

```bash
bash /opt/sapdm-demo/update.sh
```
