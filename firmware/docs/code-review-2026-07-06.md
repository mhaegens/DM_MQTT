# Firmware Code Review — ESP32-P4 SAP DM Simulator

**Date:** 2026-07-06 · **Scope:** everything under `firmware/` (~8,300 LOC C, ESP-IDF 5.3, LVGL 9)
**Goal:** identify optimisations across the whole codebase, with a mandate to raise performance significantly. Migration to another language/runtime was explicitly on the table. Hard constraints: the look and the functionality stay.

---

## Executive summary

The codebase is in good shape structurally: clean module boundaries, a sensible PSRAM strategy, correct `lv_async_call` trampolines for cross-task UI updates, staged MQTT backoff, and unusually good documentation. The performance problem is **not** the language and **not** the CPU — it is three specific architectural choices:

1. **The build ships at `-Og`.** No `CONFIG_COMPILER_OPTIMIZATION_*` is set, so ESP-IDF defaults to Debug. The LVGL software renderer — the hottest code on the device — is running unoptimised. One line in `sdkconfig.defaults` buys an estimated 20–40 % render speedup for free.

2. **Full-refresh rendering makes every pixel pay for any pixel.** `CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH=y` means a single 8 px status dot changing colour triggers a full 720×720 software repaint (~1.04 MB written to PSRAM plus a full widget-tree walk) every time. During a scroll gesture this happens every frame. Switching the avoid-tear path from *full-refresh* to *direct mode* keeps the double-buffered vsync swap (so the cyan-flash fix survives) but redraws only dirty regions. This is the single biggest lever in the codebase and it's a configuration change, not a rewrite.

3. **MQTT publishes run on the LVGL render task.** Tank drags, Start/Stop taps, pulse taps and NFC selections all call `esp_mqtt_client_publish()` directly from touch callbacks. That call takes the client mutex and performs a synchronous TLS/WSS write — the UI thread stalls on network backpressure, which is exactly when it happens (drag publishes are throttled to one per 400 ms *during the gesture*). Publishes must be enqueued, not sent inline.

**On migration: don't.** A Rust/Slint or MicroPython rewrite produces the same pixels through the same (or a slower) software rasteriser while forfeiting the Waveshare BSP, the ST7703 DSI driver, GT911 touch and the esp_hosted C6 WiFi bridge — all C, all mature, all load-bearing. The bottleneck is the render strategy, not the language; a rewrite moves the risk, not the frame time. The right "migration" is staying on C/ESP-IDF and upgrading IDF + esp_lvgl_port/LVGL to unlock the P4's PPA (Pixel Processing Accelerator) draw unit. Full analysis in §6.

Applying P0-1 through P0-4 below should take UI interactions from "misses the 33 ms frame budget on every change" to smooth dirty-region updates, and eliminate gesture jank entirely.

---

## 1 · What's already good

Worth stating so it doesn't get refactored away:

- **Module layout** — `data.c` owns state, screens own widgets, tasks own timing. Easy to navigate.
- **PSRAM discipline** — entity arrays in SPIRAM, draw buffer in internal DMA-capable SRAM, cJSON allocator redirected to PSRAM during parse, 80 KB internal reserve for esp_hosted DMA. All correct and documented.
- **Cross-task UI updates** — every WiFi/MQTT event reaches LVGL via `lv_async_call` with heap-allocated payloads and failure-path `free()`. This is the pattern most ESP32+LVGL projects get wrong.
- **Config pipeline** — SPIFFS → SD → embedded default fallback chain with a clear result enum; captive-portal config upload/select is a genuinely nice demo feature.
- **Docs** — `firmware-architecture.md`, `sdkconfig-reference.md` and `troubleshooting.md` explain *why*, not just *what*. Rare. (Some entries are now stale — see H-7.)

---

## 2 · P0 — Performance: the big levers

### P0-1 · Build at `-O2` (one line, ~20–40 % render speedup)

`sdkconfig.defaults` sets no compiler optimisation level, so the generated `sdkconfig` uses ESP-IDF's default: **Debug (`-Og`)**. Every hot path — LVGL's blend/fill loops, flex layout, cJSON, TLS — runs unoptimised.

```
# sdkconfig.defaults
CONFIG_COMPILER_OPTIMIZATION_PERF=y
```

