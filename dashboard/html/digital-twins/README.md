# Digital Twins library

Open `/digital-twins/` or choose **Digital Twins** in the workbench navigation.
The library currently contains **Brewery GE01**. **Open twin** opens its live
view; **Explore demo** starts its isolated simulation. The brewery view's
**All digital twins** link returns to the library. Its original `/twin/` URL
continues to work, with Digital Twins highlighted in navigation.

The library opens no MQTT connection and loads no 3D renderer. Its preview is
a static capture of the brewery scene in demo mode, with no live readings.
The selected twin manages its own connection and data source.

Twin entries live in `catalog.json`. Add another entry when its twin is
implemented, including its own URLs, preview, profile and equipment counts.
The page renders every entry; no application code change is needed to add
an entry. Only implemented twins are listed.

Double-click `dashboard/Open Digital Twins.command` in Finder to open locally,
or run `python3 dashboard/preview.py --open` from the repository root. The
existing brewery launcher continues to open the brewery directly. Use
`--twin` for the brewery demo, or `--live` for its live view.

Reload Nginx after deploying to enable the library route. The existing brewery
browser suite checks catalog selection, demo isolation, active navigation,
return navigation, thumbnail loading and desktop/mobile layouts before its
telemetry checks: `node tests/brewery-browser-qa.cjs`.
