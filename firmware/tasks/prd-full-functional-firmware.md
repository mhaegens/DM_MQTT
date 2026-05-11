# PRD: Full Functional Firmware — DM MQTT ESP32-P4

## Introduction

The device currently runs a visually complete demo UI but large parts of it are not wired to real backend logic. Buttons do not always trigger MQTT publishes. The WiFi screen connects nothing. The People tab shows a scan animation with no operator data. Entity counts are partially dynamic. The JSON config does not include operators. This PRD specifies the work to make every UI interaction functional and every screen live, while establishing a PSRAM-aware memory architecture that keeps the device performant as the entity set grows.

**Target state:** A standalone demo device that boots from a JSON config file in internal flash, renders all entities dynamically, simulates and publishes every metric over MQTT, allows WiFi provisioning through the UI, and works gracefully with no connectivity.

---

## Goals

- Every interactive UI element (buttons, toggles, drag gestures, list items) produces a real effect: state change, MQTT publish, or navigation
- All entities (machines, tanks, operators) are defined in one JSON file loaded at boot; no hardcoded entity data in C source
- JSON stored in SPIFFS internal flash; writable at runtime; SD card replaces it when present; embedded default_config.c is the final fallback
- Machines screen handles 1–10 machines via vertical scroll, 2 cards per row
- Tanks screen handles 1–3 tanks with even distribution
- People tab shows the operator roster from JSON with a Simulate Scan action that publishes to MQTT
- WiFi screen fully wires Scan & Join and Setup via Phone; shell bar pill reflects live connection state
- Device operates fully in isolation (no WiFi, no MQTT) with clear visual indication of disconnected state
- Frame buffers and large entity arrays allocated in PSRAM; internal SRAM kept clear for FreeRTOS + WiFi stack

---

## User Stories

---

### US-001: Operators in JSON Config

**Description:** As a developer, I need operators/people defined in the JSON config alongside machines and tanks so the device renders them dynamically without hardcoded data.

**Acceptance Criteria:**
- [x] JSON schema extended with a `"people"` array at the top level
- [x] Each person object has: `id`, `name`, `role`, `role_key`, `dept`, `plant`, `uid`, `avatar_color`, and a `certs` string array
- [x] `config_loader.c` parses the `people` array and populates `g_badges[]`
- [x] `data_model.c` hardcoded operator init is removed; no operator data in C source
- [x] `default_config.c` embedded fallback JSON includes the 5 demo operators with correct field names
- [x] `MAX_BADGES` respected; excess entries silently skipped with an ESP_LOGW
- [ ] Build succeeds and `g_badges` is populated correctly at boot (verify via UART log)

---

### US-002: SPIFFS Partition for JSON Config

**Description:** As a user, I want the device to store `sapdm_config.json` in internal flash so it survives reboots and can be updated without reflashing firmware.

**Acceptance Criteria:**
- [ ] `partitions.csv` includes a `spiffs` partition of at least 256 KB labelled `storage`
- [ ] `config_loader.c` checks SPIFFS for `/spiffs/sapdm_config.json` first, then SD card, then falls back to embedded default
- [ ] SPIFFS is mounted read-only at boot using `esp_vfs_spiffs_register`; no write path needed for now
- [ ] A build-time script or CMakeLists.txt step packages `default_config.json` into the SPIFFS image
- [ ] `idf.py flash` flashes both firmware and SPIFFS image in one command
- [ ] If SPIFFS file is missing or corrupt the fallback chain still works and logs which source was used (SPIFFS / SD / embedded)
- [ ] Build and flash succeeds; UART log confirms config source at boot

---

### US-003: Dynamic Machines Screen (Vertical Scroll, Up to 10)

**Description:** As a user, I want the Machines screen to show all machines from the loaded config in a scrollable 2-column grid so I can run demos with any number of machines.

