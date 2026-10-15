# MQTT Live Dashboard — POD 2 extension

A read-only MQTT dashboard for SAP Digital Manufacturing POD 2. One persistent view shows machine status, metric gauges/trends, conical vessel levels, the latest operator badge, and device diagnostics. Incoming messages update existing values and SVG elements. They do not append dashboards or create a message history list.

## Install

1. Build the upload: `python3 pod-plugin/package.py` from the repository root.
2. In **Manage PODs 2.0 / POD Designer**, open Extensions and create an extension.
3. Name: **MqttDashboard**. Namespace: **custom/pod2/mqttdashboard**.
4. Upload `pod-plugin/build/dm-mqtt-pod2-v1.0.0.zip`. It has `extension.json` at ZIP root, with both widgets and actions arrays.
5. Add **MQTT Live Dashboard**, under **Connected Shop Floor**, to your POD page.
6. Set **Public broker URL** to your `wss://` endpoint. The source deliberately has no deployed hostname, account, password or tenant configuration.
7. Set **Device profile** to `brewery`, `factory`, `bakery` or `ev_battery`, matching the device. For another scenario, paste its device JSON into **Custom device profile**. Apply/publish the POD and reopen runtime after configuration changes.
8. Confirm **Live readings** and change a value on the device. The plugin requests a complete snapshot after its subscription is acknowledged, including quiet/stopped assets.

All profiles use the firmware's actual keys, ranges, labels, units, colors and thresholds. Up to ten machines, three tanks and eight metrics per asset are supported. Choosing the wrong profile does not create arbitrary cards; an unmapped-signal count points to the mismatch.

The extension connects directly to an anonymous WebSocket MQTT broker, matching this project's demo broker. HTTPS POD runtime requires WSS. Embedded URL credentials are rejected. Configure any deployment-specific connection policy in the tenant; its Content Security Policy must permit the broker's WebSocket origin. No live SAP tenant, production processes, SFCs or master data are changed by this widget.

## Configuration

| Property | Default | Meaning |
| --- | --- | --- |
| Public broker URL | empty | Deployment-specific WebSocket MQTT address; set in POD Designer. |
| Topic prefix | `factory` | Prefix shared with the device. No MQTT wildcards. |
| Device profile | `brewery` | `brewery`, `factory`, `bakery`, `ev_battery`. |
| Custom device profile | empty | Optional full JSON configuration, overriding Device profile. |
| Dashboard title | `Shop floor live` | Header label. |
| Device timeout | `30` | Seconds without device traffic before showing stale readings. Minimum 15. |
| Show connection details | true | Collapsible queue/error/memory information. |

The public broker address is intentionally not inferred from the SAP tenant hostname. Set it explicitly. Repeated operator events increment the event count; they are not discarded as duplicate metric readings.

## Accuracy and lifecycle

- Unknown values display a dash until received. Initial profile values are sample data only in Designer/standalone simulation mode.
- A retained online status alone does not mark readings as fresh. The dashboard waits for actual telemetry, diagnostics or another valid device event.
- Unchanged metrics may remain quiet indefinitely while device diagnostics confirm the device is alive. Staleness is based on device presence/traffic and connection state, rather than incorrectly expiring quiet metrics individually.
- **Device round trip** is the ESP's reported broker loopback, not SAP DM processing time. Missing measurements show a dash.
- Each metric stores at most 60 changed values. Sparkline samples are changes, not equally spaced seconds. Raw values remain visible outside the configured range; gauge geometry is clipped to its range.
- A single animation-frame callback batches renders. Inactive tabs skip rendering; pending values stay bounded. A one-second visible refresh updates status/rate information.
- Repeated `_createView`/render callbacks reuse the root controls. UI5 HTML control ID and HTML root ID match; no `setContent` calls occur during incoming signals.
- Reconnect clears old readings, creates one subscription and requests a fresh snapshot. Exit removes MQTT listeners, closes the connection, aborts pending initialization and cancels UI timers/frames.
- Multiple widgets use independent clients and SVG clip IDs. Incoming operator/configuration labels are rendered with textContent.
- The only publication is non-retained `state` to `<prefix>/device/request_snapshot`, on subscribe or when **Refresh readings** is selected. The plugin does not send device control commands.

## Preview and verification

Serve `pod-plugin/` using a local static server and open `preview/`, for example:

```sh
python3 -m http.server 8788 --bind 127.0.0.1 --directory pod-plugin
```

Open `http://127.0.0.1:8788/preview/`. It starts with clearly labeled simulated readings. The connection field uses only memory and is not saved into the source or ZIP. The preview shares the exact state, connection and DOM renderer used by the POD extension; it does not emulate the complete SAP tenant runtime.

From the repository root:

```sh
node --test tests/*.test.cjs
DM_QA_MODULES=/path/to/test/node_modules node tests/pod-browser-qa.cjs
python3 pod-plugin/package.py
```

The browser suite checks thousands of real MQTT updates against a disposable broker, fixed card/node counts and page height, final values, reconnect snapshots, independent instances, safe operator text, profile switching and narrow layouts. It also runs the actual adapter inside real OpenUI5 HTML/VBox controls and checks preservation after ten invalidations and complete teardown; only the tenant-specific POD SDK is doubled. That harness downloads OpenUI5 from its official CDN and records the tested version. These checks do not certify installation in an authenticated SAP tenant; validate the ZIP in the target tenant before replacing a demo POD.

The package includes only an explicit allowlist of runtime files and this README. Preview/test hooks, screenshots, local settings and deployment notes are excluded. Builds are reproducible and accompanied by a SHA256 file.

## API references

The widget follows [SAP's POD 2 sample structure and extension registration](https://github.com/SAP-samples/digital-manufacturing-extension-samples/tree/main/dm-podplugin-extensions), including WidgetProperty editors and the Widget lifecycle. The visual style follows this repository's web workbench: white surfaces, navy typography, blue/violet accents and restrained status colors. See assets/SOURCES.md for third-party asset provenance.
