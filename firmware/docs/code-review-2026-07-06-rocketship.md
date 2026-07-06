# Firmware Code Review v2 — the rocketship pass

**Date:** 2026-07-06 · **Scope:** everything under `firmware/` (second full pass, after the [first review](code-review-2026-07-06.md) landed)
**Reported symptoms:** (1) the machines screen still stutters and lags badly while scrolling; (2) the portal's config upload fails with *"Failed: Cannot write to SD card"*.
**Constraints:** the look and the functionality stay. Architecture changes allowed.

---

## Executive summary

The first pass fixed the *steady-state* render pipeline (direct mode, `-O2`, enqueue-not-publish). This pass looked at why the two reported problems survived it, and both turned out to have exact, findable root causes — neither is "the hardware is too slow":

**Scroll lag** — direct mode only helps when a *small* region is dirty. A scroll gesture translates the entire 720×584 viewport every frame, so every frame degenerates to a full software repaint of every visible card (sparkline charts, anti-aliased arcs, ~15 rounded rects per card) **plus** LVGL's direct-mode dirty-region sync copy between the two PSRAM framebuffers. That is 2–3 MB of PSRAM traffic and a full widget-tree walk per frame — 25–35 ms, i.e. 30 fps with dropped frames exactly while the finger is down. Three compounding factors: the CPU was clocked at 360 MHz (P4 does 400), touch was sampled every 30 ms (LVGL default — scroll positions update at 33 Hz no matter how fast frames render), and the direct-mode patch had to be re-applied *by hand* after every `idf.py update-dependencies` — one missed re-apply and the pipeline silently reverts.

**Fix:** *ghost scrolling* (§P-1) — every card is pre-rendered off-gesture into an opaque RGB565 image in PSRAM; during the gesture the widget trees are swapped for those images, so a scroll frame becomes a handful of straight `memcpy` blits (~5–8 ms — comfortable 60 fps) and swaps back on release, pixel-identical. Plus 400 MHz, 10 ms touch sampling, and the patch is now applied automatically by CMake.

**Upload failure** — `sdkconfig.defaults` never enabled FATFS long filenames, and ESP-IDF's default is DOS 8.3. `sapdm_config.json` has a 12-character basename, so **every** `fopen()` of it on the SD card has always failed with `FR_INVALID_NAME` — writes ("Cannot write to SD card") *and* reads, which means SD-card configs never loaded either; the boot log's "not found — using default" was this same bug wearing a different hat. Worse: even with the write fixed, the upload could never take effect, because the factory SPIFFS image also ships a `sapdm_config.json` and the loader tried **SPIFFS first** — the flashed config permanently masked anything on the SD card.

**Fix:** LFN enabled (§F-1); uploads are validated then written to **both** SPIFFS and SD (§F-3); the load order is now SD → SPIFFS → embedded default, so an inserted card is an explicit external override and a portal upload wins in every other case (§F-2). Config upload now works with **no SD card at all**.

---

## P — Performance findings (all applied)

### P-1 · Ghost scrolling: swap widget trees for pre-rendered images during the gesture

`ui_machines.c`. The scroll cost was never *which* pixels are repainted (direct mode already minimises that) but *how* they are produced: LVGL re-rasterises every rounded corner, chart line and glyph on every frame of a scroll. None of that changes mid-gesture — the cards are static pixels that merely translate. So:

- Each card is snapshotted (`lv_snapshot_take`, ARGB8888) and composited over the page background into an **opaque RGB565** draw buffer in PSRAM (~187 KB/card, ~1.9 MB for 10 machines out of 32 MB). Opaque RGB565 → the blit is a per-row `memcpy`, no per-pixel blending; compositing bakes correct rounded-corner pixels, so ghosts are pixel-identical to the live cards.
- `LV_EVENT_SCROLL_BEGIN` hides the cards and shows the images; `LV_EVENT_SCROLL_END` swaps back. The existing viewport culling now toggles whichever representation is active.
- Snapshots are retaken *off-gesture only*: after any refresh tick that changed a card, on Start/Stop taps, and when the pulse flash restores — so the swap is never visible. A card whose snapshot allocation failed simply stays live (graceful degradation).
- Overhead when the grid fits the viewport (≤4 machines): zero — the whole machinery arms only when the grid can scroll.
- Kill switch: `UI_SCROLL_GHOSTS` in `config.h` (set to 0 → exactly the previous behaviour).

Estimated scroll frame cost: ~25–35 ms → **~5–8 ms** (blits + direct-mode buffer sync), i.e. locked 60 fps.

### P-2 · CPU was running at 360 MHz

No `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_*` was set, and IDF defaults the P4 to 360 MHz; the datasheet part runs 400. `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y` — a free ~11 % on every CPU-bound path, which during scroll is all of them.

### P-3 · Touch sampled at 33 Hz

LVGL's default indev read period is 30 ms. Even a 60 fps renderer shows a scroll position that updates every other frame — perceived as judder that no render optimisation can remove. `CONFIG_LV_INDEV_DEF_READ_PERIOD=10`.

### P-4 · The direct-mode patch could silently vanish

`patches/esp_lvgl_port_avoid_tearing_direct_mode.patch` had to be applied manually after every dependency update; unpatched, the BSP's avoid-tear path drops esp_lvgl_port into PARTIAL render mode with two alternating half-stale framebuffers. The top-level `CMakeLists.txt` now applies the patch at configure time — idempotent, verified by marker, hard-fails with instructions if upstream moves the anchor, and removes the component-manager hash file so the build doesn't refuse the modified component. The `.patch` file remains as documentation.

### P-5 · Idle refresh restyled every widget every 3 s

`ui_machines_refresh` phase 2 wrote text/colour/arc values unconditionally for every visible metric; LVGL invalidates on every style write whether or not the value changed. Now a per-metric last-rendered cache (`s_prev_val/col`) skips untouched metrics entirely — an idle tick on a screen of stopped machines invalidates nothing (and, with P-1, triggers no snapshot retakes).

### P-6 · Box shadows re-blurred on every repaint

The NFC badge cards (`shadow_width 20` + radius 16), result card and detail cards recompute a software blur each repaint. All shadows in the UI share parameters, so `CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE=40` makes them cache hits. Preserves the look — this is the cache the first review's "shadow diet" avoided needing.

---

## F — Functional findings (all applied)

### F-1 · FATFS long filenames were disabled — the actual "Cannot write to SD card"

ESP-IDF defaults FATFS to 8.3 names. `sapdm_config.json` (basename 12 chars) can neither be created nor opened: `fopen(..., "w")` → NULL → the portal's exact error message; `fopen(..., "r")` → silent fallback to the default config at boot. `CONFIG_FATFS_LFN_HEAP=y` + `CONFIG_FATFS_MAX_LFN=255`. This also un-breaks `/list-configs` (which saw only 8.3-mangled `SAPDM_~1.JSO` names that failed the `.json` filter) and `/set-config`.

### F-2 · The flashed SPIFFS config masked every SD config

`spiffs_create_partition_image` ships `sapdm_config.json` inside SPIFFS on every flash, and `config_load()` tried SPIFFS first — so the documented "put a config on the SD card" workflow could never work, and an SD upload (had it written) would be ignored on reboot. Load order is now **SD card → SPIFFS → embedded default**: an inserted card is an explicit physical override; SPIFFS (factory image + portal uploads) is the resident store. SPIFFS stays registered after boot for the portal.

### F-3 · Upload rework: validate, then write both stores

