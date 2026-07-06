# sdkconfig.defaults Reference

`sdkconfig.defaults` contains the required ESP-IDF configuration for the ESP32-P4 firmware. This file provides defaults that are applied when `sdkconfig` is generated. The generated `sdkconfig` is not committed to git.

When you change this file, regenerate:

```bash
rm sdkconfig
idf.py reconfigure
idf.py build
```

Skipping `rm sdkconfig` causes the existing generated file to take precedence over the new defaults.

---

## Target

```
CONFIG_IDF_TARGET="esp32p4"
```

Locks the build to the ESP32-P4. Prevents accidentally building for the wrong chip when `sdkconfig` does not exist.

---

## Compiler

```
CONFIG_COMPILER_OPTIMIZATION_PERF=y
```

Builds at `-O2`. Without this line ESP-IDF defaults to Debug (`-Og`), which leaves the LVGL software rasteriser — the hottest code on the device — unoptimised. `-O2` is worth roughly 20–40 % on render-heavy paths for free.

---

## CPU

```
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y
```

The ESP32-P4's HP cores run up to 400 MHz, but ESP-IDF defaults the target to 360 MHz. Everything render-related on this device is CPU-bound, so this is a free ~11 %.

---

## FATFS (SD card)

```
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255
```

Long filename support. **Without this, ESP-IDF's FATFS accepts DOS 8.3 names only** — and `sapdm_config.json` has a 12-character basename, so every `fopen()` of it on the SD card failed with `FR_INVALID_NAME`: config reads silently fell back to the default, and the portal's upload reported "Cannot write to SD card". `LFN_HEAP` keeps the name buffers off the stack.

---

## PSRAM

```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=512
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=81920
```

**`CONFIG_SPIRAM_USE_MALLOC=y`**: allows `malloc()` to return PSRAM memory when internal SRAM is low. Without this, PSRAM is only available via explicit `heap_caps_malloc(MALLOC_CAP_SPIRAM)`.

**`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=512`**: allocations smaller than 512 bytes always go to internal SRAM, even when `USE_MALLOC` is set. PSRAM has higher latency than internal SRAM, and short-lived small allocations (structs, string buffers) benefit from internal SRAM speed.

**`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=81920`**: reserves 80 KB of internal SRAM that `malloc()` will never touch. This 80 KB is consumed by the `esp_hosted` SDIO driver's DMA ring buffers during initialization. If the reservation is too small, `esp_hosted` fails at init with a DRAM allocation error, and the WiFi companion chip cannot start.

**Why no XIP (execute-in-place from PSRAM):** XIP (`CONFIG_SPIRAM_RODATA=y`, `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`) was tested and removed. On ESP32-P4 with this partition table layout, XIP triggers a linker error: "SRAM region is not contiguous." The performance benefit from XIP is not needed for this application.

---

## L2 Cache

```
CONFIG_CACHE_L2_CACHE_256KB=y
CONFIG_CACHE_L2_CACHE_LINE_128B=y
```

The ESP32-P4's DPI display controller streams two 720x720 framebuffers from PSRAM via DMA. Without the L2 cache, the DMA stalls on PSRAM latency and the display shows corrupted frames or nothing. The 256 KB L2 cache with 128-byte cache lines provides enough bandwidth for the DPI DMA to sustain full-frame refresh at ~50 fps.

---

## Flash

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

The Waveshare board has 16 MB flash. `partitions.csv` defines the custom layout:

| Partition | Offset | Size | Use |
|-----------|--------|------|-----|
| NVS | 0x9000 | 24 KB | WiFi credentials, user settings |
| PHY_INIT | 0xF000 | 4 KB | RF calibration data |
| factory | 0x10000 | 4 MB | Application firmware |
| spiffs | 0x410000 | ~8 MB | Config JSON (`sapdm_config.json`) |

---

## FreeRTOS

```
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y
```

**`CONFIG_FREERTOS_HZ=1000`**: 1 ms tick resolution. Required for `pdMS_TO_TICKS(20)` to produce accurate 20ms intervals. At the default 100Hz, `pdMS_TO_TICKS(20)` rounds to 2 ticks = 20ms, which works, but higher precision is useful for the button debounce (20ms) and LVGL render period.

**`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y`**: allows `xTaskCreatePinnedToCore` to allocate task stacks in PSRAM. The `esp_hosted` SDIO tasks use this to allocate their stacks from PSRAM (enabled by `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y`).

---

## LVGL

```
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2
CONFIG_LV_DEF_REFR_PERIOD=15
CONFIG_LV_OS_FREERTOS=y
```

**`CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`**: enables two software draw units, one per CPU core. LVGL 9 can split a render frame into two horizontal bands and process them in parallel on CPU 0 and CPU 1. This roughly halves render time for complex screens.

**`CONFIG_LV_DEF_REFR_PERIOD=15`**: default LVGL timer period in ms. The BSP overrides this via `timer_period_ms = 16` in `main.c`, so the effective value is 16ms (~60fps). Direct-mode rendering only touches dirty regions, so a render pass comfortably fits the 16ms budget.

```
CONFIG_LV_INDEV_DEF_READ_PERIOD=10
```

Touch sampling period. LVGL's default is 30 ms, which caps scroll-position updates at 33 Hz — visible judder during gestures even when frames render at 60 fps. 10 ms makes the scroll track the finger.

