# DM_MQTT performance and accuracy audit — 14–15 October 2026

## Result

The largest confirmed software bottleneck is the firmware's MQTT outbox path. The ESP's hardware capacity alone does not explain the long delays. The source now uses a dedicated transmitter, change-only telemetry, bounded storage, and separate simulation/display/network schedules. The four production browser tools retain their functions and use the Slide Design project's light SAP style with SAP's actual downloaded SVG logo.

The source has been compiled for ESP32-P4 using ESP-IDF 5.4.1. Browser and native logic tests pass. The connected board has now received an application-only update; startup, saved-network persistence and automatic reconnection have been verified. Physical frame-time percentiles and touch-to-broker timing remain unmeasured. The live LXC stack and tunnel configuration were not deployed from this workstation.

## Scope and evidence

Published results are aggregate measurements. Raw logs, device/network screenshots, board backups and deployment-specific settings remain local and are excluded from Git. Evidence filenames below identify those private records; they are not downloadable repository artifacts.

Reviewed the firmware application, MQTT and simulation tasks, WiFi/provisioning, model/config loading and storage, LVGL screens and snapshot pipeline, component/build configuration, all four production browser apps, Mosquitto/Nginx/Compose configuration, tunnel examples, installation/update scripts, and existing architecture/hardware notes. Earlier reviews describe estimated frame times; those estimates are not treated as measurements here.

The visual reference uses white surfaces, navy typography, SAP blue, restrained violet, thin borders, and generous spacing. No browser framework or animation dependency was added.

The configured public endpoint was measured from this Mac using 20 non-retained QoS 1 publications on a unique `audit/latency/...` topic outside `factory/#`: **p50 56.0 ms, p95 69.3 ms, 0 timeouts**. This measures Mac ↔ Cloudflare WSS broker PUBACK, including that route's overhead. It excludes the ESP's radio/SDIO path and SAP DM ingestion. Raw results: public-broker-roundtrip.json (private evidence).

## Main findings and changes