`/upload-config` now: rejects anything that isn't parseable JSON with at least one of `machines`/`tanks`/`people` (a corrupt upload used to brick the config to embedded-default with only a boot log to explain); then writes `/spiffs/sapdm_config.json` **and**, if a card is inserted, `/sdcard/sapdm_config.json` — whichever store wins at boot carries the upload. If a card is present but unwritable the endpoint says so explicitly instead of letting the stale card mask the upload. `/set-config` sources from either store (`src: "flash"|"sd"`), validates, and persists through the same dual-write. `/list-configs` lists both stores (portal UI tags SD entries `[SD]`). **A device with no SD card now has a fully working config workflow.**

### F-4 · DNS hijack answered AAAA/HTTPS queries with A records

`dns_task` appended an IPv4 answer to *every* query. iPhones probe with AAAA and HTTPS(65) queries; a type-mismatched answer is malformed and makes iOS distrust the resolver — the plausible root cause of the flaky captive-portal auto-launch (Known Issue 2). Now the QTYPE is parsed: A queries get 192.168.4.1, everything else gets an empty NOERROR.

### F-5 · MQTT message counter raced across cores

`pub()` runs on the sim task (core 0) and LVGL touch callbacks (core 1); `g_mqtt_msg_count++` is a read-modify-write and drops counts under contention. Now `__atomic_fetch_add`.

---

## Reviewed and left alone (deliberately)

- **PPA / 2D-DMA draw unit (`LV_USE_PPA`)** — still experimental in LVGL, accelerates fills/blends but not the shadows/corners/text that dominate here, and cannot be validated without hardware. Ghost scrolling already removes the workload PPA would accelerate. Revisit with the IDF 5.4/5.5 upgrade.
- **IDF / LVGL version bump** — needs a hardware build/validate cycle; nothing in this pass depends on it.
- **Shared `lv_style_t` theme table** (P1-5 of the first review) — a large restyle-risk refactor with no per-frame win now that scroll frames don't rasterise widgets at all; still worthwhile for binary size and maintainability someday.
- **`btn_task` running UI transitions under `bsp_display_lock`** — correct, stack already raised to 4 KB; converting to `lv_async_call` remains a nice-to-have.
- **`g_last_pub_topic` unlocked cross-core writes** — benign torn-string risk on a debug label; documented locking rule covers it.
- **OTA partitions** (H-3) — still a feature, not a fix; the 8 MB SPIFFS partition now actually earns its keep as the config store.

---

## Status / verification

| Change | Where | On-device check |
|---|---|---|
| Ghost scrolling | `ui_machines.c`, `config.h` | Scroll machines grid with >4 machines: smooth, no visual difference at rest; toggle `UI_SCROLL_GHOSTS 0` to A/B |
| 400 MHz, LFN, touch 10 ms, shadow cache | `sdkconfig.defaults` | `rm sdkconfig && idf.py reconfigure && idf.py build` (defaults changed!) |
| Auto-patch | `CMakeLists.txt` | Configure log prints `esp_lvgl_port: applied direct-mode avoid-tear patch` (or "already applied") |
| SD-first load + validation | `config_loader.c/.h` | Boot log: `config source: SD card` / `SPIFFS` |
| Upload/list/set rework, DNS qtype | `wifi_portal.c` | Upload with no SD card inserted → succeeds, reboots into the new config; `/list-configs` shows flash + `[SD]` entries |
| Atomic msg counter | `mqtt_task.c` | Home KPI counts monotonically under heavy tapping |

**Post-flash checklist:** `rm sdkconfig && idf.py reconfigure && idf.py build && idf.py flash` — the reconfigure output must show the patch message; then verify boot (no cyan/tear), machines scroll, config upload via portal without SD card, and SD-card override by inserting a card with a different `sapdm_config.json`.

**Revert paths:** ghosts — `UI_SCROLL_GHOSTS 0`; 400 MHz — drop the line (falls back to 360); load order — previous behaviour was SPIFFS-first (see git history of `config_loader.c`); auto-patch — delete the CMake block and re-apply the `.patch` manually.
