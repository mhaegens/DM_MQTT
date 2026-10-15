# Runtime assets

- SAP SVG: identical to `dashboard/html/assets/sap-logo.svg`, sourced from SAP's official asset. See `dashboard/html/assets/ASSET-SOURCES.md` in the repository. It is used as an image; no logo is recreated from UI elements.
- MQTT.js 5.10.4: the exact repository-vendored browser bundle, wrapped to expose one SAP UI5 AMD module, one CommonJS export or a standalone preview global. MIT license is included at `lib/MQTT-LICENSE.md`. There are no runtime CDN requests.
- Device configurations: copied from the sanitized firmware templates. The package builder verifies byte equality.
- Widget implementation and scoped CSS: maintained in this repository. API conventions follow SAP's primary POD 2 examples. The implementation does not copy a third-party widget.