Then `rm sdkconfig && idf.py reconfigure && idf.py build` (per your own Issue 5 procedure). LVGL's software rasteriser typically gains 20–40 % from `-Og` → `-O2`. Zero risk, zero code changes. Do this first so every other measurement happens against the real baseline.

### P0-2 · Replace FULL_REFRESH with direct mode (the order-of-magnitude lever)

Current config (`sdkconfig.defaults:79-81`):

```
CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2
CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH=y
```

Full-refresh mode marks the whole screen dirty on *any* invalidation. Consequences today:

- The home timer updates four KPI labels every 3 s → full 720×720 repaint (1,036,800 px ≈ 1.04 MB written into the PSRAM framebuffer) plus a complete widget-tree render, including every rounded corner, gradient and soft shadow on screen.
- Scrolling the machines grid invalidates every frame → full repaint per frame. This is Known Issue 4, and it's why the card-hiding workaround and the 30 fps cap exist.
- The tanks tab's 500 ms refresh timer unconditionally invalidates the custom-drawn tank widgets → two full repaints per second while the tab is open, even when values barely moved.

**Fix:** keep `AVOID_TEAR` + 2 framebuffers (this is what prevents tearing *and* the cyan flash) but use **direct mode** instead of full-refresh. In direct mode LVGL renders only dirty regions straight into the active framebuffer; the swap still happens on vsync; the port syncs dirty regions between the two buffers. A KPI label update then costs a few thousand pixels instead of a million.

You already maintain a patch against `esp_lvgl_port` (`patches/esp_lvgl_port_avoid_tearing_full_refresh.patch`) — and the patch context itself shows the port's `direct_mode` branch immediately below the code you added. Two routes:

- If the Waveshare BSP exposes a direct-mode Kconfig (upstream Espressif BSPs offer `FULL_REFRESH`/`DIRECT_MODE` as a choice under `AVOID_TEAR`), flip it there.
- If it doesn't, adapt your existing patch to set `flags.direct_mode = 1` for the avoid-tearing path instead of `full_refresh = 1`. Your current patch becomes unnecessary.

Validate on-device that the cyan flash stays gone (the root-screen background pre-set in `main.c:117-121` still runs before first render, so it should). After this lands, revisit two things it obsoletes:

- The 33 ms timer cap (`main.c:111`) can likely return to ~15–20 ms — dirty-region frames are cheap.
- The card-hiding workaround (see P0-4) becomes optional.

### P0-3 · Get MQTT publishes off the LVGL task

`esp_mqtt_client_publish()` with QoS 0 acquires the client mutex and writes synchronously through the WSS/TLS transport in the *caller's* context. Callers today include:

| Call site | Trigger |
|---|---|
| `ui_tanks.c:321` | tank drag, every 400 ms **mid-gesture** |
| `ui_tanks.c:331` | tank drag release |
| `ui_machines.c:90` | Start/Stop tap |
| `ui_machines.c:120` | Pulse tap |
| `ui_nfc.c:142,160` | operator select / simulated tap |
| `ui_tank_overview.c:160` | fill/drain action |

All of these run on the LVGL render task. When the tunnel is slow or the C6 SDIO link is busy, the render task blocks — dropped frames exactly during touch interaction, plus lock contention with the esp_mqtt task on core 0.