| Priority | Finding | Change / result |
| --- | --- | --- |
| P1 | Queued QoS 0 telemetry can fall behind dramatically | In the installed ESP-MQTT implementation, a connected loop dequeues one outbox item and then polls reads for up to 1,000 ms. The shipped SPIFFS profile generates 27 telemetry values every 3 s, approximately 9/s, even without user activity. With little incoming traffic, a roughly 1/s service path cannot keep up. This is a source-derived estimate, not an ESP benchmark. The new `mqtt_tx` worker writes directly outside the UI; read polling is 25 ms. |
| P1 | An enqueue from a touch callback still waits for MQTT's API mutex | Touch/simulation producers now copy into fixed application storage and notify the transmitter. They never call the MQTT transport/client API. A slow socket write runs only in the worker. |
| P1 | Detail-screen changes were not published until a later simulator pass | Step/slider/random-generation edits publish immediately through the worker. The simulation still advances every 3 s; a separate 100 ms observer catches other model changes. |
| P1 | Values were resent even when unchanged | Machine/tank telemetry is change-only. The transmitter compares the actual two-decimal payload, normalizes negative zero, suppresses identical pending/in-flight/successfully written values, and coalesces unsent changes by topic. A→B→A while B is in flight is correctly preserved. |
| P1 | Reconnect could leave stopped machines missing from the server | Every successful connection invalidates the telemetry cache and publishes a complete snapshot, including stopped machines' status, pulse counts, and metrics. Periodic unchanged-value application heartbeats were removed. |
| P1 | Transport backlog consumed memory and delivered obsolete telemetry | Latest-value capacity is exactly 104 metric topics, matching 10×8 machine and 3×8 tank metrics. Discrete events use a separate 32-entry FIFO; the ESP-MQTT QoS 1 outbox is capped at 8 KB. Outbox-full events are held for retry after PUBACK rather than discarded. Application FIFO overflow and send failures are exposed in diagnostics. |
| P1 | Network tasks called LVGL's asynchronous timer API without its lock | A FreeRTOS queue feeds a 20 ms LVGL timer, with at most four callbacks per pass. WiFi/MQTT status is also polled by that timer, so a full callback queue does not hide connection state. |
| P1 | Configuration file I/O blocked display navigation and touch | Profile listing/activation runs in a lower-priority worker; the LVGL task only renders the result. Completion of a successful save is preserved if the UI queue is temporarily full. |
| P1 | Config validation accepted unsafe structures and values | Boot, profile activation and portal upload reject invalid entity/metric limits, duplicate topic identifiers, invalid ranges/steps, malformed arrays, invalid simulation periods, unsupported enums and colors. Both editors validate before imports/exports; UTF-8 limits are measured in bytes. All shipped profiles remain valid. |
| P2 | One published status/counter state could race between cores | MQTT connection/count/edit-hold scalars use C atomics. Publication snapshots hold the data mutex briefly and perform no network or LVGL work under it. WiFi callbacks are registered before starting WiFi. |
| P2 | Sine simulation periods did not match elapsed time | The simulator advanced the phase by a hard-coded 1.5 s while ticking every 3 s. It now uses the configured interval; missing/invalid periods get a safe default. Random-walk bounds can include zero/negative values. |
| P1 | Only one network was stored, before connection success | Up to ten networks are kept in one validated NVS blob. Credentials are committed after IP acquisition, with most-recent-success ordering and fallback to other saved networks. Failed joins do not overwrite working passwords. The original single-network entry is migrated. WiFi RPC/scan/save operations run in a controller worker, and the screen/phone portal reuse saved credentials. |
| P2 | Tank shading escaped the taper and labels sat in its narrow end | The tank grid and overview share an allocation-free renderer: rounded shoulders, cylindrical body, straight cone, support legs and discharge outlet. The identical vessel geometry clips the live fill/highlight, and labels stay in the readable body. Existing level/drag/target semantics remain. |
| P2 | The C6's default modem sleep adds receive delay | `WIFI_PS_NONE` is explicitly requested for the USB-powered panel through ESP-WiFi-Remote. This is a power/latency tradeoff; the C6 companion remains the radio. |
| P2 | The 400 MHz setting assumed the board's silicon supported it | Default restored to Waveshare's documented 360 MHz. The existing 200 MHz PSRAM, cache configuration, direct-mode double-buffer display pipeline and core separation remain. A later silicon revision should be identified before selecting 400 MHz. |
| P2 | The configured touch-sampling Kconfig symbol did not exist in LVGL 9.5 | Touch read timers are set to 10 ms through the actual LVGL API. The unknown configuration symbol is removed. |
| P2 | Card snapshots blocked taps/navigation, and snapshot failure could expose stale pixels | Snapshots are built one card at a time during idle periods, never while touching/scrolling. Dirty or missing snapshots use live widgets; allocation failure keeps the cache dirty. All charts, gauges and controls remain. Cold-cache scrolling still needs hardware measurement. |
| P2 | MQTT diagnostics were refreshed only when opening their screen | The connection detail screen now polls live statistics only while visible, and updates cached labels/styles only when they change. |
| P2 | Home refresh invalidated unchanged labels/styles | Home labels/styles update only when their rendered state changes. Home/machine observation is 500 ms without advancing the simulation faster. |
| P2 | Tank dragging sent updates only every 400 ms | Drag updates can be queued every 50 ms, and final release still sends the final value. Coalescing and wire-precision deduplication bound the traffic. |
| P2 | cJSON allocator hooks were changed globally during concurrent parses | The PSRAM allocator is initialized once before networking. Profile metadata parsing no longer resets global hooks. |
| P2 | MQTT monitor rebuilt every topic row for every message | Incoming messages are batched every 16 ms, existing rows are reused, sorting occurs only when topics are added, and timestamp ticks update only timestamp cells. Feed/topic limits prevent indefinite DOM growth. |
| P2 | Decorative tank animation rebuilt SVG markup every frame | SVG nodes are reused, animation is limited to 30 Hz, hidden tabs skip animation, and reduced-motion users get static live tank values. |
| P2 | Dependency/CDN drift and a missing server route made behavior inconsistent | Firmware component versions and lockfile are pinned to the compiled stack. MQTT.js is local and pinned. Nginx serves the configuration studio/assets and exposes an optional same-origin `/mqtt` proxy. Browser tools default to public WSS, as requested. Tunnel examples cover all routes; proxy headers follow [Nginx’s WebSocket guidance](https://nginx.org/en/docs/http/websocket.html). |
| P2 | Updating bind-mounted Nginx configuration did not necessarily reload it | `update.sh` validates and reloads Nginx after Compose starts. The smoke test covers the four apps, assets and WebSocket upgrade. |
| P2 | Empty operator configurations could divide by zero on a simulated tap | Operator actions guard an empty badge list; empty configurations remain supported. |
| P2 | Constructed SAP badges and misleading editor errors | The actual SAP SVG replaces text/CSS approximations in production tools and existing HTML previews. The ESP retains its original immutable RGB565 bitmap logo, avoiding an SVG renderer at runtime. An undefined `parseRawJSON()` handler was removed; operator/config text is escaped before HTML rendering. |

## MQTT contract

Existing topic names and scalar `%.2f` payloads remain. Simulation cadence and automatic-pulse pacing remain 3 s and five simulation cycles respectively. Metrics stay QoS 0: they represent current state and obsolete intermediate slider/telemetry values may be coalesced. Reconnect sends the current full state once. The monitor and shop-floor page request a fresh snapshot after their subscription is acknowledged, so opening a new page also initializes unchanged/quiet values. Publish `state` to `factory/device/request_snapshot` for another client that needs the same initial state; requests are coalesced and full snapshots are limited to one per 500 ms. Metrics remain non-retained to avoid stale values from old demo profiles. A subscribe-only external client receives subsequent changes; native retained telemetry for such clients is a separate contract decision.

Machine status, pulse counts and operator login events use QoS 1 and FIFO order within the application queue. QoS 1 is at-least-once delivery, so consumers must tolerate retransmission. Repeated operator taps are events and are not deduplicated. Queue saturation is observable rather than an assertion of lossless delivery. The operator `ts` field remains monotonic milliseconds since boot, preserving its current meaning; it is not an epoch timestamp.

MQTT keepalive is 15 s. The retained availability topic remains `factory/device/status` with an offline last will. Diagnostics/probes run every 10 s on `factory/device/performance` and `factory/device/latency/<client-id>`; these deliberately remain periodic even when plant values are unchanged. The diagnostic `rtt_ms` is the device's own MQTT loopback, not a synchronized-clock subtraction. `queue_ms` measures application wait before calling the transport, not total network latency.

The device keeps **public WSS everywhere**, as requested. No automatic LAN preference is enabled. A separate Mac comparison confirmed that the direct LAN listener reaches the same broker and measured approximately 5 ms, but that route is not used by this firmware. Port 0 uses the URI/scheme's port. Do not mistake the 4B for an Ethernet-equipped ETH-2RO assembly.

## Connected-board follow-up

The programming connection identifies **ESP32-P4 revision 1.3, 32 MB flash**.
Startup confirms **32 MB PSRAM at 200 MHz and a 360 MHz CPU**. The existing
factory partition remains 4 MB at `0x10000`; the bootloader, NVS and SPIFFS
partition layout were preserved. The previous application and boot/settings
were backed up privately under the ignored firmware build directory.

The new firmware loaded the existing five-machine/three-tank profile. Earlier boots exposed intermittent SD initialization failures. The custom mount
ran before the display BSP enabled the GPIO39–45 power domain. The store now
acquires VO4 at 3.3 V, drives GPIO45 low, retries by cycling only card power,
and leaves slot 0 mounted. Repeated updated boots mount the actual card with
**29,813 MB total and 29,812 MB free**, separately from the C6's SDIO slot 1.
The power wiring was checked against the [Waveshare schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4B/ESP32-P4-WIFI6-Touch-LCD-4B.pdf).

After a second network was joined, a reboot reported **two saved networks**,
connected automatically to the newly preferred network, and reconnected MQTT.
The C6 transport works in this observed run, but its reported version is `0.0.0`
and ESP-Hosted warns about a host/coproc version mismatch. The C6 was not flashed;
long-duration RPC/reconnection reliability is not certified by this short test.

The original device logo is restored. In a 210-second observation, 17 diagnostic
samples contained 16 valid ESP loopback RTTs: **p50 91 ms, p95 215 ms**, a sampled
maximum application queue wait of **63 ms**, **zero rejected/failed messages** and
**zero repeated metric payloads** across 19 metric topics. Observed free internal
RAM stayed at or above **42,199 bytes**. This is a short five-machine/three-tank
profile test, not the maximum 104-metric load test or a frame-rate benchmark.
Raw results: device-mqtt-roundtrip.json (private evidence).
The USB capture failure was traced to snapshot rendering on the console's 4 KB
stack. A temporary 20 KB capture worker now produces an actual 720×720 device
screenshot without holding the display lock during the serial transfer.
The final diagnostic protocol uses hexadecimal RLE565 runs over buffered UART, with CRC32 validation of decoded pixels. It replaces the long binary transfer; the final transfer remains unverified on hardware. No baseline
ESP RTT comparison is claimed: the previous firmware was offline during the
short before-update observation.

## Profile storage and live Debug follow-up

Portal uploads are retained as named SD profiles. Different uploads with the
same filename receive numbered names, and byte-identical re-uploads reuse the
existing file. The complete catalog is indexed in PSRAM and displayed in
12-file pages; twelve is a page size, not a storage limit. Invalid saved files
remain visible with a warning. Boot seeding preserves card copies and archives
older unnamed active configurations from either store if their contents are
not already represented. Save-only uploads are available in the portal.

Writes stage, flush, synchronize and verify the bytes before replacing a file;
a backup supports recovery when an interruption leaves the original missing.
Identical content avoids another write. Native sanitizer tests cover 33-file
catalogs, filename conflicts, invalid files, failed writes, recovery, older
unnamed uploads and flash-only fallback after SD synchronization.

The actual portal saved a 104-metric profile twice to SD, retained one named
entry, and retained that entry across restart. The card initially contained
three default templates; no older named custom profiles were found. Historical
uploads that were already overwritten need re-importing. The performance-test
profile remains available for future checks.

The highest load exposed two memory issues beyond ordinary MQTT operation.
The old 512-byte allocation cutoff crowded internal RAM with UI objects. A
64-byte cutoff gives normal objects/buffers access to PSRAM. SPIFFS work/cache
buffers are explicitly kept internal, avoiding repeated flash bounce-buffer
allocation and heap scans. This uses a scoped compile header and preserves the
existing SDK installation and filesystem format. See the SDK's
[SPIFFS allocator](https://github.com/espressif/esp-idf/blob/v5.4.1/components/spiffs/esp_spiffs.c)
and [flash-read implementation](https://github.com/espressif/esp-idf/blob/v5.4.1/components/spi_flash/esp_flash_api.c).

Large TLS allocations follow the PSRAM-aware malloc policy. Internal task
stacks, explicit DMA memory and certificate verification remain in place.
This resolved an SD DMA allocation failure during a WSS handshake with the
104-metric UI and portal active. Five repeated actual catalog requests then
returned all four profiles as valid in **155–531 ms**, including requests during
the handshake. Once flash templates are synchronized to SD, routine catalog
requests avoid rescanning SPIFFS; a flash-only save invalidates that shortcut.

Debug now shows actual broker RTT, queue depth/wait, message rate/count,
WiFi signal/IP, free/minimum internal RAM, PSRAM and largest internal block,
failed/rejected/suppressed counters, SD capacity/profile count and uptime.
Connection, Reconnect, Profiles and Check storage perform real actions. Text
updates are cached and its one-second timer runs only while visible.

The final combined 104-metric/portal observation lasted 90 seconds and saw all
**104 metric topics**, **3,014 messages**, **zero rejected/failed sends** and
at least **51,155 bytes of free internal RAM**. Nine diagnostic samples measured
**p50 73 ms / p95 218 ms** broker loopback. Sampled current queue waits were
26–243 ms. The latched 6,851 ms maximum includes the preceding deliberate
portal WiFi scan/reconnection; it is not a steady connected-state percentile.
This run requested a full state refresh and counted four identical metric
payloads; full snapshots intentionally resend unchanged values. The earlier
140-second change-only observation counted zero identical repeats and measured
p50 74 ms / p95 145 ms. Raw final results:
device-max-load-final.json (private evidence).

The exact original `config_brewery.json` was restored from its saved SD template,
verified active, and the board restarted into its five-machine/three-tank setup.
The restore plus catalog verification took approximately **606 ms**. The named
performance-test profile remains saved on SD.

The actual device screen was confirmed clear. Device captures and raw measurements remain private. The final RLE565 screenshot transfer is not hardware-verified; image-transfer work was paused at the user’s request.
The final 110-second Brewery observation, including USB capture, measured
p50 **126 ms**, p95 **193 ms**, no failed/rejected sends, no identical metric
repeats, sampled queue waits **13–20 ms**, and a boot maximum of **44 ms**.
Free internal RAM was at least **100,443 bytes during capture** and recovered
to over **121 KB afterward**. Raw results:
device-brewery-final.json (private evidence).

## Hardware-specific checks

The [Waveshare 4B documentation](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B) specifies the 720×720 MIPI-DSI panel, C6 SDIO radio and a 360 MHz board frequency. Its current family page mentions 32 MB flash, while the checked-in device profile uses 16 MB. The attached unit confirms 32 MB physical flash; this update preserves the working 16 MB image header and partition layout. Expanding unused flash does not improve the latency path, so the bootloader was not rewritten. GPIO/SDIO settings retain the existing working reference; a changed board revision or C6 firmware must be verified on the actual device. SD storage remains on host slot 0, separate from the C6's slot 1.

Espressif documents that [disabling C6 modem sleep minimizes receive delay](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32c6/api-guides/wifi.html#esp32-c6-wi-fi-power-saving-mode). The queue analysis was verified in the local SDK's [exact ESP-MQTT source revision](https://github.com/espressif/esp-mqtt/blob/cac1552e62b0474c162547b7cce345d7cd1aecfe/mqtt_client.c), including `max_poll_timeout`, the connected outbox loop and enqueue implementation. [LVGL's threading guidance](https://lvgl.io/docs/open/9.4/details/integration/overview/threading) supports dispatching work onto the LVGL task.

## Validation

- ESP-IDF 5.4.1, ESP32-P4 target, Waveshare BSP 1.0.1, ESP-LVGL-Port 2.7.2, LVGL 9.5.0, ESP-Hosted 2.12.7 and ESP-WiFi-Remote 1.5.1: full build passes. The application fits the unchanged 4 MB partition. Pre-existing bounded-string compiler warnings remain.
- Known-network native tests with address/undefined-behavior sanitizers: multiple passwords, preferred-order changes, bounded eviction, network-specific Forget, full-length credentials, invalid blobs and unchanged-write suppression pass. On the board, two saved networks persist across reboot and the preferred network reconnects automatically. Unavailable-network fallback and password-error recovery still need a controlled device test.
- Native C tests with address/undefined-behavior sanitizers: 10,000-update coalescing, 10,000 identical-value suppression, event FIFO/priority, saturation, in-flight A→B→A reversal, reconnect invalidation, failed-send cache reset and string bounds pass.
- Firmware validator: embedded defaults and all four shipped JSON profiles pass; malformed arrays and invalid steps/empty tank metric sets fail safely.
- Browser config validator: shipped profiles plus malformed structures, unsafe topic identifiers, duplicate IDs, limits and UTF-8 overflow pass.
- Real browser against a disposable local MQTT broker: 2,000-message burst, correct final value, persistent topic rows, bounded feed, publishing, round-trip test, tank SVG reuse, escaped operator text, invalid config imports and all four pages at 1440/390 px pass.
- Profile-store sanitizer tests pass, and actual portal upload/deduplication/reboot persistence, full-load portal reads and restoration of the original Brewery profile pass.
- Shell syntax and Git whitespace checks pass. Docker/Nginx runtime smoke checks need to run on the LXC; Docker is not available on this workstation.

Private browser QA screenshots show local synthetic test data. In particular the displayed 24 ms ESP RTT in those screenshots is a fixture, not a hardware measurement. Test setup and repeatable commands: [tests/README.md](../tests/README.md).

## Remaining findings

These do not justify silently changing working demo connections during a latency audit:

- The public broker is configured for anonymous access. TLS protects transport but does not identify clients. Authentication/ACL rollout requires matching ESP, browser and SAP DM credentials.
- WiFi scans currently disconnect/reconnect the station as a workaround for the existing C6 bridge scan behavior. That intentional interruption can briefly interrupt MQTT; continuous background scanning needs validation against the physical C6 firmware.
- The existing editor's direct `http://<device>/upload-config` action is subject to browser CORS/mixed-content restrictions. Use exported JSON with the device's own captive portal for a reliable upload workflow. Remote upload was not presented as verified.
- Each filesystem now uses verified staged writes and recoverable backups. The pair is still not a cross-store transaction, and abrupt power-loss testing on actual FAT/SPIFFS media remains outstanding. SD is the primary library; flash mirrors are best effort and retain the existing SPIFFS filename/space limits. Already overwritten historical uploads cannot be reconstructed from the device.
- The physical NFC screen exists, but there is no PN532 polling driver/task in this repository. Virtual badges and simulated taps are the implemented behavior.
- The shop-floor monitor's metric labels/units/thresholds remain a built-in map; arbitrary new config keys do not automatically carry their metadata to the server. A retained metadata schema is a separate feature.
- Device IDs are fixed per firmware configuration. Multiple devices sharing that client ID would disconnect each other; configure unique identities before cloning the demo rig.
- A cold ghost cache can temporarily fall back to live card rendering. Physical scroll FPS, frame-time percentiles and thermal behavior were not measured; the connected test uses the existing five-machine/three-tank profile. No experimental PPA renderer or unverified overclock was introduced.

## Rollout and acceptance

1. Review the source and compiled application. Deploy the server files, updated Nginx configuration and tunnel ingress together. Run the LXC smoke test; the existing bare-host WSS endpoint must still work for SAP DM.
2. Connect the ESP by its programming USB port. Use an application-only flash for an existing unit to preserve uploaded SPIFFS profiles. The build helper leaves the verified temporary build path available; `idf.py -C <build-snapshot> -p <port> app-flash` flashes only the application. Reconfigure from the new defaults when building elsewhere; an old generated sdkconfig can retain the previous poll/CPU settings.
3. On the intended LAN/hotspot/WireGuard route, record the device's `rtt_ms`, queue wait/depth, suppressed/coalesced/rejected/failed counters and minimum free internal heap. Repeat with 10 machines × 8 metrics and 3 tanks × 8 metrics, scrolling, sliders, pulses and operator taps together. Counters for rejected/failed events should remain zero during normal operation.
4. Confirm unchanged plant values stop generating telemetry; change a value and verify it reaches the broker promptly. Verify the latest value after rapid A→B→A edits and after WiFi/broker interruption. Check complete state for stopped machines after reconnect.
5. Repeat profile loading/saving with and without SD, empty operators, all four shipped profiles and a malformed upload. Confirm no cyan/tearing regression and no stale ghost images.
6. Compare device RTT/interaction-to-broker measurements for direct routed MQTT and Cloudflare WSS. Use those measurements to set the final latency budget; the Mac baseline above cannot certify ESP or SAP DM end-to-end latency.
