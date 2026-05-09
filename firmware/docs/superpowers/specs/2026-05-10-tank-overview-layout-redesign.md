# Tank Overview Layout Redesign

## Context

The tank detail screen (`ui_tank_overview.c`) currently stacks all elements vertically: a wide, flat fill bar on top, metric tiles below, and the fill/drain control panel at the bottom. The fill visualization is 400 × 120 px — landscape-oriented and difficult to read as a tank level indicator. With 5 metric rows and a control panel all below it, the screen feels unbalanced and wastes horizontal space.

The goal is to change to a two-column layout: a tall, narrow fill bar on the left as a strong visual anchor, with all metric tiles and the fill/drain controls in a right column beside it. This makes the fill level easier to read at a glance, while keeping all information visible on screen at once.

## Design Decision

- **Layout**: Option C — slim tank column left, metrics + controls in right column (no row below)
- **Tank column width**: 200 px (28% of 720 px content width)
- **Tank column height**: fills all remaining vertical space after the header (~536 px)
- **Right column**: metrics packed at the top, spacer grows to push the control panel to the bottom

## Dimensions

| Constant | Old | New | Notes |
|---|---|---|---|
| `OVW_FILL_W` | 400 | 200 | tank column width |
| `OVW_FILL_H` | 120 | 536 | `DETAIL_H - OVW_PAD*2 - 8 - 48` |
| `OVW_RIGHT_W` | — | 490 | `CONTENT_W - OVW_PAD*2 - OVW_FILL_W - SZ_GAP` |
| `TILE_H` | 40 | 48 | slightly taller for right-column proportions |

`OVW_FILL_H` and `OVW_RIGHT_W` should be defined as compile-time macro expressions so they stay in sync with theme constants.

## Layout Structure

```
s_screen (flex column, gap=8)
├── hdr                           (48 px, full width — unchanged)
└── body_row                      (CONTENT_W-OVW_PAD*2 × OVW_FILL_H, flex row, gap=SZ_GAP)
    ├── s_fill_outer              (OVW_FILL_W × OVW_FILL_H — left column)
    │   ├── s_fill_bar            (fill level, bottom-anchored)
    │   │   └── shine             (highlight strip)
    │   ├── tick marks @ 25/50/75%
    │   ├── s_target_line         (yellow, hidden by default)
    │   └── s_fill_pct_lbl        (centered %)
    └── right_col                 (OVW_RIGHT_W × OVW_FILL_H, flex column, gap=8)
        ├── s_tile_rows[0..N-1]   (OVW_RIGHT_W × TILE_H each, shown per metric_count)
        ├── [spacer, flex_grow=1] (pushes ctrl to bottom)
        └── s_ctrl_panel          (OVW_RIGHT_W × 96, flex column)
            ├── s_target_lbl
            ├── s_slider          (OVW_RIGHT_W - 12 wide)
            └── s_action_btn      (OVW_RIGHT_W - 12 wide)
```

## File to Modify

`main/ui/ui_tank_overview.c` — single file change.

### What changes in `ui_tank_overview_create()`

1. Update `#define OVW_FILL_W`, `OVW_FILL_H`, and add `OVW_RIGHT_W` / `TILE_H`.
2. Remove `fill_wrap` (the centered wrapper that gave the old flat bar its height).
3. Add `body_row` as the second child of `s_screen` (flex row, styled like a bare container).
4. Reparent `s_fill_outer` into `body_row` (left child). Width/height now use the new constants.
5. Add `right_col` as the right child of `body_row` (flex column, `OVW_RIGHT_W × OVW_FILL_H`).
6. Change tile creation loop parent from `s_screen` to `right_col`; tile width changes from `CONTENT_W - OVW_PAD*2` to `OVW_RIGHT_W`.
7. Add a `lv_obj_set_flex_grow(spacer, 1)` spacer after the tiles loop.
8. Move `s_ctrl_panel` creation into `right_col`; update slider and button widths to `OVW_RIGHT_W - 12`.

### What does NOT change

- All callbacks (`fill_tap_cb`, `tile_tap_cb`, `slider_cb`, `action_btn_cb`, `anim_timer_cb`)
- `ovw_refresh_fill()` — uses `OVW_FILL_H` which is now a larger constant; math stays correct
- `ovw_show_target_line()` — same, uses `OVW_FILL_H`
- `ui_tank_overview_activate()` and `ui_tank_overview_deactivate()` — no changes needed
- All touch targets and interactions remain identical

### Tick mark positions at creation

Tick marks use absolute position relative to `s_fill_outer`. The formula `ty = (OVW_FILL_H - 2) * (100 - p) / 100` still works; the ticks will be spaced along the new taller container. Tick width `OVW_FILL_W / 4 = 50 px`, x position `OVW_FILL_W - OVW_FILL_W/4 - 2 = 148`.

## Verification

1. Build: `idf.py build` — zero warnings expected, no other files touched.
2. Flash: `idf.py flash` to the connected ESP32-P4.
3. Screenshot: run `python3 tools/screenshot.py` and confirm:
   - Fill bar is tall and narrow on the left (~200 px wide, full available height)
   - 5 metric rows fill the right column top
   - Fill/drain slider and action button are at the bottom of the right column
   - Tapping fill bar still opens the detail overlay
   - Tapping a metric row still opens the metric detail overlay
   - Fill/drain animation still works correctly
4. Test with a tank that has fewer than 5 metrics — ctrl panel should still be at the bottom of the right column (spacer absorbs the gap).
