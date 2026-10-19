# Brewery digital twin

Open `/twin/` on the existing dashboard host. This is a local addition to the
MQTT workbench, using the Factory Twin Lab's bundled Three.js 0.180.0 and orbit
interaction with new brewery geometry. No build, CDN, backend, or firmware
change is required. Nginx must be reloaded once to enable the new `/twin/` route.

## Live rig

1. Activate **Brewery GE01** (the standard `config_brewery.json`) on the ESP rig.
2. Open **Digital Twins** in the workbench navigation, then select **Brewery GE01**.
3. Open **Connection** if the broker address needs changing. The page shares the
   workbench's saved WebSocket URL, public site override, and hostname default.
4. Select equipment in the 3D view or the equipment buttons to inspect readings.

The page subscribes to `factory/machines/+/#`, `factory/tanks/+/#`, operator
badge events, retained device availability, and device diagnostics. It requests
`factory/device/request_snapshot = state` only after the subscription succeeds,
on reconnect, or when **Refresh readings** is pressed. That snapshot request is
the only outgoing application message. There are no machine control commands.

Live mode starts with unknown values. Profile `init_val`, `running` and `pulses`
are never presented as live measurements. An MQTT connection alone does not
mean the rig is online. Previous values remain visible but are marked **Last
known** after a disconnect, device offline message, reconnect before a fresh
field snapshot, retained telemetry, or 45 seconds without rig activity.

Telemetry is change-only: unchanged fields stay valid while other rig readings
or diagnostics continue. Each field tracks its reconnect generation. Sparkline
history contains up to 90 timestamped observations, and the activity log holds
30 events. These are browser-session observations, not historian records.
Operator timestamps show browser receipt time; firmware `ts` is uptime.

## Assets and visual signals

| Equipment | MQTT ID | What changes |
| --- | --- | --- |
| Malt Silo | `tanks/malt` | Measured level and low-stock color |
| Mash Tun | `machines/mash01` | Temperature label, stirrer rotation from RPM, gravity |
| Wort Kettle | `machines/boil01` | Temperature, steam above 80°C while running, pressure, progress |
| Fermenter | `machines/ferm01` | Temperature, CO₂ bubbles from rate, pressure |
| Bright Beer | `tanks/bright` | Measured level, temperature, pressure |
| Bottle Filler | `machines/fill01` | Bottle motion from bottles/min while running, flow and foam loss |
| CIP skid | `machines/clean01` | Running state, cleaner temperature and concentration |
| Spent Grain | `tanks/spent` | Measured level and high-fill color |

All five machine status lamps follow `RUNNING`/`STOPPED`; unknown and stale
states use grey. Activity stops when its required signals are unavailable.
Vessel fill is clamped to 0–100% for geometry, while the inspector preserves
the exact received value. Profile threshold bands distinguish low malt/beer
stock from high spent-grain accumulation. Low-stock profiles whose final
threshold is below `max_val` have a healthy upper band. Status colors are
visual indicators, not a process alarm system.

The floor, piping and building are schematic. Moving dots illustrate activity
when connected assets have live operating states; they do not measure transfer
volume, valve state, actual material movement, or batch genealogy. No vessel
capacity, OEE, batch output, or fermentation progress is invented.

## Demo and accessibility

**Demo** runs a deterministic local version of the standard profile's simulation
settings and closes the MQTT connection. `?demo=1` starts directly in demo mode
without opening a broker connection. Returning to **Live rig** creates empty
state and requests fresh telemetry. Demo readings never publish to MQTT.

Drag to orbit, scroll to zoom, right-drag to pan, or use **Top view** and the
camera reset. Equipment buttons provide keyboard access to every asset. The
inspector works even if WebGL is unavailable. Reduced-motion preferences stop
process animations; readings still update. No external font or asset requests
are needed.

## Checks

On macOS, double-click `dashboard/Open Brewery Twin.command` in Finder to start
the local preview and open the live twin in your browser. Keep its Terminal
window open while using the twin; Ctrl+C stops it. Python 3 is required.

For a local preview of the twin selection page:

```sh
python3 dashboard/preview.py
```

Open the printed address. **Live rig** connects to the
public broker override in `assets/site-config.local.js` or the saved browser
preference. The preview does not proxy MQTT. With no override, enter the rig's
public `wss://` address under **Connection**. Stop the server with Ctrl+C.
Use `--open --live` to open the browser directly in live mode.
Use `--twin` to start directly in the brewery demo. The default opens the
Digital Twins library, where both modes can be selected.

From the repository root:

```sh
node --test tests/*.test.cjs
```

With the test dependencies installed in `tests/`, run:

```sh
node tests/brewery-browser-qa.cjs
```

Alternatively point `DM_QA_MODULES` at an external `node_modules` directory.
The browser suite starts its own disposable MQTT/HTTP server. It tests real
WebGL, subscriptions and snapshot requests, all profile mappings, a 2,000-message
burst, stable canvas/cards, threshold states, operator text escaping, offline
readings, reconnect freshness, demo isolation, connection validation and
responsive layouts. Screenshots default to `/tmp/dm-brewery-browser-qa`.

For manual QA use the existing test preview server, which overrides the broker
to its disposable local instance, then open `/twin/?demo=1`. Deployment uses the
normal workbench broker settings. Physical rig operation must be verified on
the intended broker with the brewery profile active.

`brewery.json` is an exact copy of the standard firmware profile; a unit test
detects drift. If the standard changes, copy the updated profile here and rerun
the checks. Third-party code and its license are retained under `vendor/`.
