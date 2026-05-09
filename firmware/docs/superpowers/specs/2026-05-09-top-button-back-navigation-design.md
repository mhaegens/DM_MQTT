# Design: Top Button Universal Back Navigation

**Date:** 2026-05-09
**Status:** Approved

---

## Context

The ESP32-P4 device has two physical buttons: the bottom button resets the MCU. The top button (BOOT, GPIO9) is already wired as a user input and already fires short/long press callbacks in `ui_main.c`.

The current short-press handler only navigates back from two overlay screens (Metric Detail and Tank Overview). Pressing it while in WiFi Setup or MQTT Connection overlays does nothing, and pressing it on root tab screens (Home, Machines, etc.) also does nothing. The goal is to make the button behave as a universal "back/home" button across all screens.

---

## Behavior

| Current view | Button press | Result |
|---|---|---|
| VIEW_DETAIL | short | `ui_pop_detail()` → back to previous overlay or tab |
| VIEW_TANK_OVW | short | `ui_pop_detail()` → back to Tanks tab |
| VIEW_WIFI_SETUP | short | `ui_pop_detail()` → back to originating tab |
| VIEW_MQTT_DETAIL | short | `ui_pop_detail()` → back to originating tab |
| VIEW_MACHINES / TANKS / DEBUG / NFC | short | `ui_switch_tab(VIEW_HOME)` |
| VIEW_HOME | short | no-op |
| Any view | long | unchanged (`ui_detail_step(+1)` only in Detail view) |

---

## Implementation

### 1. `ui_main.c` — replace `ui_btn_short_press()`

**File:** `firmware/main/ui/ui_main.c` lines 603–607

Replace:
```c
void ui_btn_short_press(void)
{
    if (g_current_view == VIEW_DETAIL || g_current_view == VIEW_TANK_OVW)
        ui_pop_detail();
}
```

With:
```c
void ui_btn_short_press(void)
{
    if (g_current_view > VIEW_NFC)      // any overlay screen
        ui_pop_detail();
    else if (g_current_view != VIEW_HOME)
        ui_switch_tab(VIEW_HOME);
}
```

### 2. `ui_main.h` — document enum ordering constraint

**File:** `firmware/main/ui/ui_main.h`

Add a comment above or within the `ViewId` enum making the ordering contract explicit:
```c
// Tab screens MUST come before overlay screens (VIEW_HOME..VIEW_NFC = tabs,
// VIEW_NFC+1.. = overlays). ui_btn_short_press() relies on this ordering.
typedef enum { ... } ViewId;
```

---

## Approach Rationale

Approach B (enum-range check) was chosen over an explicit list. The `ViewId` enum already groups tabs at 0–`VIEW_NFC` (0–4) and overlays at 5+. Checking `g_current_view > VIEW_NFC` means any future overlay screen automatically inherits back navigation without updating this function. The ordering constraint is documented in the enum.

---

## Verification

1. Build and flash: `idf.py build flash monitor`
2. Test each overlay: tap WiFi pill → top button should dismiss WiFi Setup
3. Test MQTT overlay: tap MQTT pill → top button should dismiss MQTT Detail
4. Test existing overlays still work: tap machine metric → detail view → top button back
5. Test tab screens: while on Machines/Tanks/Debug/NFC → top button should go to Home
6. Test Home: while on Home → top button should do nothing (no crash, no navigation)
7. Test long press: in Detail view, long press should still step the metric