**Acceptance Criteria:**
- [ ] Machines screen reads `g_machine_count` from config; renders exactly that many cards, no more
- [ ] Cards are arranged 2 per row; container is an `lv_obj` with `LV_LAYOUT_GRID` or manual y-positioning with scroll enabled
- [ ] Scroll is vertical only; snap-to-row is off (free scroll)
- [ ] With 4 machines the layout is identical to the current 2×2; with 5–10 machines additional rows appear
- [ ] Card content (name, type, metrics, Start/Stop, Pulse buttons) populates from `g_machines[i]`; no hardcoded machine IDs in UI code
- [ ] Cards for machines beyond index 3 render correctly — no off-screen clipping or LVGL allocation failure
- [ ] Tested on device with a config file containing 5 machines and 8 machines

---

### US-004: Dynamic Tanks Layout (1–3 Tanks, Even Distribution)

**Description:** As a user, I want the Tanks screen to distribute however many tanks are in the config evenly across the screen width so the layout always looks intentional.

**Acceptance Criteria:**
- [ ] Tanks screen reads `g_tank_count` from config; renders that many tanks
- [ ] 1 tank: centered, occupies the full width with equal side margins
- [ ] 2 tanks: side by side with equal spacing, each occupying roughly half the width
- [ ] 3 tanks: equal thirds as in the current layout
- [ ] Tank width and hopper proportions scale to available width per tank
- [ ] Sub-metric chips below each tank remain correctly positioned
- [ ] Tested on device with 1, 2, and 3 tanks loaded from config

---

### US-005: People Tab — Operator List + Simulate Scan

**Description:** As a demo operator, I want the People tab to show all configured operators and let me simulate a badge scan that publishes to MQTT so I can demonstrate operator identification without NFC hardware.

**Acceptance Criteria:**
- [ ] NFC ring animation and "Tap Badge" instruction text are removed
- [ ] Tab renders a vertically scrollable list of operator cards; one card per entry in `g_badges[]`
- [ ] Each card shows: avatar circle (with role initial + `avatar_color`), full name, role label, department, plant, certifications as chips
- [ ] A "Simulate Scan" button (80px, full width or prominent at bottom) is always visible
- [ ] Tapping Simulate Scan cycles to the next operator in `g_badges[]` (wraps at end), highlights that card as "active", and publishes `factory/operators/login` JSON payload: `{uid, name, role, plant, ts}`
- [ ] `g_active_badge` global is updated so the Home screen "Active Operator" KPI reflects the simulated scan
- [ ] MQTT publish fires regardless of MQTT connection state (queued/dropped silently if disconnected)
- [ ] "Scans today" counter increments on each simulate press and displays on screen
- [ ] Tab icon and label updated from NFC placeholder to reflect operators (e.g., person icon, label "People")

---

### US-006: WiFi Screen — Full Backend Audit and Fix

**Description:** As a user, I want every action on the WiFi screen to actually work so I can connect the device to any network during a demo without touching the firmware.

**Acceptance Criteria:**

*Scan & Join mode:*
- [ ] "Scan" button triggers `wifi_manager_scan()`; SSID list populates from scan results within 3 s
- [ ] Tapping an SSID from the list navigates to the password entry overlay (`ui_wifi_password`)
- [ ] Submitting a password in the overlay calls `wifi_manager_connect(ssid, pass, cb)`
- [ ] Connection callback updates `g_wifi_connected`, `g_wifi_rssi`, `g_device_ip`, `g_wifi_ssid`
- [ ] Shell bar WiFi pill updates colour and label immediately after callback fires (via `lv_async_call`)
- [ ] Credentials are persisted to NVS; device reconnects automatically on next boot
- [ ] Failed password / wrong SSID shows an error toast on the WiFi screen

*Setup via Phone mode:*
- [ ] Toggling to "Setup via Phone" calls `wifi_manager_start_ap()` and starts the captive portal HTTP server
- [ ] AP SSID `SAP-DM-Setup` becomes visible on a phone within 5 s of toggle
- [ ] Phone connecting and submitting credentials via captive portal triggers STA connect and stops the AP
- [ ] If AP is active and user switches back to Scan & Join, AP is stopped cleanly

*General:*
- [ ] WiFi overlay "Close" / back navigation stops any in-progress scan or AP without crashing
- [ ] All state transitions logged to UART

---

