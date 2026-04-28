#pragma once
#include "lvgl.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ui_theme.h — Design tokens matching the HTML dashboard CSS variables exactly.
//  All pixel values are calibrated to the 720×720 IPS at 255 PPI (4-inch physical).
// ─────────────────────────────────────────────────────────────────────────────

// ─── Colors ──────────────────────────────────────────────────────────────────
#define COL_HI       lv_color_hex(0x0070F2)   // SAP primary blue
#define COL_HI_DK    lv_color_hex(0x0050BE)
#define COL_HI_LT    lv_color_hex(0xE8F2FF)
#define COL_OK       lv_color_hex(0x107E3E)   // green / running / safe
#define COL_OK_LT    lv_color_hex(0xE3F5EC)
#define COL_WARN     lv_color_hex(0xE76500)   // orange / warning
#define COL_WARN_LT  lv_color_hex(0xFEF0E5)
#define COL_ERR      lv_color_hex(0xBB0000)   // red / error / stopped
#define COL_ERR_LT   lv_color_hex(0xFFE8E8)
#define COL_TEAL     lv_color_hex(0x045B7A)
#define COL_TEAL_LT  lv_color_hex(0xE0F2F8)
#define COL_PUR      lv_color_hex(0x6A28C8)   // purple / pH
#define COL_PUR_LT   lv_color_hex(0xF2EAFF)
#define COL_BG       lv_color_hex(0xF2F4FA)   // page background
#define COL_BASE     lv_color_hex(0xFFFFFF)   // card surface
#define COL_BASE2    lv_color_hex(0xEEF1F8)
#define COL_BASE3    lv_color_hex(0xE3E6EF)
#define COL_BD       lv_color_hex(0xDDE1EA)   // border
#define COL_BD_STR   lv_color_hex(0xB8BFCC)
#define COL_TX       lv_color_hex(0x1D2D3E)   // text primary
#define COL_TX2      lv_color_hex(0x4A6080)   // text secondary
#define COL_TX3      lv_color_hex(0x8090A8)   // text tertiary
#define COL_DBG_BG   lv_color_hex(0x080F1C)   // debug log background
#define COL_DBG_PUB  lv_color_hex(0x5BB2FF)   // publish log entry
#define COL_DBG_OK   lv_color_hex(0x4CC87A)   // ok log entry
#define COL_DBG_ERR  lv_color_hex(0xFF6666)   // error log entry
#define COL_DBG_TS   lv_color_hex(0x334E68)   // timestamp

// ─── Opacity ─────────────────────────────────────────────────────────────────
#define OPA_FULL   LV_OPA_COVER
#define OPA_90     LV_OPA_90
#define OPA_60     LV_OPA_60
#define OPA_30     LV_OPA_30

// ─── Sizes (px at 255 PPI / 4-inch) ─────────────────────────────────────────
#define SZ_SCREEN     720    // display width & height
#define SZ_SHELL       64    // top shell bar height
#define SZ_TAB         72    // bottom tab bar height
#define SZ_CRUMB       44    // breadcrumb bar height (detail pages)
#define SZ_BTN         80    // primary action button height (8 mm)
#define SZ_BTN_SM      64    // smaller button height (6.5 mm)
#define SZ_CARD_PAD    12    // minimum card internal padding
#define SZ_GAP         10    // grid gap between cards
#define SZ_RADIUS      12    // standard border radius
#define SZ_RADIUS_SM    8
#define SZ_BADGE_SAP   44    // SAP logo badge square
#define SZ_DOT          8    // status dot diameter
#define SZ_THUMB       28    // slider thumb diameter

// ─── Font sizes ──────────────────────────────────────────────────────────────
// Fonts must be declared as extern and defined in the generated font .c files.
// Generate with: lv_font_conv --font IBMPlexSans.ttf --size N --format lvgl --bpp 4
#define F_XL  40   // KPI hero values
#define F_LG  32   // machine names / large metric
#define F_MD  26   // card titles / body large
#define F_SM  22   // button text / body medium
#define F_XS  18   // body text / small values
#define F_XXS 15   // captions, labels (minimum readable)

// Forward-declare all font objects (defined in fonts/*.c)
// Sans (UI text)
extern lv_font_t ibm_plex_sans_40;
extern lv_font_t ibm_plex_sans_32;
extern lv_font_t ibm_plex_sans_26;
extern lv_font_t ibm_plex_sans_22;
extern lv_font_t ibm_plex_sans_18;
extern lv_font_t ibm_plex_sans_15;
// Mono (metric values, topics, status codes)
extern lv_font_t ibm_plex_mono_26;
extern lv_font_t ibm_plex_mono_18;
extern lv_font_t ibm_plex_mono_15;

// Convenience macros — use these in UI code
#define FONT_SANS_XL   (&ibm_plex_sans_40)
#define FONT_SANS_LG   (&ibm_plex_sans_32)
#define FONT_SANS_MD   (&ibm_plex_sans_26)
#define FONT_SANS_SM   (&ibm_plex_sans_22)
#define FONT_SANS_XS   (&ibm_plex_sans_18)
#define FONT_SANS_XXS  (&ibm_plex_sans_15)
#define FONT_MONO_MD   (&ibm_plex_mono_26)
#define FONT_MONO_XS   (&ibm_plex_mono_18)
#define FONT_MONO_XXS  (&ibm_plex_mono_15)

// ─── Content area geometry ───────────────────────────────────────────────────
// Height of the area between shell bar and tab bar (main content zone)
#define CONTENT_Y      SZ_SHELL
#define CONTENT_H      (SZ_SCREEN - SZ_SHELL - SZ_TAB)
#define CONTENT_W      SZ_SCREEN

// Content area when a breadcrumb bar is shown (detail pages)
#define DETAIL_Y       (SZ_SHELL + SZ_CRUMB)
#define DETAIL_H       (SZ_SCREEN - SZ_SHELL - SZ_CRUMB - SZ_TAB)
