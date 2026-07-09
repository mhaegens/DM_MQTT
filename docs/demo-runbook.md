# Demo Runbook

Operational guide for running the SAP DM MQTT demo rig at a customer event or internal demo. This covers setup, scenario changes, and common issues you might encounter during a session.

---

## Pre-Demo Checklist

Run through this before the session starts:

- [ ] Power on the device. The display lights up and shows the Home tab within 5 seconds.
- [ ] Home tab shows MQTT badge as "Connected" (green). If gray, see [MQTT status is gray](#mqtt-status-is-gray).
- [ ] At least one machine card on the Machines tab shows "Running" and live metric values are changing.
- [ ] WiFi RSSI on the Debug tab is above -75 dBm. Below that, WSS reconnects become frequent.
- [ ] Server dashboard is accessible at `https://mqttfordm.haegens.be/dashboard/` (or your hostname). Confirm values are updating in real time.
- [ ] Config JSON matches the customer scenario. Check the machine names and types on the Machines tab.

---

## Powering On

1. Connect USB-C power to the device.
2. The display turns on immediately. The LVGL UI loads within 1-2 seconds.
3. WiFi connection begins. MQTT connects after WiFi gets an IP, typically within 5-10 seconds on a known network.
4. The Home tab shows the current WiFi and MQTT status. All metrics begin updating once MQTT connects.

No interaction is needed on boot if the device was previously connected to the correct WiFi network (credentials are stored in NVS and persist across power cycles).

---

## Connecting to a New WiFi Network

Use this when the device needs to connect to a venue or customer WiFi network.

1. Navigate to the **WiFi** tab (last tab, gear-like icon).
2. Tap the **soft-AP toggle** to start the "SAP-DM-Setup" hotspot.
3. On a phone or laptop, join the WiFi network **SAP-DM-Setup**.
4. Open a browser and navigate to `http://192.168.4.1`.

   > **iPhone note:** Do not rely on the captive portal auto-launch. iOS may show a "Sign in to network" prompt but the page often does not load. Open Safari manually and type the URL.

5. Enter the venue WiFi credentials and tap **Connect**.
6. The device disconnects from the AP, connects to the new network, and MQTT reconnects within 30 seconds.
7. Turn off the soft-AP from the WiFi tab if it stayed on.

Credentials are saved to NVS. The next power cycle will connect automatically.

---

## Changing the Demo Scenario

The entire demo content (machine names, metrics, operators) is driven by `sapdm_config.json`. To use a different scenario for a different customer:

### Via the captive portal (recommended)

1. Start the soft-AP and join **SAP-DM-Setup** as described above, then open `http://192.168.4.1` and switch to the **Config** tab.
2. Pick a profile from the list to activate it, or upload a new JSON file from your phone.
3. Uploads are kept **permanently** under their original filename — they appear in the profile list and survive switching profiles and reboots. The SD card is the home of the profile library (the bundled scenarios are copied onto it at boot); with no card inserted, profiles are kept in flash instead.
4. Activating or uploading a profile reboots the device into the new scenario.

### Via the web editor (no SD card needed)

1. Open `https://mqttfordm.haegens.be/editor/` in a browser.
2. Edit the JSON directly in the editor, or paste a new config.
3. Download the file.
4. Copy it to an SD card as `sapdm_config.json` (root of the card, no subdirectory).
5. Insert the SD card into the device and reboot.

### Via SPIFFS (pre-loaded flash)

For a scenario that survives SD card removal:

```bash
cd firmware
idf.py spiffsgen --input spiffs_image --output sapdm_spiffs.bin
idf.py -p /dev/ttyUSBx write-flash 0x410000 sapdm_spiffs.bin
```

Replace `sapdm_spiffs.bin` with your JSON file name. The partition offset `0x410000` matches `partitions.csv`.

---

## Using the Operator Badge (People Tab)

1. Navigate to the **People** tab.
2. Swipe left or right through the badge carousel to select the operator.
3. Tap **Select Operator** (or equivalent button at the bottom).
4. The selected operator's name appears in the shell bar on the Home tab.
5. An operator login event is published to `factory/operators/login` with the operator's name, role, plant, and NFC UID.

If a physical NFC card is available (MIFARE Classic), switch to Physical mode on the People tab and tap the card to the reader.

---

## Button Navigation

The physical BOOT button on the side of the device (GPIO35) provides navigation without touching the screen:

- **Short press:** Go back to the previous screen, or return to the Home tab from any top-level tab.
- **Long press (>600ms):** Step to the next detail visualization in the Detail view. Use this during a walkthrough to cycle through metric visualizations without reaching over to the screen.

---

## Common Issues During a Demo

### MQTT status is gray

The device is connected to WiFi but MQTT is not connected.

Likely cause: the Cloudflare Tunnel is down, or the server stack is not running.

Check:
1. Open `https://mqttfordm.haegens.be/dashboard/` in a browser. If it does not load, the tunnel is down.
2. SSH to the LXC and run `docker compose ps`. If any service is not `Up`, restart it with `docker compose restart`.
3. If the tunnel is running but MQTT is still gray, check `docker compose logs mosquitto` for errors.

The device will automatically reconnect to MQTT within 30 seconds once the broker is reachable again.

### Device shows wrong machines or scenario

The wrong `sapdm_config.json` is loaded.

Check:
- Is the SD card inserted? A missing SD card causes fallback to the embedded default.
- Is the filename correct? It must be `sapdm_config.json`, lowercase, at the root of the SD card. A file in a subdirectory is not found.
- Are the machine `running` values set correctly in the JSON? A machine with `"running": false` shows as stopped even if the scenario implies it should be running.

### Metric values are not updating

The sim_task stopped, or MQTT is disconnected (values update internally but are not published).

Check:
1. If the Home tab MQTT badge is gray, the values are simulating locally but not publishing. See [MQTT status is gray](#mqtt-status-is-gray).
2. If the values are also frozen on the device screen (not just not publishing), reboot the device.

### All machines shown as stopped

The config JSON has `"running": false` for all machines.

Fix: Edit `sapdm_config.json`, set `"running": true` for the machines you want running, and reload the config (SD card reinsert + reboot).

### Scroll on the Machines tab is slow

This is a known limitation. See [Issue 4 in troubleshooting.md](troubleshooting.md#issue-4-machines-tab-scroll-lag). For demo purposes, navigate to a specific machine card before the presenter speaks about it, rather than scrolling live during the presentation.

### Display shows a brief cyan flash on boot

This is expected behavior on the current firmware. It lasts less than 100ms and is not visible once the UI loads. See [Issue 1 in troubleshooting.md](troubleshooting.md#issue-1-cyan-flash-on-boot).

---

## Quick Reference

| Tab | Use for |
|-----|---------|
| Home | KPI overview, connection status |
| Machines | Live CNC/line metrics, start/stop demo |
| Tanks | Tank level fill animation, drag to change |
| People | Operator badge selection, NFC demo |
| Debug | Show broker IP, RAM stats, live MQTT publish log |
| WiFi | Change network, show QR code for provisioning |