### US-007: Home Screen — Live Data Wiring

**Description:** As a demo operator, I want the Home screen KPI tiles and connection rows to always show accurate live data so I can glance at the device to confirm system state.

**Acceptance Criteria:**
- [ ] "Machines Running" KPI counts `g_machines[i].running == true` across `g_machine_count`; updates each sim tick
- [ ] "Pulses Today" KPI sums all `g_machines[i].pulses`; updates each sim tick
- [ ] "MQTT Messages" KPI shows `g_mqtt_msg_count`; updates on every `MQTT_EVENT_PUBLISHED`
- [ ] "WiFi RSSI" KPI shows `g_wifi_rssi` in dBm ("–" when disconnected); updates every 5 s
- [ ] MQTT status row shows broker hostname + green/red dot + "Connected" / "Disconnected"
- [ ] WiFi status row shows SSID + IP address when connected, "Not connected" when disconnected
- [ ] Active Operator row shows `g_badges[g_active_badge].name` + role; updates when `g_active_badge` changes
- [ ] Uptime counter increments live (seconds since boot), displayed as HH:MM:SS
- [ ] All label updates are dispatched via `lv_async_call` or within the LVGL task to avoid concurrency crashes

---

### US-008: MQTT Detail Screen — Live Data Wiring

**Description:** As a developer or demo operator, I want the MQTT detail overlay to show real broker connection information so I can quickly verify connectivity during a demo.

**Acceptance Criteria:**
- [ ] Broker URL row shows `MQTT_BROKER_URI` from config (not hardcoded string)
- [ ] Connection state shows "Connected" (green) / "Reconnecting" (orange) / "Disconnected" (red) from `g_mqtt_connected`
- [ ] Client ID row shows the active client ID string
- [ ] Message count row shows `g_mqtt_msg_count`, updates live while overlay is open
- [ ] Last publish topic row shows the topic of the most recent publish; stored in a global updated from `mqtt_task`
- [ ] "Reconnect" button calls `mqtt_task_reconnect()` (or equivalent); button is disabled when already connected

---

### US-009: Start/Stop and Pulse Buttons Wired to MQTT

**Description:** As a demo operator, I want the machine card Start/Stop and Pulse buttons to immediately update state and publish to MQTT so the broker reflects button presses in real time.

**Acceptance Criteria:**
- [ ] Start/Stop button toggles `g_machines[i].running`
- [ ] On toggle, immediately publishes `factory/machines/{id}/status` = `"RUNNING"` or `"STOPPED"`
- [ ] Button label and status dot on the card update synchronously on press
- [ ] Pulse button increments `g_machines[i].pulses` by 1
- [ ] On press, immediately publishes `factory/machines/{id}/pulse` = current pulse count as string
- [ ] Both actions work regardless of current sim tick timing (not blocked by sim_task mutex)
- [ ] If MQTT is disconnected the state updates locally; publish is queued or dropped; no crash

---

### US-010: Tank Drag Gesture Wired to MQTT

**Description:** As a demo operator, I want dragging a tank level to immediately publish to MQTT so the broker reflects manual level adjustments in real time.

**Acceptance Criteria:**
- [ ] Vertical drag on a tank body updates `level` metric value in real time
- [ ] Publishes `factory/tanks/{id}/level` throttled to at most once per 400 ms during active drag
- [ ] Publishes a final value on drag release (even if <400 ms since last throttled publish)
- [ ] Wave physics (slosh) responds to drag direction as before
- [ ] Sim task does not overwrite the manually set level during the drag gesture (lock or skip sim for that metric while drag is active)
- [ ] No LVGL event handler blocking or watchdog timeout on long drag

---

### US-011: Simulation and MQTT Publish from Dynamic Config

**Description:** As a developer, I need the sim_task and mqtt_task to iterate over config-loaded entity arrays so adding or removing entities in JSON automatically changes what gets simulated and published.

