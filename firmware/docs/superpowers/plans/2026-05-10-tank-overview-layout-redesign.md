# Tank Overview Layout Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redesign `ui_tank_overview.c` from a stacked-column layout to a two-column layout: tall narrow fill bar on the left, metric tiles + fill/drain controls on the right.

**Architecture:** Single-file change. Replace the centered `fill_wrap` container with a `body_row` flex-row container that holds `s_fill_outer` (left) and a new `right_col` flex-column (right). Metric tiles and the control panel move into `right_col`; a flex-grow spacer pushes the control panel to the bottom of the right column.

**Tech Stack:** LVGL 9, ESP-IDF, C. Build with `idf.py build`, flash with `idf.py flash`, screenshot with `python3 tools/screenshot.py`.

---

### Task 1: Update dimension macros

**Files:**
- Modify: `main/ui/ui_tank_overview.c:12-15`

This project has no host-side unit tests; verification is a clean build followed by flash + screenshot.

- [ ] **Step 1: Replace the four `#define` lines at the top of the file**

Replace lines 12–15:
```c
#define OVW_FILL_H  120
#define OVW_FILL_W  400
#define TILE_H       40
#define OVW_PAD     SZ_GAP
```
With:
```c
#define OVW_FILL_W  200
#define OVW_FILL_H  (DETAIL_H - OVW_PAD * 2 - 8 - 48)
#define OVW_RIGHT_W (CONTENT_W - OVW_PAD * 2 - OVW_FILL_W - SZ_GAP)
#define TILE_H       48
#define OVW_PAD     SZ_GAP
```
`OVW_FILL_H` evaluates to `612 - 20 - 8 - 48 = 536 px` (full height below the header).
`OVW_RIGHT_W` evaluates to `720 - 20 - 200 - 10 = 490 px`.

- [ ] **Step 2: Verify the build still compiles cleanly**

```bash
cd /path/to/DM_MQTT/firmware && idf.py build 2>&1 | tail -20
```
Expected: `Project build complete.` — no errors, possibly warnings about unused `OVW_FILL_H` value (fine at this stage).

- [ ] **Step 3: Commit**

```bash
git add main/ui/ui_tank_overview.c
git commit -m "refactor(tank_ovw): update layout dimension macros for two-column design"
```

---

### Task 2: Replace `fill_wrap` with `body_row` and reparent `s_fill_outer`

**Files:**
- Modify: `main/ui/ui_tank_overview.c:254-271`

- [ ] **Step 1: Replace the `fill_wrap` block and update `s_fill_outer` parent/alignment**

Find this block (lines ~254–271):
```c
    // ── Fill bar wrapper (centers OVW_FILL_W inside full content width) ────────
    lv_obj_t *fill_wrap = lv_obj_create(s_screen);
    lv_obj_remove_style_all(fill_wrap);
    lv_obj_set_size(fill_wrap, CONTENT_W - OVW_PAD * 2, OVW_FILL_H);
    lv_obj_clear_flag(fill_wrap, LV_OBJ_FLAG_SCROLLABLE);

    s_fill_outer = lv_obj_create(fill_wrap);
    lv_obj_set_size(s_fill_outer, OVW_FILL_W, OVW_FILL_H);
    lv_obj_align(s_fill_outer, LV_ALIGN_CENTER, 0, 0);
```

Replace with:
```c
    // ── Two-column body row ─────────────────────────────────────────────────────
    lv_obj_t *body_row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(body_row);
    lv_obj_set_size(body_row, CONTENT_W - OVW_PAD * 2, OVW_FILL_H);
    lv_obj_set_flex_flow(body_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(body_row, SZ_GAP, 0);
    lv_obj_clear_flag(body_row, LV_OBJ_FLAG_SCROLLABLE);

    s_fill_outer = lv_obj_create(body_row);
    lv_obj_set_size(s_fill_outer, OVW_FILL_W, OVW_FILL_H);
```

Note: `lv_obj_align(LV_ALIGN_CENTER)` is removed — `body_row` flex handles positioning.

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -20
```
Expected: `Project build complete.`

- [ ] **Step 3: Commit**

```bash
git add main/ui/ui_tank_overview.c
git commit -m "refactor(tank_ovw): replace fill_wrap with body_row flex-row container"
```

---

### Task 3: Add `right_col` container and update internal fill sizes

**Files:**
- Modify: `main/ui/ui_tank_overview.c` — after `s_fill_outer` block, before metric tile loop

The fill_bar, shine, tick marks, target_line, and pct_lbl are all children of `s_fill_outer` and need no reparenting. However their width references (`OVW_FILL_W - 2`, `(OVW_FILL_W - 2) / 3`, `OVW_FILL_W / 4`, `OVW_FILL_W - 2`) are macro-derived and automatically correct with the new `OVW_FILL_W = 200`. No edits needed to those child objects.

- [ ] **Step 1: Add `right_col` immediately before the metric tile loop**

Find the comment `// ── Metric tile list (all metrics, create MAX_TANK_METRICS rows) ───────────` and insert the block directly above it:

```c
    // ── Right column (metrics + controls) ───────────────────────────────────────
    lv_obj_t *right_col = lv_obj_create(body_row);
    lv_obj_remove_style_all(right_col);
    lv_obj_set_size(right_col, OVW_RIGHT_W, OVW_FILL_H);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(right_col, 8, 0);
    lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
```

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -20
```
Expected: `Project build complete.`

- [ ] **Step 3: Commit**

```bash
git add main/ui/ui_tank_overview.c
git commit -m "refactor(tank_ovw): add right_col flex-column container in body_row"
```

---

### Task 4: Move metric tiles into `right_col`

**Files:**
- Modify: `main/ui/ui_tank_overview.c` — metric tile creation loop (~lines 332–379)

- [ ] **Step 1: Change tile parent and width in the creation loop**

In the `for (int k = 0; k < MAX_TANK_METRICS; k++)` loop, make two changes:

**Change 1** — parent from `s_screen` to `right_col`:
```c
// OLD:
        lv_obj_t *tile = lv_obj_create(s_screen);
// NEW:
        lv_obj_t *tile = lv_obj_create(right_col);
```

**Change 2** — width from `CONTENT_W - OVW_PAD * 2` to `OVW_RIGHT_W`:
```c
// OLD:
        lv_obj_set_size(tile, CONTENT_W - OVW_PAD * 2, TILE_H);
// NEW:
        lv_obj_set_size(tile, OVW_RIGHT_W, TILE_H);
```

- [ ] **Step 2: Add a flex-grow spacer after the tile loop (before the ctrl panel)**

Immediately after the closing `}` of the tile loop, add:
```c
    // Spacer pushes ctrl panel to the bottom of right_col
    lv_obj_t *spacer = lv_obj_create(right_col);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, OVW_RIGHT_W, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
```

- [ ] **Step 3: Build**

```bash
idf.py build 2>&1 | tail -20
```
Expected: `Project build complete.`

- [ ] **Step 4: Commit**

```bash
git add main/ui/ui_tank_overview.c
git commit -m "refactor(tank_ovw): move metric tiles into right_col, add spacer"
```

---

### Task 5: Move control panel into `right_col` and update its child widths

**Files:**
- Modify: `main/ui/ui_tank_overview.c` — ctrl panel creation block (~lines 382–431)

- [ ] **Step 1: Change ctrl panel parent and width**

```c
// OLD:
    s_ctrl_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_ctrl_panel, CONTENT_W - OVW_PAD * 2, 96);
// NEW:
    s_ctrl_panel = lv_obj_create(right_col);
    lv_obj_set_size(s_ctrl_panel, OVW_RIGHT_W, 96);
```

- [ ] **Step 2: Update slider width**

```c
// OLD:
    lv_obj_set_size(s_slider, CONTENT_W - OVW_PAD * 2 - 12, 28);
// NEW:
    lv_obj_set_size(s_slider, OVW_RIGHT_W - 12, 28);
```

- [ ] **Step 3: Update action button width**

```c
// OLD:
    lv_obj_set_size(s_action_btn, CONTENT_W - OVW_PAD * 2 - 12, 36);
// NEW:
    lv_obj_set_size(s_action_btn, OVW_RIGHT_W - 12, 36);
```

- [ ] **Step 4: Build**

```bash
idf.py build 2>&1 | tail -20
```
Expected: `Project build complete.` — this is the full layout change, build must be clean.

- [ ] **Step 5: Commit**

```bash
git add main/ui/ui_tank_overview.c
git commit -m "refactor(tank_ovw): move ctrl panel into right_col, two-column layout complete"
```

---

### Task 6: Flash, screenshot, and verify

- [ ] **Step 1: Flash to device**

```bash
idf.py flash 2>&1 | tail -20
```
Expected: `Hard resetting via RTS pin...` (successful flash).

- [ ] **Step 2: Take screenshot while on the tank overview screen**

Navigate the device to any tank detail view, then:
```bash
python3 tools/screenshot.py
```
Expected: `Saved screenshot.png (720x720)`

- [ ] **Step 3: View the screenshot and confirm the new layout**

Open `screenshot.png` and verify:
- Fill bar is tall and narrow (~200 px wide) on the left side
- Metric rows (Tank Level, Temperature, etc.) appear in a column to the right of the fill bar
- Fill/drain slider and action button are at the bottom of the right column
- No content appears below the two-column area (everything fits on one screen)

- [ ] **Step 4: Verify tap targets still work**

Tap the fill bar on the device — confirm the metric detail overlay opens.
Tap a metric row — confirm the metric detail overlay opens with the correct metric.
Move the slider — confirm the target line appears on the fill bar and the action button updates.
Tap Fill/Drain — confirm the animation runs.

- [ ] **Step 5: Verify with a tank that has fewer metrics**

Navigate to a tank with fewer than 5 metrics (e.g. if one exists). Confirm the control panel is still at the bottom of the right column (the spacer absorbs the gap).

- [ ] **Step 6: Commit the screenshot as evidence**

```bash
git add screenshot.png
git commit -m "chore: add screenshot showing two-column tank overview layout"
```
