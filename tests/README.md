# Project checks

## Configuration validation

From the repository root:

```sh
node --test tests/*.test.cjs
```

The tests cover the four shipped profiles, unsafe topic identifiers, malformed arrays, duplicate IDs, invalid numeric bounds, UTF-8 byte limits and entity limits.

## Native firmware logic

Use a host C compiler with address/undefined-behavior sanitizers:

```sh
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ifirmware/main tests/test_mqtt_tx_buffer.c firmware/main/mqtt_tx_buffer.c \
  -o /tmp/dm-mqtt-buffer-test
/tmp/dm-mqtt-buffer-test

clang -std=c11 -Wall -Wextra -Werror -Wno-deprecated-declarations \
  -fsanitize=address,undefined -Ifirmware/main -I"$IDF_PATH/components/json/cJSON" \
  tests/test_config_validation.c firmware/main/config_validation.c \
  firmware/main/default_config.c "$IDF_PATH/components/json/cJSON/cJSON.c" \
  -o /tmp/dm-config-test
/tmp/dm-config-test firmware/main/spiffs_image/sapdm_config.json \
  firmware/main/spiffs_image/config_bakery.json \
  firmware/main/spiffs_image/config_brewery.json \
  firmware/main/spiffs_image/config_ev_battery.json
```

Known-network storage checks (multiple passwords, most-recent order, capacity,
forget, unchanged-write suppression and malformed persisted data):

```sh
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ifirmware/main tests/test_wifi_known_networks.c firmware/main/wifi_known_networks.c \
  -o /tmp/dm-wifi-known-test
/tmp/dm-wifi-known-test
```

Profile persistence, distinct same-name uploads, catalogs beyond one page,
invalid files, failed staged writes and interrupted-write recovery:

```sh
python3 tests/run-config-store-tests.py
```

Set `IDF_PATH` to the installed ESP-IDF directory first. The test uses real file
operations in an isolated temporary directory, with mocked ESP peripherals.
It also covers recovery of older unnamed active files and a flash-only save
after SD synchronization, so fast SD catalogs cannot hide a fallback profile.

`fixtures/performance-104-metrics.json` exercises ten machines and three tanks
with eight metrics each. Save the current active profile before activating it
on a board, then restore that exact profile after the test.

## Browser + disposable MQTT broker

Install the test-only dependencies in this directory (`npm install`), then run `npm run preview` in one terminal and `npm run browser` in another. The preview binds only to 127.0.0.1:8787 and uses a disposable in-memory MQTT broker, with no production data. The browser suite needs Google Chrome on macOS by default; set `DM_QA_CHROME` to another Chromium executable on other platforms.

Manual preview pages also use the disposable broker by default. The preview server substitutes its local broker setting for `/assets/site-config.js` and never loads the deployment's local override.

Optional variables: `DM_QA_MODULES` (external temporary node_modules), `DM_QA_URL`, `DM_QA_OUTPUT`, `DM_HTML_ROOT`.

The browser suite verifies a 2,000-message burst, last-value accuracy, persistent topic rows, bounded feed, publication, round-trip measurement, persistent tank SVGs, escaped operator text, invalid config rejection, official logo loading, and all four screens at 1440px and 390px.

## Full firmware build

Use ESP-IDF 5.4.1 and the pinned component manifest/lockfile. Run `firmware/tools/build_check.sh` after activating ESP-IDF if the checkout path contains spaces. It copies the source into a temporary path, builds it there, and prints the build location. It does not flash the device.

Physical checks remain required: sustained ten-machine/eight-metric load, scrolling and tank dragging while publishing, reconnect behavior, C6 firmware compatibility, free internal memory, SD-card profiles, and device round-trip latency on the intended network.

## POD 2 MQTT dashboard

`node --test tests/*.test.cjs` includes bounded state, change-only staleness, connection cleanup, snapshot sequencing, and POD adapter lifecycle tests. `DM_QA_MODULES=/path/to/node_modules node tests/pod-browser-qa.cjs` starts its own disposable HTTP/MQTT server and runs Chrome checks for a 5,001-message burst, fixed DOM/page height, exact final values, multiple instances, reconnects, four profiles and the maximum 104 metrics. It also loads real OpenUI5 from the official CDN for container-preservation/teardown checks, so that part needs network access; only tenant-specific POD SDK modules are doubled. QA results/screenshots stay in ignored `pod-plugin/.qa/`.

Build and verify the allowlisted, reproducible upload with `python3 pod-plugin/package.py`. Preview and test harness files are excluded from the ZIP. See [POD extension instructions](../pod-plugin/README.md).
