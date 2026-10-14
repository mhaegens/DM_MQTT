# Vendored assets

- `sap-logo.svg`: downloaded unmodified from [SAP's official logo asset](https://www.sap.com/dam/application/shared/logos/sap-logo-svg.svg.adapt.svg/1493030643828.svg) on 2026-10-14. Server screens reference this SVG directly. The identical source is stored in the firmware assets directory and compiled once into `sap_logo.c` for fast LVGL rendering.
- `mqtt.min.js`: MQTT.js 5.10.4, downloaded from `https://unpkg.com/mqtt@5.10.4/dist/mqtt.min.js`. License: `MQTT-LICENSE.md`. Served locally; no runtime CDN request.