**Acceptance Criteria:**
- [ ] `sim_task` loops over `g_machines[0..g_machine_count-1]` and `g_tanks[0..g_tank_count-1]`; no hardcoded machine IDs or counts
- [ ] `mqtt_task` (or sim_task publish path) publishes all metrics for all loaded entities each tick
- [ ] Removing an entity from the JSON config stops its simulation and publishing on next boot — no leftover topics
- [ ] Adding a new entity with a custom metric key publishes to `factory/{type}/{id}/{metric_key}` using the `key` field from JSON
- [ ] Operator login publish uses `g_badges[g_active_badge]` data (uid, name, role, plant) as JSON payload
- [ ] Publish path handles disconnected state gracefully (esp_mqtt queue or drop silently)

---

### US-012: Offline-First Operation

**Description:** As a demo operator, I want the device to fully function with no WiFi and no MQTT broker available so I can run a local-only demonstration or hand the device to someone without network setup.

**Acceptance Criteria:**
- [ ] Device boots and reaches the Home screen within 10 s regardless of WiFi state
- [ ] With no WiFi configured: simulation runs, UI updates, all screens are navigable
- [ ] Shell bar WiFi pill shows red dot + "No WiFi"; MQTT pill shows red dot + "Offline"
- [ ] Home screen KPI tiles and status rows reflect disconnected state clearly (no stale "connected" labels)
- [ ] WiFi connect attempt has a timeout (≤15 s) with clear failure feedback on the WiFi screen
- [ ] MQTT reconnect loop uses exponential backoff (5 s → 10 s → 30 s); does not block UI
- [ ] When WiFi connects (e.g., user provisions via phone hotspot mid-demo), MQTT reconnects automatically and pills turn green without requiring a reboot

---

### US-013: PSRAM Memory Architecture

**Description:** As a developer, I want large allocations to live in PSRAM so internal SRAM stays available for the WiFi stack, FreeRTOS, and time-critical code.

**Acceptance Criteria:**

*Frame buffers (Tier 1 — non-negotiable):*
- [ ] LVGL draw buffer(s) are allocated with `MALLOC_CAP_SPIRAM`; confirmed in boot log
- [ ] If BSP allocates buffers internally, verify in BSP init code or override as needed

*Entity arrays (Tier 2 — do in this story):*
- [ ] `g_machines[]`, `g_tanks[]`, `g_badges[]` are heap-allocated via `heap_caps_malloc(…, MALLOC_CAP_SPIRAM)` at boot rather than being static globals
- [ ] All existing code references to these arrays remain valid (same pointer interface)
- [ ] `default_config.c` embedded JSON string lives in flash (`.rodata`); does not occupy RAM

*cJSON parse (Tier 2):*
- [ ] cJSON is configured to use a custom allocator that calls `heap_caps_malloc(…, MALLOC_CAP_SPIRAM)` for its internal allocations during config parse
- [ ] After config parse completes and data is copied into entity structs, cJSON tree is freed

*LVGL heap (Tier 3 — if internal SRAM pressure is observed):*
- [ ] `LV_MEM_CUSTOM` is enabled and backed by a SPIRAM-allocated arena if heap monitor shows internal SRAM below 50 KB free during normal operation

*Verification:*
- [ ] `heap_caps_print_heap_info(MALLOC_CAP_INTERNAL)` at boot shows ≥100 KB free internal SRAM after all init
- [ ] No stack overflow or heap allocation failure watchdog during normal 30-minute operation

---

## Functional Requirements

- FR-1: All entity data (machines, tanks, people) is sourced exclusively from the loaded JSON config; zero hardcoded entities in UI or sim code
- FR-2: JSON load order: SPIFFS `/spiffs/sapdm_config.json` → SD `/sdcard/sapdm_config.json` → embedded default; first found wins; source logged at boot
- FR-3: Machine count: 1–10; Tank count: 1–3; Operator count: 1–10; values capped silently if JSON exceeds limits
- FR-4: Every tap/gesture/button on the UI produces a visible state change, an MQTT publish, or both
- FR-5: Shell bar pills (WiFi, MQTT) are the authoritative at-a-glance connectivity indicators; they must reflect live state within 1 s of a state change
- FR-6: Simulate Scan publishes valid JSON to `factory/operators/login` every time it is pressed
- FR-7: Machine Start/Stop and Pulse buttons publish to MQTT synchronously on press
- FR-8: Tank drag publishes to MQTT throttled at 400 ms; final value published on release
- FR-9: sim_task and mqtt_task are driven by `g_machine_count` and `g_tank_count` — no hardcoded counts
- FR-10: Device reaches a usable UI state within 10 s on a cold boot with no WiFi present
- FR-11: Frame buffers must be in PSRAM; entity arrays must be PSRAM-heap-allocated
- FR-12: MQTT reconnect uses exponential backoff; UI never blocks waiting for network

