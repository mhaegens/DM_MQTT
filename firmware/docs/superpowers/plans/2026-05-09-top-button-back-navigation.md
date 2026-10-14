# Top Button Universal Back Navigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the top physical button (GPIO9 / BOOT) navigate back on every screen — popping any overlay, or returning to Home from any root tab.

**Architecture:** Two-line change in `ui_btn_short_press()`. The `ViewId` enum already groups tabs at 0–4 and overlays at 5+, so a range check (`> VIEW_NFC`) covers all current and future overlays without enumerating them. A comment is added to the enum to make this ordering contract explicit.

**Tech Stack:** ESP-IDF, LVGL 9, C17, `idf.py build flash monitor`

---

### Task 1: Document the enum ordering contract in `ui_main.h`

**Files:**
- Modify: `firmware/main/ui/ui_main.h:15-26`

- [ ] **Step 1: Open `ui_main.h` and locate the `ViewId` enum (lines 15–26)**

Current code:
```c
typedef enum {
    VIEW_HOME     = 0,
    VIEW_MACHINES = 1,
    VIEW_TANKS    = 2,
    VIEW_DEBUG    = 3,
    VIEW_NFC      = 4,
    VIEW_DETAIL    = 5,   // not a tab; pushed over any tab view
    VIEW_TANK_OVW  = 6,   // not a tab; pushed over Tanks tab
    VIEW_WIFI_SETUP  = 7,  // not a tab; pushed over any tab via WiFi pill/card
    VIEW_MQTT_DETAIL = 8,  // not a tab; pushed over any tab via MQTT pill
    VIEW_COUNT       = 9,
} ViewId;
```

- [ ] **Step 2: Add ordering-contract comment above the enum**

Replace the enum with:
```c
// ORDERING CONTRACT: tab screens (VIEW_HOME..VIEW_NFC) MUST stay before overlay
// screens (VIEW_NFC+1..). ui_btn_short_press() uses > VIEW_NFC to distinguish them.
typedef enum {
    VIEW_HOME     = 0,
    VIEW_MACHINES = 1,
    VIEW_TANKS    = 2,
    VIEW_DEBUG    = 3,
    VIEW_NFC      = 4,
    VIEW_DETAIL    = 5,   // not a tab; pushed over any tab view
    VIEW_TANK_OVW  = 6,   // not a tab; pushed over Tanks tab
    VIEW_WIFI_SETUP  = 7,  // not a tab; pushed over any tab via WiFi pill/card
    VIEW_MQTT_DETAIL = 8,  // not a tab; pushed over any tab via MQTT pill
    VIEW_COUNT       = 9,
} ViewId;
```

- [ ] **Step 3: Verify the file compiles**

```bash
cd /path/to/DM_MQTT/firmware && idf.py build 2>&1 | tail -5
```
Expected: `Build successful` with no errors or warnings referencing `ui_main.h`.

---

### Task 2: Extend `ui_btn_short_press()` to cover all screens

**Files:**
- Modify: `firmware/main/ui/ui_main.c:603-607`

- [ ] **Step 1: Locate `ui_btn_short_press()` at line 603**

Current code:
```c
void ui_btn_short_press(void)
{
    if (g_current_view == VIEW_DETAIL || g_current_view == VIEW_TANK_OVW)
        ui_pop_detail();
}
```

- [ ] **Step 2: Replace with the universal back handler**

```c
void ui_btn_short_press(void)
{
    if (g_current_view > VIEW_NFC)      // any overlay screen
        ui_pop_detail();
    else if (g_current_view != VIEW_HOME)
        ui_switch_tab(VIEW_HOME);
}
```

`ui_pop_detail()` already handles all four overlay screens correctly:
- `VIEW_DETAIL` → hides crumb bar, returns to `g_prev_view` tab (or Tank OVW if nested)
- `VIEW_TANK_OVW` → calls `ui_tank_overview_deactivate()`, returns to Tanks
- `VIEW_WIFI_SETUP` → hides crumb bar, calls `ui_switch_tab(g_prev_view)`
- `VIEW_MQTT_DETAIL` → falls through to standard path, hides crumb bar, switches to `g_prev_view`

- [ ] **Step 3: Build to confirm no compile errors**

```bash
cd /path/to/DM_MQTT/firmware && idf.py build 2>&1 | tail -10
```
Expected: `Build successful`.

---

### Task 3: Flash and verify on device

- [ ] **Step 1: Flash the firmware**

```bash
cd /path/to/DM_MQTT/firmware && idf.py flash monitor
```
Expected: Flash succeeds, device boots, LVGL UI comes up. Press Ctrl+] to exit monitor.

- [ ] **Step 2: Ask user to run the verification checklist**

Ask the user to test each scenario on the physical device:

| Test | Steps | Expected |
|---|---|---|
| WiFi overlay back | Tap WiFi pill in shell bar → press top button | Returns to previous screen |
| MQTT overlay back | Tap MQTT pill in shell bar → press top button | Returns to previous screen |
| Detail overlay back | Tap a machine metric → press top button | Returns to Machines tab |
| Tank OVW back | Tap a tank → press top button | Returns to Tanks tab |
| Tab → Home | Navigate to Machines/Tanks/Debug/NFC → press top button | Switches to Home tab |
| Home no-op | Stay on Home tab → press top button | Nothing happens |
| Long press unchanged | Tap a machine metric (Detail view) → long-press top button | Metric steps +1 |

- [ ] **Step 3: Confirm user approval before committing**

Do not proceed to commit until the user confirms the device behaves correctly.

---

### Task 4: Commit and push for PR

- [ ] **Step 1: Stage the two changed files**

```bash
git add firmware/main/ui/ui_main.h firmware/main/ui/ui_main.c
```

- [ ] **Step 2: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(button): top button navigates back on all screens

Short press now pops any overlay (Detail, Tank OVW, WiFi Setup, MQTT
Detail) via ui_pop_detail(), or returns to Home from any root tab.
Previously only Detail and Tank OVW were handled.

Uses ViewId range check (> VIEW_NFC) so future overlay screens
inherit back navigation automatically. Ordering contract documented
in the enum.
EOF
)"
```

- [ ] **Step 3: Push and open PR**

```bash
git push -u origin claude/firmware-bsp-st7703-fonts
gh pr create \
  --title "feat(button): top button navigates back on all screens" \
  --body "$(cat <<'EOF'
## Summary
- `ui_btn_short_press()` now covers all four overlay screens (Detail, Tank OVW, WiFi Setup, MQTT Detail) using a `ViewId` range check instead of an explicit list
- Pressing the button on any root tab (Machines, Tanks, Debug, NFC) navigates to Home; pressing on Home is a no-op
- Long press behaviour (step metric in Detail view) is unchanged
- Ordering contract documented in `ViewId` enum comment

## Test plan
- [ ] WiFi Setup overlay: tap WiFi pill → top button → returns to previous screen
- [ ] MQTT Detail overlay: tap MQTT pill → top button → returns to previous screen
- [ ] Detail overlay: tap machine metric → top button → returns to Machines tab
- [ ] Tank OVW overlay: tap tank → top button → returns to Tanks tab
- [ ] Root tab → Home: navigate to Machines → top button → Home tab
- [ ] Home no-op: on Home tab → top button → nothing happens
- [ ] Long press unchanged: in Detail view → long press → metric steps +1
EOF
)"
```
