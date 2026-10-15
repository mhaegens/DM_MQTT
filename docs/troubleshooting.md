# Troubleshooting

This document covers all known issues with root causes and fixes. For each issue the status column shows whether it is open, mitigated (workaround exists but root fix not yet implemented), or fixed.

---

## Known Issues Summary

| # | Area | Symptom | Status |
|---|------|---------|--------|
| 1 | Display | Cyan flash on boot | Mitigated |
| 2 | WiFi | iPhone captive portal page does not load | Mitigated (DNS hijack implemented — retest) |
| 3 | WiFi | WiFi scan returns no results | Fixed in codebase (blocking scan) |
| 4 | Machines tab | Vertical scroll is sluggish | Fixed in codebase (direct mode — validate on device) |
| 5 | Build | Stale sdkconfig causes crash at ~22 seconds | Fixed by procedure |
| 6 | Build | `xTaskCreatePinnedToCore` fails on boot | Fixed in codebase |
| 7 | Display | LVGL custom fonts render as filled squares | Fixed in codebase |
| 8 | Display | LVGL draw buffer in PSRAM causes UI freeze | Fixed in codebase |

---

## Issue 1: Cyan Flash on Boot

**Symptom:** A brief cyan (teal) frame appears on the display during the first LVGL render after boot.

**Root cause:** The two PSRAM framebuffers (~1 MB each) allocated by the BSP DPI driver are not zeroed before the first DMA transfer. Fresh PSRAM cells at higher addresses default to a bit pattern that corresponds to cyan (`0x07E0` in RGB565). Before LVGL paints the first frame, the display controller reads this garbage data and shows it momentarily.

**Current mitigation (`main.c` lines 117-121):**

```c
// Set root screen background before UI builds to prevent teal flash on first frame
bsp_display_lock(0);
lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xF2F4FA), LV_PART_MAIN);
lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
bsp_display_unlock();
```

By setting the root screen background color before `ui_main_init()`, LVGL's first render paints the background color over the garbage data fast enough that the cyan is not visible in practice.

**Proper fix (not yet implemented):** Obtain the raw DPI panel handle from the BSP and `memset` both framebuffers to the background color before enabling the backlight. This requires access to internal BSP structs and is not currently exposed by the Waveshare BSP API.

---

## Issue 2: iPhone Captive Portal Not Loading

**Symptom:** When the device is running in soft-AP mode (SSID: `SAP-DM-Setup`), an iPhone joins the network but Safari does not automatically open the captive portal page.

**Root cause:** iOS captive portal detection works by making an HTTP request to `captive.apple.com` and checking whether the response is redirected. This redirect requires a DNS hijack: the device's DNS server must resolve all queries to its own IP (`192.168.4.1`).

**Status:** A UDP/53 DNS hijack responder that answers every A-record query with `192.168.4.1` **is implemented** in `wifi_portal.c` (`dns_task`, started by `wifi_portal_start`). If the iPhone portal still does not auto-launch, retest on-device and check that the phone actually sends DNS to `192.168.4.1` (DHCP option 6) and that the responder answers within iOS's timeout.

**Fallback:** Manually open Safari on the iPhone and navigate to `http://192.168.4.1`.

---

## Issue 3: WiFi Scan Returns No Results

**Symptom:** Opening the WiFi tab and tapping the scan button shows a spinner that never populates with nearby networks.

**Root cause:** The ESP32-P4 uses `esp_wifi_remote` to proxy WiFi API calls to the ESP32-C6 companion chip over SDIO. The async `WIFI_EVENT_SCAN_DONE` event was not reliably delivered back to the P4 application event loop.

**Status: fixed in codebase.** `wifi_manager.c` runs a **blocking** scan (`esp_wifi_scan_start(NULL, true)`) inside a dedicated FreeRTOS task so LVGL is never stalled, then delivers results to the UI via `lv_async_call`. The scan task also disconnects STA before scanning (the driver rejects scans mid-connect) and reconnects the previous network when the scan completes.

---

## Issue 4: Machines Tab Scroll Lag

**Symptom:** Vertical scrolling through the machines grid is visibly sluggish compared to other tabs.

**Root cause:** `CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH=y` marked the entire 720x720 framebuffer as dirty on any invalidation — a scroll gesture triggered a full-frame software repaint every frame.

