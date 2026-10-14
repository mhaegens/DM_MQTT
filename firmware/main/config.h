#pragma once
#include "sdkconfig.h"

// ─────────────────────────────────────────────────────────────────────────────
//  config.h — Edit these before flashing.
//  WiFi credentials are stored in NVS after first provisioning.
//  These are used only on first boot or if NVS is erased.
// ─────────────────────────────────────────────────────────────────────────────

// Default WiFi credentials (overridden by captive-portal provisioning after first boot)
#define DEFAULT_WIFI_SSID     ""
#define DEFAULT_WIFI_PASSWORD ""

// MQTT broker — Cloudflare Tunnel WSS endpoint
#ifndef MQTT_BROKER_URI
#define MQTT_BROKER_URI  CONFIG_DM_MQTT_BROKER_URI
#endif
// Port is 443 for WSS; embedded in URI above, but kept explicit for logging
#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT  CONFIG_DM_MQTT_BROKER_PORT
#endif

// MQTT client identity
#define MQTT_CLIENT_ID    "esp32p4-sapdm-01"
#define MQTT_KEEPALIVE_S  15

// Topic prefix
#define TOPIC_PREFIX      "factory"

// Retained device availability topic (set as MQTT last-will, published on connect)
#define STATUS_TOPIC      TOPIC_PREFIX "/device/status"

// ─── Timing — sim and UI refresh cadence (keep in sync; UI polls sim output) ──
#define SIM_TICK_MS        3000   // preserve simulation and auto-pulse pacing
#define TELEMETRY_CHECK_MS  100   // observe user changes independently of simulation
#define UI_REFRESH_MS       500   // home / machines live-refresh timers
#define TANK_REFRESH_MS     500   // tanks screen refresh timer (active tab only)

// Ghost-scroll on the machines grid: each card is pre-rendered into an opaque
// RGB565 image in PSRAM, and during a scroll gesture the live widget trees are
// swapped for those images — a scroll frame is then a handful of straight
// memcpy blits instead of a full software repaint of charts/arcs/rounded
// rects. Set to 0 to fall back to scrolling the live widgets.
#define UI_SCROLL_GHOSTS   1

// Plant / site label shown in the shell bar subtitle
#define PLANT_NAME        "Plant BE01"

// WiFi provisioning AP (Phase 8)
#define PROV_AP_SSID      "SAP-DM-Setup"
#define PROV_AP_CHANNEL   1
// URL served by captive portal; matches ESP32 AP default gateway
#define PROV_PORTAL_URL   "http://192.168.4.1"

// Hardware GPIO assignments (verified from Waveshare ETH-2RO PDF; SoC-level,
// shared with WIFI6 variant — confirm against WIFI6-Touch-LCD-4B wiki if any
// discrepancy is observed during bring-up)
#define GPIO_I2C_SDA      7
#define GPIO_I2C_SCL      8
#define GPIO_BOOT_BTN    35    // BOOT button (GPIO35 = ESP32-P4 strapping pin); GPIO9 is I2S DSDIN

// SD card (SDIO 4-bit) — available but not used in Phase 1
#define GPIO_SD_CLK       43
#define GPIO_SD_CMD       44
#define GPIO_SD_D0        39
#define GPIO_SD_D1        40
#define GPIO_SD_D2        41
#define GPIO_SD_D3        42
#define GPIO_SD_POWER     45   // PMOS gate: low enables card power

// Audio (ES8311 + ES7210 + NS4150B)
#define GPIO_I2S_MCLK     13
#define GPIO_I2S_SCLK     12
#define GPIO_I2S_ASDOUT   11
#define GPIO_I2S_LRCK     10
#define GPIO_I2S_DSDIN     9   // Shared with the BOOT strap function; release before audio use
#define GPIO_PA_CTRL      53   // NS4150B power-amplifier enable (active high)