**Fix:** replace `esp_mqtt_client_publish` with `esp_mqtt_client_enqueue(..., store=true)` inside `pub()` (`mqtt_task.c:122-126`) — the outbox then hands the write to the MQTT task — or, if you want bounded memory, a small FreeRTOS queue drained by a publisher task on core 0. Either way the UI thread never touches the socket. `sim_task` publishes can stay synchronous (they're already on core 0 and not latency-sensitive), though enqueue is fine there too.

### P0-4 · The scroll-culling workaround fights LVGL's flex layout

`apply_card_visibility()` (`ui_machines.c:557-574`) hides off-screen cards with `LV_OBJ_FLAG_HIDDEN` — but the cards live in a `LV_FLEX_FLOW_ROW_WRAP` grid (`ui_machines.c:611-614`), and **flex layout skips hidden children**. Hiding the top row makes the remaining cards reflow upward, which changes which cards are "in view", which the scroll callback then re-hides — a feedback loop between the scroll handler and the layout engine. It happens to look acceptable today, but it's fragile and causes extra layout passes on every scroll event.

**Fix:** the cards are uniform `CARD_W × CARD_H`, so drop flex for this grid and position cards absolutely (`x = (i%2)·(CARD_W+GAP)`, `y = (i/2)·(CARD_H+GAP)`; set the container's content height explicitly). Then `LV_OBJ_FLAG_HIDDEN` culling is layout-safe and genuinely free. With direct mode (P0-2) culling matters much less, but it still cuts widget-walk cost during scroll and it removes a latent layout bug either way.

---

## 3 · P1 — Performance: significant, after the big four

**P1-1 · Don't hold `DATA_LOCK` across widget updates.** `ui_machines_refresh()` (`ui_machines.c:634-699`) takes the data mutex, then does label formatting, chart pushes and style changes for every visible machine before releasing. `sim_task` on core 0 blocks for the whole render-side update. Snapshot the needed values into locals under the lock, release, then touch widgets. Same pattern applies to `refresh_tank()` (`ui_tanks.c:245-250` does it right — copy then unlock — keep it that way).

**P1-2 · Invalidate only on change.** `refresh_tank()` calls `lv_obj_invalidate()` unconditionally every 500 ms; the home timer (`ui_home.c:275`) rebuilds label text every 3 s even when the Home tab is hidden (`ui_home_refresh` has no view guard — compare `machines_refresh_cb`, which has one). Gate both: skip when the view is inactive, and skip invalidation when the displayed value didn't change (cache last-rendered value per widget). Under full-refresh these are full-screen repaints; even under direct mode they're wasted work.

**P1-3 · Soft shadows are the most expensive brush in LVGL's box.** `shadow_width 20` on every badge card (`ui_nfc.c:177`), `12` on the NFC result card, `8` on the detail header and viz cards (`ui_detail.c:799,868`). LVGL renders these with a software blur per repaint. Replace with a 1 px border + slightly darker background (Fiori Horizon tolerates this well) or keep shadows only on screens with no periodic refresh.

**P1-4 · `btn_task` runs the whole UI on a 2 KB stack.** `main.c:132` creates `btn_task` with 2048 bytes, and the task calls `ui_btn_short_press()` under `bsp_display_lock` — which can run `ui_switch_tab → ui_machines_refresh` (deep flex relayout, chart updates, `snprintf`s) on that stack. That is uncomfortably close to overflow and it also does render-side work at priority 5 on core 0. Either raise it to 4 KB, or better: have the button task post the event via `lv_async_call` and keep all LVGL work on the LVGL task.

**P1-5 · Extract shared `lv_style_t` styles.** Nearly every widget sets 8–15 local style properties inline (`ui_main.c` alone calls `lv_obj_set_style_*` several hundred times). Local props cost RAM per object and lengthen style resolution in the render path; they're also why the UI files are 500–1100 lines each. Build a small theme table (card, tile, pill, ghost-button, label-key/label-value...) as shared `lv_style_t` objects and `lv_obj_add_style()` them. Smaller binary, faster style walk, and a restyle becomes a one-file change. This is the highest-value *refactor* (as opposed to config change) in the codebase.

**P1-6 · After P0-2, experiment with the P4's PPA.** The ESP32-P4 has a Pixel Processing Accelerator and 2D-DMA (TRM ch. 38 / ch. 7) that LVGL 9.2+ can use as an experimental draw unit (`LV_USE_PPA`) for fills and blends, and the port's inter-buffer sync can ride 2D-DMA. Requires newer esp_lvgl_port/LVGL than IDF 5.3 pulls today — pair it with the IDF upgrade in §6. Treat as an experiment, not a dependency; direct mode alone should already hit your target.

---

## 4 · Correctness findings

**C-1 · Scanning WiFi permanently drops the connection.** `scan_task` (`wifi_manager.c:213`) calls `esp_wifi_disconnect()` before the blocking scan. That disconnect arrives with reason `ASSOC_LEAVE`, which the event handler deliberately does *not* auto-reconnect (`wifi_manager.c:123`). Nothing reconnects after the scan finishes — so tapping "Scan Networks" while connected kills WiFi (and MQTT) until the user joins a network or reboots. Fix: capture whether STA was connected before the scan and call `esp_wifi_connect()` after `esp_wifi_scan_get_ap_records()`.

**C-2 · Cross-core writes without the data mutex.** The sim task mutates metrics under `DATA_LOCK` on core 0, but several LVGL-task writers don't take it: `detail_refresh_labels()` writes `g_tanks/g_machines[...].value` (`ui_detail.c:492-495`), `toggle_cb`/`pulse_cb` mutate `Machine` state (`ui_machines.c:88,118`), and `publish_all()` reads the arrays outside the lock (`sim_task.c:104`). Practical symptom: adjust a running machine's metric in the detail view and the sim overwrites your value on the next tick — tanks solved this with `g_tank_drag_active`, machines never got the equivalent. Decide ownership once: all mutations take `DATA_LOCK` (cheap enough at this scale), and add a per-metric "user override" flag mirroring the tank drag guard.

**C-3 · JSON payloads aren't escaped.** `mqtt_pub_operator` (`mqtt_task.c:160`) formats names/roles straight into a JSON string — a `"` in an operator name (config-supplied) produces invalid JSON downstream. Same class of issue: the captive portal injects raw SSIDs into HTML `<option>` elements (`wifi_portal.c:374-380`); a hostile AP name can inject markup into the provisioning page. Both are two-line escape helpers.

**C-4 · Config larger than 32 KB is silently truncated** (`config_loader.c:18,224,285`), which then reads as "JSON parse error — using default" with no hint of the real cause. Check file size (`fseek/ftell`) and log a distinct message.

**C-5 · Vestigial code confuses readers:** the RSSI refresh block in `ui_home.c:320-328` is an empty `if` with comments (does nothing); `Tank.phase/slosh` (`data.h:83-84`) survive from the removed wave animation; `MAX_METRICS_PER_MACHINE=16` (`ui_machines.c:63`) disagrees with `MAX_MACH_METRICS=8` (`data.h:57`) and doubles the widget-pointer arrays for no reason.

---

## 5 · Hygiene & housekeeping

**H-1 · Dead files.** Not referenced by `main/CMakeLists.txt`: `main/hw/buttons.c/.h` (a second, ISR-based button implementation — `main.c` has its own polling `btn_task`), `main/data_model.c/.h`, `main/wifi_simple.c/.h`. The `fonts/` directory (14 IBM Plex `.c/.h` files + `generate_fonts.sh`) is documented as incompatible with LVGL 9 and unused, yet still listed in `EXTRA_COMPONENT_DIRS` with a stub CMakeLists. Delete or move to an `attic/`; if you ever want IBM Plex back, regenerate uncompressed per Issue 7's note. If you keep exactly one button implementation, the ISR-based one in `hw/buttons.c` is actually the better design (no 20 ms poll) — but pick one.

**H-2 · Real WiFi credentials are committed** (`config.h:10-11`, and in git history). Rotate the password; keep compile-time defaults as obvious placeholders and rely on NVS/portal provisioning, which already works.

**H-3 · No OTA path.** `partitions.csv` spends 8 MB of a 16 MB flash on SPIFFS holding a 3 KB JSON, while the app has a single 4 MB factory slot. Consider `ota_0`/`ota_1` at 3 MB each + a 1–2 MB storage partition — the captive portal is already 90 % of an OTA upload UI. For a demo device that travels, cable-free updates are worth more than 7 MB of empty SPIFFS.

**H-4 · MQTT: add an LWT.** The client connects with no last-will; the dashboard cannot distinguish "device off" from "quiet". A retained `factory/device/status` = `online`/`offline` LWT is a few lines in `mqtt_task_start` and makes the demo visibly more robust.

**H-5 · Stale comment on the draw buffer.** `main.c:102` sizes a 72 KB draw buffer and claims it "reclaims 72 KB" — but in the avoid-tearing path esp_lvgl_port uses the panel's own PSRAM framebuffers and the configured draw buffer is bypassed. Verify on-device (heap trace) and correct the comment either way; it will mislead the next tuning pass.

**H-6 · Sim/UI cadence coupling is implicit.** Sim ticks every 3000 ms (`sim_task.c:105`), home/machines timers every 3000 ms, tanks every 500 ms — all magic numbers in five files. One `#define SIM_TICK_MS` / `UI_REFRESH_MS` pair in `config.h` keeps them honest.

**H-7 · Docs have drifted from the code.** README Known Issues 2/3 and `troubleshooting.md` Issues 2/3 describe the DNS hijack as "not implemented" and the scan as broken — but `wifi_portal.c:72-139` implements the UDP/53 responder and `wifi_manager.c:203-259` switched to the blocking scan. README's task map still says sim = 1500 ms and LVGL = 20 ms (now 3000/33). Update, or the next contributor will re-fix fixed things. (If the iPhone portal still misbehaves *despite* the DNS hijack, the missing piece is usually answering only A-queries and returning NXDOMAIN-free responses fast — worth a fresh on-device test before rewriting anything.)

---

## 6 · The migration question

You asked explicitly whether a different language or runtime would serve better. Assessment against the two hard constraints (identical look, identical functionality):

| Option | Look preserved? | Functionality preserved? | Perf effect | Verdict |
|---|---|---|---|---|
| **Rust** (esp-idf-sys + LVGL bindings, or Slint) | LVGL bindings: yes. Slint: full UI rebuild | esp_hosted/C6 WiFi, DSI/ST7703, GT911, BSP are all C — wrapped via FFI either way | None: same software rasteriser (Slint's is comparable, not faster) | Weeks–months of work that buys memory safety, not frames. Not now |
| **MicroPython / JS (lv_micropython, Moddable)** | Possible | P4 + esp_hosted support immature | 10–50× slower app logic; GC pauses in the render loop | No |
| **Zephyr / NuttX** | LVGL: yes | No P4 MIPI-DSI + C6-hosted-WiFi story today | N/A | No |
| **Stay C/ESP-IDF, upgrade IDF 5.3 → 5.4/5.5 + latest esp_lvgl_port/LVGL 9.3** | Identical | Identical | P4 fixes, PPA draw unit, DSI/LVGL-port improvements | **Recommended** |

The uncomfortable truth that makes this easy: LVGL *is* C, the drivers *are* C, and the performance ceiling on this board is set by how many pixels the software renderer is asked to touch per frame — which is a configuration decision (§P0-2), not a language property. A rewrite reproduces today's frame times in a new syntax while re-introducing every driver-integration risk you've already paid down (see: your own patch to esp_lvgl_port, the SDIO scan workaround, the PSRAM draw-buffer freeze in Issue 8).

Where the "new way of running code" energy *is* well spent: the IDF/LVGL version bump (PPA draw unit, 2D-DMA buffer sync, DSI improvements), OTA partitions (H-3), and — if you want a safer language somewhere — new *peripheral* logic (e.g. a future NFC reader task) can be a Rust component linked into the IDF build without touching the UI. That's a low-risk beachhead if Rust is a long-term goal.

---

## 7 · Recommended order of work

| # | Change | Effort | Expected effect |
|---|---|---|---|
| 1 | `CONFIG_COMPILER_OPTIMIZATION_PERF=y` | 1 line + reflash | 20–40 % faster rendering, everywhere |
| 2 | FULL_REFRESH → direct mode (adapt existing patch if BSP lacks the option) | Small, needs on-device validation | Order-of-magnitude fewer pixels per update; scroll fixed; 33 ms cap can drop back to ~16 ms |
| 3 | `pub()` → `esp_mqtt_client_enqueue` | ~10 lines | Zero UI stalls during drags/taps |
| 4 | Absolute-position machine grid; keep culling | Small | Removes layout feedback loop; cheaper scroll |
| 5 | Fix scan-disconnect bug (C-1) | Small | Scan no longer kills connectivity |
| 6 | Lock hygiene: snapshot-then-render, machine edit guard (C-2, P1-1) | Medium | No more sim-vs-user write races |
| 7 | Invalidate-on-change + view guards (P1-2), shadow diet (P1-3), btn stack (P1-4) | Small each | Removes background repaint load |
| 8 | Shared style theme (P1-5) | Medium refactor | Smaller/faster binary, restylable UI |
| 9 | Dead-code sweep, docs refresh, LWT, credential rotation (H-1..H-7) | Small | Hygiene |
| 10 | IDF 5.4/5.5 + LVGL 9.3 + PPA experiment (P1-6) | Medium, gated on 1–2 landing first | Hardware-accelerated headroom |

Items 1–3 alone should deliver the "performance up by a lot" you asked for; everything after that is compounding.