**Status: fixed in codebase (validate on device).** The avoid-tearing path now uses **direct mode** (`patches/esp_lvgl_port_avoid_tearing_direct_mode.patch` + `CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y`): LVGL renders only dirty regions into the panel-owned framebuffers, still swapping on vsync, so tearing and the cyan flash stay fixed while scroll repaints drop by an order of magnitude. The build also moved from `-Og` to `-O2` (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`), the LVGL timer is back to 16 ms, and the machine grid uses absolute positioning so off-screen card culling cannot reflow the layout.

**Revert path** (if direct mode shows artifacts on device): restore the old full-refresh patch, set `CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH=y`, and raise `timer_period_ms` back to 33 in `main.c`.

---

## Issue 5: Stale sdkconfig Crash at ~22 Seconds

**Symptom:** The device boots, the display initializes, but it crashes with a panic at approximately 22-23 seconds. The terminal log shows the SDIO hosted driver timing out.

**Root cause:** `sdkconfig` (the generated configuration file, not committed to git) from a previous build configured the wrong WiFi companion chip or transport. The generated file takes precedence over `sdkconfig.defaults`. When you switch branches or change `sdkconfig.defaults`, the stale generated file is used and the driver panics when the chip does not respond as expected.

**Fix:**

```bash
rm sdkconfig
idf.py reconfigure
idf.py build
```

This regenerates `sdkconfig` from scratch using the current `sdkconfig.defaults`. The `idf.py reconfigure` step is required before `build`, not just `build` alone.

---

## Issue 6: `xTaskCreatePinnedToCore` Fails on Boot

**Symptom:** Boot panic at the `esp_hosted` initialization step with a message about task creation failure.

**Root cause:** The `esp_hosted` SDIO driver creates several FreeRTOS tasks during init. If these tasks are allocated in internal SRAM, the allocation fails because internal SRAM is exhausted by the time the hosted driver runs. The driver needs `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y` to allocate its tasks from PSRAM.

**Status:** Fixed in `sdkconfig.defaults`. The line `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y` is already present. If you see this issue, it means `sdkconfig` was generated without this setting. Apply the fix from Issue 5 (delete `sdkconfig` and reconfigure).

---

## Issue 7: LVGL Custom Fonts Render as Filled Squares

**Symptom:** After adding custom IBM Plex Sans/Mono fonts to `fonts/`, the text appears as solid filled rectangles instead of readable characters.

**Root cause:** IBM Plex fonts compiled with `lv_font_conv` using `--bpp 4` and a compressed bitmap format (`bitmap_format=1`) are incompatible with the LVGL 9 font decompressor. LVGL 9 changed the internal font format. The compressed format causes the glyph renderer to misinterpret the bitmap data.

**Status:** Fixed. The codebase switched to the built-in Montserrat font family using LVGL's compiled-in variants:

```
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_18=y
CONFIG_LV_FONT_MONTSERRAT_22=y
CONFIG_LV_FONT_MONTSERRAT_24=y
```

If you need to add custom fonts in the future, generate them without compression:

```bash
lv_font_conv \
  --font IBMPlexSans-Regular.ttf \
  --bpp 4 \
  --size 22 \
  --format lvgl \
  -o ibm_plex_sans_22.c
```

Do not pass `--compressed` or set `bitmap_format` in the output options.

---

## Issue 8: LVGL Draw Buffer in PSRAM Causes UI Freeze

**Symptom:** Setting `buff_spiram = true` in the `bsp_display_cfg_t` struct causes the display to stop updating entirely. The firmware boots and runs but the screen is frozen.

**Root cause:** `buff_spiram = true` places the LVGL software render buffer (the intermediate per-tick render target) in PSRAM. The DPI display controller's DMA engine uses a dedicated path to read the two display framebuffers from PSRAM via the L2 cache. The software render buffer is not part of this DMA path: LVGL writes to it via the CPU, then copies it to the active framebuffer. When the render buffer is in PSRAM, the copy operation works but the DPI DMA does not see the result because the render buffer and the framebuffers use different address ranges and the L2 cache does not bridge them in this configuration.

**Status:** Fixed. `buff_spiram = false` is set in `main.c`:

```c
.flags = {
    .buff_dma    = true,
    .buff_spiram = false,  // draw buffer must stay in internal DMA-capable SRAM
    .sw_rotate   = false,
}
```

The two display framebuffers themselves live in PSRAM (allocated by the BSP DPI driver via `MALLOC_CAP_SPIRAM`). Only the LVGL software render buffer must stay in internal SRAM.

## Main hostname returns 502, or pages load without controls

Route the entire MQTT hostname to Nginx on port 80 using the current `cloudflared/config.yml.example`. Deploy the current Nginx configuration first: it redirects browser root requests to `/dashboard/` and proxies MQTT WebSocket upgrades at `/` and `/mqtt` to Mosquitto. An older tunnel rule sending the root directly to port 9001 can serve MQTT while returning 502 to a browser.

After pulling changed bind-mounted Nginx configuration, validate and recreate the web container. When running inside a pasted heredoc, explicitly disconnect the validation command from the script's input:

```bash
docker compose run --rm --no-deps -T --interactive=false dashboard nginx -t </dev/null
docker compose up -d --no-deps --force-recreate dashboard </dev/null
```

`docker compose run` keeps standard input open by default and can consume the commands following it. A successful configuration test alone does not confirm that the running web container has reloaded.

A page with unstyled controls or an empty broker field can indicate missing JavaScript or CSS. Deploy the HTML and assets together. Versioned asset references bypass cached responses from the previous deployment. Run `TUNNEL_URL=https://mqtt.example.com/dashboard/ bash scripts/smoke-test.sh` inside the LXC to check the public root, assets and both MQTT WebSocket paths.
