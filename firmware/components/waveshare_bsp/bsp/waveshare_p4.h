#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  bsp/waveshare_p4.h — Board Support for Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
//
//  Panel IC: ST7703 (MIPI-DSI 2-lane, 720×720)
//  Confirmed from: ESP32-P4-WIFI6-Touch-LCD-4B.zip / ESP-IDF/07_color_panel
//  Component: waveshare/esp_lcd_st7703 (idf_component.yml)
// ─────────────────────────────────────────────────────────────────────────────

#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_st7703.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

// ── Display resolution ────────────────────────────────────────────────────────
#define BSP_LCD_H_RES   720
#define BSP_LCD_V_RES   720

// ── MIPI-DSI PHY power (internal LDO, must be enabled before DSI bus init) ───
#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN       3
#define BSP_MIPI_DSI_PHY_PWR_LDO_MV        2500

// ── GPIO assignments (from Waveshare demo + config.h) ────────────────────────
#define BSP_LCD_RST_GPIO    27   // Panel reset (active low)
#define BSP_LCD_BL_GPIO     26   // Backlight enable (active low per demo)
#define BSP_LCD_BL_ON        0   // Level to turn backlight ON

// ── BSP config passed from main.c ─────────────────────────────────────────────
typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;
} bsp_display_cfg_t;

// ── Function declarations (implemented in waveshare_p4.c) ─────────────────────
esp_err_t bsp_display_start_with_config(const bsp_display_cfg_t *cfg);
void      bsp_display_lock(uint32_t timeout_ms);
void      bsp_display_unlock(void);
