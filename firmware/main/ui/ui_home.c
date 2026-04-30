#include "ui_home.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "data_model.h"
#include "config.h"
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────────────────
//  ui_home.c — Home screen: 2×2 KPI grid + 3 connection status rows.
//  Refreshed on tab activation and on MQTT/WiFi events.
// ─────────────────────────────────────────────────────────────────────────────

static lv_obj_t *s_screen          = NULL;
static lv_obj_t *s_kpi_val[4]     = {NULL};
static lv_obj_t *s_kpi_meta[4]    = {NULL};
static lv_obj_t *s_operator_label = NULL;  // "OPERATOR" row value; updated in refresh

// ─── KPI card ────────────────────────────────────────────────────────────────
// Creates a 140px tall card inside the grid container at position (idx%2, idx/2).
// The accent color drives both the 4px top bar and the value label color.
// s_kpi_val[idx] and s_kpi_meta[idx] are set for later updates.
static void make_kpi_card(lv_obj_t *grid, int idx,
                           const char *label_text,
                           const char *val_text,
                           const char *meta_text,
                           lv_color_t  accent)
{
    int grid_col = idx % 2;
    int grid_row = idx / 2;

    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_height(card, 140);
    lv_obj_set_style_bg_color(card, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, SZ_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COL_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    // Accent is drawn via a 4px child bar (see below); card uses 1px full border.
    lv_obj_set_style_pad_top(card, SZ_CARD_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(card, SZ_CARD_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(card, SZ_CARD_PAD, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Place card in its grid cell (stretch to fill the cell width)
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, grid_col, 1,
                               LV_GRID_ALIGN_STRETCH, grid_row, 1);

    // ── 4px accent bar at the very top of the card ───────────────────────────
    // Positioned at y = -SZ_CARD_PAD so it sits flush against the card's top
    // edge (the card padding provides clearance for the text content below).
    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_size(bar, LV_PCT(100), 4);
    lv_obj_set_pos(bar, 0, -SZ_CARD_PAD);   // push up to the very top of content
    lv_obj_set_style_bg_color(bar, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // ── Label row ─────────────────────────────────────────────────────────────
    lv_obj_t *lbl = lv_label_create(card);
    lv_obj_set_pos(lbl, 0, 10);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, FONT_MONO_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, COL_TX3, LV_PART_MAIN);

    // ── Value ─────────────────────────────────────────────────────────────────
    s_kpi_val[idx] = lv_label_create(card);
    lv_obj_set_pos(s_kpi_val[idx], 0, 34);
    lv_label_set_text(s_kpi_val[idx], val_text);
    lv_obj_set_style_text_font(s_kpi_val[idx], FONT_MONO_MD, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_kpi_val[idx], accent, LV_PART_MAIN);

    // ── Meta ──────────────────────────────────────────────────────────────────
    s_kpi_meta[idx] = lv_label_create(card);
    lv_obj_set_pos(s_kpi_meta[idx], 0, 78);
    lv_label_set_text(s_kpi_meta[idx], meta_text);
    lv_obj_set_style_text_font(s_kpi_meta[idx], FONT_SANS_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_kpi_meta[idx], COL_TX3, LV_PART_MAIN);
}

// ─── Badge pill ───────────────────────────────────────────────────────────────
static lv_obj_t *make_badge(lv_obj_t *parent,
                             const char *text,
                             lv_color_t  bg,
                             lv_color_t  fg,
                             lv_color_t  border)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(pill, border, LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(pill, SZ_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(pill, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(pill, 12, LV_PART_MAIN);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(pill);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FONT_MONO_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, fg, LV_PART_MAIN);
    lv_obj_center(lbl);

    return pill;
}

// ─── Status row ───────────────────────────────────────────────────────────────
// Flex row: [key/value column] SPACE_BETWEEN [badge pill]
// Returns the value label so the caller can update it in refresh.
static lv_obj_t *make_status_row(lv_obj_t *col,
                                  const char *key_text,
                                  const char *val_text,
                                  lv_color_t  badge_bg,
                                  lv_color_t  badge_fg,
                                  lv_color_t  badge_border,
                                  const char *badge_text)
{
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_set_size(row, LV_PCT(100), 64);
    lv_obj_set_style_bg_color(row, COL_BASE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, SZ_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, COL_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 10, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                               LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER);

    // Left side: key label stacked above value label
    lv_obj_t *left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                                LV_FLEX_ALIGN_START,
                                LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(left, 2, LV_PART_MAIN);

    lv_obj_t *key = lv_label_create(left);
    lv_label_set_text(key, key_text);
    lv_obj_set_style_text_font(key, FONT_MONO_XXS, LV_PART_MAIN);
    lv_obj_set_style_text_color(key, COL_TX3, LV_PART_MAIN);

    lv_obj_t *val = lv_label_create(left);
    lv_label_set_text(val, val_text);
    lv_obj_set_style_text_font(val, FONT_MONO_XS, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, COL_TX, LV_PART_MAIN);

    // Right side: badge pill
    make_badge(row, badge_text, badge_bg, badge_fg, badge_border);

    return val;
}

// ─── ui_home_create ───────────────────────────────────────────────────────────
void ui_home_create(lv_obj_t *parent)
{
    // Screen container: flex column, 14px padding, 12px row gap
    s_screen = lv_obj_create(parent);
    lv_obj_set_size(s_screen, SZ_SCREEN, CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_screen, 14, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    g_screens[VIEW_HOME] = s_screen;

    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_CENTER,
                                    LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_screen, 12, LV_PART_MAIN);

    // ── 2×2 KPI grid ─────────────────────────────────────────────────────────
    lv_obj_t *grid = lv_obj_create(s_screen);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_style_all(grid);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    // Two equal-width columns; row height = content
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                    LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT,
                                    LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_column(grid, SZ_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, SZ_GAP, LV_PART_MAIN);

    static const struct { const char *label, *val, *meta; } kpi[4] = {
        { "RUNNING",      "3 / 4", "CNC-02 stopped" },
        { "PULSES TODAY", "1,284", "+42 last 5 min"  },
        { "MQTT MSGS",    "387",   "since boot"      },
        { "WIFI RSSI",    "-61",   "dBm"             },
    };
    lv_color_t acc[4] = { COL_OK, COL_HI, COL_WARN, COL_TEAL };

    for (int i = 0; i < 4; i++) {
        make_kpi_card(grid, i, kpi[i].label, kpi[i].val, kpi[i].meta, acc[i]);
    }

    // ── Status rows column (8px gap) ─────────────────────────────────────────
    lv_obj_t *rows_col = lv_obj_create(s_screen);
    lv_obj_set_size(rows_col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_style_all(rows_col);
    lv_obj_set_style_bg_opa(rows_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(rows_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(rows_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rows_col, LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(rows_col, 8, LV_PART_MAIN);

    // Row 0: MQTT Broker (static value from config.h; badge = green "CONNECTED")
    make_status_row(rows_col,
                    "MQTT BROKER",
                    "mqttfordm.haegens.be : 443",
                    COL_OK_LT, COL_OK, lv_color_hex(0x9ADDB8),
                    "CONNECTED");

    // Row 1: Device IP (updated in refresh; badge = blue "DHCP")
    make_status_row(rows_col,
                    "DEVICE IP",
                    g_device_ip[0] ? g_device_ip : "\xe2\x80\x94",
                    COL_HI_LT, COL_HI, lv_color_hex(0xC5DCFF),
                    "DHCP");

    // Row 2: Operator (updated in refresh; badge = green "ACTIVE")
    s_operator_label = make_status_row(rows_col,
                                       "OPERATOR",
                                       "\xe2\x80\x94",
                                       COL_OK_LT, COL_OK, lv_color_hex(0x9ADDB8),
                                       "ACTIVE");

    ui_home_refresh();
}

// ─── ui_home_refresh ─────────────────────────────────────────────────────────
void ui_home_refresh(void)
{
    if (!s_screen) return;

    // KPI 0: Running / Total machines
    int running = data_running_count();
    lv_label_set_text_fmt(s_kpi_val[0], "%d / %d", running, MACH_COUNT);

    const char *stopped_name = NULL;
    for (int i = 0; i < MACH_COUNT; i++) {
        if (!g_machines[i].running) {
            stopped_name = g_machines[i].name;
            break;
        }
    }
    if (stopped_name)
        lv_label_set_text_fmt(s_kpi_meta[0], "%s stopped", stopped_name);
    else
        lv_label_set_text(s_kpi_meta[0], "All running");

    // KPI 1: Pulses today (meta is static, set at create time)
    lv_label_set_text_fmt(s_kpi_val[1], "%d", data_total_pulses());

    // KPI 2: MQTT message count (meta "since boot" is static)
    lv_label_set_text_fmt(s_kpi_val[2], "%d", g_mqtt_msg_count);

    // KPI 3: WiFi RSSI (meta "dBm" is static)
    lv_label_set_text_fmt(s_kpi_val[3], "%d", g_wifi_rssi);

    // Operator row
    if (s_operator_label) {
        if (g_active_operator >= 0 && g_active_operator < OP_COUNT)
            lv_label_set_text(s_operator_label,
                              g_operators[g_active_operator].name);
        else
            lv_label_set_text(s_operator_label, "\xe2\x80\x94");
    }
}