```
CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE=40
```

Caches the most recent software-blurred box shadow. The NFC badge cards (shadow 20 + radius 16), NFC result card and detail cards all share shadow parameters, so repaints become cache hits instead of a fresh blur. 40 covers the largest `shadow_width + radius` in the UI; the cache costs ~1.6 KB.

### Built-in Montserrat fonts

```
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_18=y
CONFIG_LV_FONT_MONTSERRAT_22=y
CONFIG_LV_FONT_MONTSERRAT_24=y
```

Custom IBM Plex Sans fonts were originally generated with `lv_font_conv` and placed in `fonts/`. They caused glyphs to render as filled squares because LVGL 9 changed the internal font format and `bitmap_format=1` (compressed) is not supported. The codebase switched to the built-in Montserrat family, which is compiled directly into LVGL and guaranteed compatible.

See [troubleshooting.md](../../docs/troubleshooting.md#issue-7-lvgl-custom-fonts-render-as-filled-squares) for details.

---

## Display

```
CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2
CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y
```

`BUFFER_NUMS=2` + `AVOID_TEAR` give two panel-owned PSRAM framebuffers swapped on vsync — this is what prevents both tearing and the boot cyan flash.

The render mode within the avoid-tear path is **direct mode** (previously full-refresh). Full-refresh marked the entire 720x720 frame dirty on *any* invalidation, so a single label update cost a ~1 MB repaint plus a full widget-tree render — the root cause of the machines-tab scroll lag. In direct mode LVGL renders only dirty regions straight into the active framebuffer; the port syncs dirty areas between the buffers on flush.

The mode is actually carried by `patches/esp_lvgl_port_avoid_tearing_direct_mode.patch`, because the Waveshare BSP does not set the direct/full flags on the LVGL port in its avoid-tearing path. The `DIRECT_MODE` Kconfig line above only takes effect on BSPs that expose the choice and is otherwise ignored.

**The patch is applied automatically** by the top-level `CMakeLists.txt` at configure time (idempotent, and it removes the component-manager hash file so the modified component still builds). After `idf.py update-dependencies`, the next configure re-applies it — no manual step. If upstream `esp_lvgl_port` ever changes the anchor code, the configure step fails loudly with instructions instead of silently building a broken pipeline.

Each framebuffer is 720 x 720 x 2 bytes = 1,036,800 bytes (~1 MB). Both framebuffers are allocated in PSRAM by the BSP DPI driver.

Revert path if direct mode shows artifacts on device: restore the full-refresh patch variant, set `CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH=y`, and raise `timer_period_ms` in `main.c` back to 33.

---

## WiFi / esp_hosted

```
CONFIG_ESP_HOSTED_CP_TARGET_ESP32C6=y
CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y
CONFIG_ESP_HOSTED_SDIO_PIN_CMD=19
CONFIG_ESP_HOSTED_SDIO_PIN_CLK=18
CONFIG_ESP_HOSTED_SDIO_PIN_D0=14
CONFIG_ESP_HOSTED_SDIO_PIN_D1=15
CONFIG_ESP_HOSTED_SDIO_PIN_D2=16
CONFIG_ESP_HOSTED_SDIO_PIN_D3=17
CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54
CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y
```

Pin assignments match the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B schematic. These are SoC-level GPIO numbers, not board-level header pins.

**`CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y`**: the `esp_hosted` SDIO driver creates several FreeRTOS tasks during initialization. By the time it runs, internal SRAM is largely consumed by the network stack, LwIP, and other components. Without this setting, `xTaskCreatePinnedToCore` fails for the hosted driver tasks, and the WiFi companion chip cannot start.

---

## MQTT

```
CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED=y
CONFIG_MQTT_TASK_CORE_SELECTION=0
```

The `esp_mqtt` internal task is pinned to CPU 0. LVGL runs on CPU 1. Without this pin, the MQTT task may be scheduled on CPU 1 and compete with LVGL rendering, causing display stutters during MQTT reconnect.

---

## Console

```
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y
CONFIG_NEWLIB_STDOUT_LINE_ENDING_LF=y
```

**`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`**: uses UART0 for the debug console. The Waveshare board has a CH343 USB-UART bridge chip on UART0, exposed as a USB serial port. The USB-JTAG port (USB-C, secondary) is enabled as a secondary console.

**`CONFIG_NEWLIB_STDOUT_LINE_ENDING_LF=y`**: disables the default CRLF translation on stdout. Without this, any `\n` byte in stdout output is silently converted to `\r\n`. The screenshot tool sends raw RGB565 pixel data over stdout. A `\r\n` substitution mid-pixel corrupts every row of the image that contains a `0x0A` byte.

---

## LVGL Optional Features

```
CONFIG_LV_USE_SNAPSHOT=y
CONFIG_LV_USE_QRCODE=y
```

**`CONFIG_LV_USE_SNAPSHOT=y`**: enables `lv_snapshot_take()`, used by the screenshot tool to capture the current screen state as a raw pixel buffer.

**`CONFIG_LV_USE_QRCODE=y`**: enables the LVGL QR code widget, used on the WiFi setup screen to display the provisioning portal URL as a scannable QR code.
