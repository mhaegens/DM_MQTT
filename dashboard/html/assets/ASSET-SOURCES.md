# Vendored assets

- `sap-logo.svg`: downloaded unmodified from [SAP's official logo asset](https://www.sap.com/dam/application/shared/logos/sap-logo-svg.svg.adapt.svg/1493030643828.svg) on 2026-10-14. Server screens reference this SVG directly. The identical source is stored in the firmware assets directory and compiled once into `sap_logo.c` for fast LVGL rendering.
- `mqtt.min.js`: MQTT.js 5.10.4, downloaded from `https://unpkg.com/mqtt@5.10.4/dist/mqtt.min.js`. License: `MQTT-LICENSE.md`. Served locally; no runtime CDN request.
- `../twin/vendor/three.module.js`, `three.core.js`, and `OrbitControls.js`: Three.js 0.180.0, copied unmodified from the user's local Factory Twin Lab reference (`factory-twin-poc/dist/assets`) on 2026-10-19. MIT license retained as `../twin/vendor/THREE-LICENSE.txt`. The brewery geometry and telemetry adapter are new; no Factory Twin simulation data is used in live mode.
- `../digital-twins/assets/brewery-preview.png`: a static browser capture of the local brewery 3D scene in isolated demo mode, with controls and readings hidden. Used as the Digital Twins library thumbnail; no live data is included.
