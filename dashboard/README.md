# SAP DM Demo Workbench

Four buildless browser apps share the Slide Design project's SAP light style, local runtime assets, and an official SAP SVG logo.

The workbench uses one shared navigation header, with live connection status in the same row. Configuration pages put their file actions in a compact toolbar. The shop floor has spaced machine cards and responsive vessel cards; its tanks and the studio preview share `assets/tank-vessel.js`, which follows the ESP's rounded vessel, conical base, legs, outlet, level marks, and status colors. Live tank updates preserve the SVG and repaint on incoming readings.

| Path | Function |
| --- | --- |
| `/dashboard/` | MQTT live feed, last values, publishing, broker selection, round-trip measurement, device availability and device diagnostics |
| `/shopfloor/` | Live machine, tank, and operator visualization |
| `/config-editor/` | Visual configuration studio with the existing device preview, templates, simulation, JSON import/export, and autosave |
| `/editor/` | Existing form/modal JSON editor and device upload action |

Nginx serves `/assets/` and proxies `/mqtt` to Mosquitto's WebSocket listener on 9001. The browser defaults to WSS on the page hostname. For LAN access, configure your public endpoint in ignored `html/assets/site-config.local.js` as `window.DM_MQTT_PUBLIC_BROKER = "wss://mqtt.example.com";`. Nginx serves it through `/assets/site-config.js`; only public browser settings belong there, never passwords or keys. The monitor's broker field can override this for all workbench pages on the same origin; the same-origin `/mqtt` proxy remains available as an explicit alternative. Browser QA explicitly selects its disposable local broker before page scripts connect.

Update the tunnel ingress using `../cloudflared/config.yml.example` so every page, asset and `/mqtt` route reaches Nginx. The HTTP rules must precede the legacy WebSocket catch-all. The root LAN URL redirects to `/dashboard/`; use `/dashboard/` explicitly on the public hostname.

The monitor and shop-floor page request a fresh state snapshot after subscribing, then receive change-only telemetry from the device. The monitor updates existing topic rows and batches incoming messages every 16 ms. Its feed holds the latest 100 messages and its topic map is capped at 2,000 topics. Device diagnostics arrive on `factory/device/performance`; the displayed ESP round trip is a device-to-broker loopback and does not measure SAP DM processing. The separate 20-message browser test reports browser-to-broker p50/p95.

Runtime assets are local: MQTT.js 5.10.4 and SAP's SVG. No CDN or font download is required. See `html/assets/ASSET-SOURCES.md` for provenance.

Run `node --test tests/config-validation.test.cjs` from the repository root for config checks. See `../tests/README.md` for browser and firmware checks. The full audit, remaining limitations, and rollout steps are in `../docs/performance-audit-2026-10-14.md`.