---

## Non-Goals

- NFC hardware integration (PN532 driver, badge UID scanning) — skipped for this release
- Audio feedback (ES8311) — skipped
- QR code on Home screen — skipped
- SD card write path (JSON config update via UI) — read only from SD; writes are future scope
- SAP DM tenant configuration, signal mapping, SFC setup — out of scope for device firmware
- Web dashboard changes — not part of this PRD
- OTA firmware update — future scope

---

## Technical Considerations

- **LVGL task affinity:** All LVGL object creation and label updates must happen on CPU core 1 (LVGL task) via `lv_async_call`; calling LVGL APIs from `mqtt_task` or `sim_task` (core 0) causes crashes
- **Sim/UI mutex:** `sim_task` and UI detail-view slider share metric values; protect with a FreeRTOS mutex or use atomic float updates consistently
- **cJSON SPIRAM allocator:** set via `cJSON_Hooks` before any `cJSON_Parse` call; restore default hooks after parse to avoid affecting other cJSON users
- **SPIFFS partition table:** `partitions.csv` must be present in `firmware/`; the existing default partition table may not have a `spiffs` entry — add one without shrinking the `app` partition below the build output size
- **Tank drag + sim conflict:** When a drag is active on a tank, set a per-tank `drag_active` flag; sim_task skips level simulation for that tank while the flag is set; clear flag on `LV_EVENT_RELEASED`
- **WiFi callback threading:** `wifi_manager_connect_cb` fires from a WiFi event handler (not the LVGL task); all UI updates from it must use `lv_async_call`
- **Operator JSON publish timestamp:** use `esp_timer_get_time() / 1000` for a monotonic ms timestamp; no RTC dependency

---

## Implementation Order

Stories should be implemented in this sequence to minimise rework:

1. US-001 (operators in JSON) — foundation, unblocks US-005
2. US-002 (SPIFFS) — storage layer, can be done in parallel with US-001
3. US-013 (PSRAM architecture) — do early before arrays grow larger
4. US-011 (sim + publish from dynamic config) — unblocks correct behaviour for all entity screens
5. US-003 (dynamic machines UI)
6. US-004 (dynamic tanks UI)
7. US-005 (People tab)
8. US-009 (Start/Stop + Pulse → MQTT)
9. US-010 (Tank drag → MQTT)
10. US-006 (WiFi screen full audit + fix)
11. US-007 (Home screen live data)
12. US-008 (MQTT detail live data)
13. US-012 (offline-first validation)

---

## Success Metrics

- Device cold-boots to Home screen in ≤10 s regardless of network state
- A config JSON with 6 machines, 2 tanks, and 7 operators renders correctly with no UI overflow or crash
- Every button and gesture on every screen produces a visible state change or MQTT publish
- 30-minute continuous operation with WiFi + MQTT shows no watchdog resets or memory growth
- Internal SRAM headroom ≥100 KB free after boot (measured via `heap_caps_get_free_size`)
- WiFi provisioning via captive portal completes end-to-end on a fresh device in under 2 minutes

---

## Open Questions

- Does the current BSP `bsp_display_start()` already allocate frame buffers in PSRAM? Needs verification before US-013 — if it does, US-013 is simpler.
- What SPIFFS partition size is safe given current firmware binary size? Needs a check of current `idf.py size` output before modifying `partitions.csv`.
- Should the captive portal (`wifi_portal.c`) serve a full HTML form, or is a simple redirect + JSON endpoint enough for the "Setup via Phone" flow?
