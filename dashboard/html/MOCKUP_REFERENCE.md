# Shop Floor Simulator — HTML Mockup Reference

> Source: `dashboard/html/index.html` · Target device: Waveshare ESP32-P4 · 720×720 IPS display

---

## Overview

Single-file standalone HTML mockup (no external dependencies). All CSS and JS are embedded. Designed for a 720×720 square display. Has a responsive breakpoint at 800px that scales all sizing to `vw` units for testing on desktop browsers.

---

## Shell (Always Visible)

The top header bar (64px high) is persistent across all screens.

| Element | Detail |
|---|---|
| SAP badge | 44×44px rounded blue square, "SAP" text |
| Title | "Shop Floor Simulator" — IBM Plex Sans 600, 22px |
| Subtitle | "SAP Digital Manufacturing · Plant BE01" — gray, 15px |
| MQTT badge | Green animated dot + "MQTT" label — pulses every 2s |
| WiFi badge | Blue animated dot + "WIFI" label — pulses every 2s |

The pulsing dots use a keyframe (`pdot`) that cycles opacity 1 → 0.25 → 1 over 2s.

---

## Tab Bar (Always Visible)

Fixed at bottom, 72px high, 5 equal-width buttons.

| Tab | Icon | Screen |
|---|---|---|
| Home | House SVG | Home screen |
| Machines | Grid SVG | Machine cards |
| Tanks | Cylinder SVG | Tank visualization |
| Debug | Terminal SVG | Log + system info |
| NFC | Badge SVG | NFC operator scanner |

Active tab: 3px solid blue underline, text turns primary blue.
Inactive tab: gray text, transitions to blue on hover (0.15s).

---

## Navigation Tree

```
Shell (always)
└── Tab Bar (always)
    ├── Home
    ├── Machines
    │   └── [click metric tile] → Detail View (metric deepdive)
    ├── Tanks
    │   ├── [click tank body] → Detail View (metric deepdive)
    │   └── [click tank label] → Tank Overview
    │       └── [click metric tile] → Detail View (metric deepdive)
    ├── Debug
    └── NFC
        ├── Physical Mode
        └── Virtual Mode (badge carousel)
```

Breadcrumb nav (44px, appears in Detail and Tank Overview): Back button + "/" + current page name. Clicking back returns one level.

---

## Screens

### Home

**Purpose:** Status overview — is everything running?

**KPI Grid (2×2):**

| KPI | Value | Sub-note | Color bar |
|---|---|---|---|
| Running | 3 / 4 | "CNC-02 stopped" | Primary blue |
| Pulses Today | 1,284 | "+42 last 5 min" | Primary blue |
| MQTT Msgs | 387 | "since boot" | Warning orange |
| WiFi RSSI | -61 | "dBm · Ch 6" | Teal |

Cards have a 4px colored top bar indicating status. Cards themselves are white with a subtle drop shadow.

**Connection Rows (3 rows below the KPIs):**

| Label | Value | Badge |
|---|---|---|
| MQTT Broker | 10.8.0.1:1883 | "Connected" (green) |
| Device IP | 192.168.43.112 | "DHCP" (blue) |
| Operator | M. Haegens | "Active" (green) |

Rows are flex, space-between layout. Badges are 9px monospace, uppercase, padded, color-coded.

**Actions:** None — Home is read-only.

---

### Machines

**Purpose:** Real-time operational metrics for each machine.

**Layout:** 2×2 grid of machine cards. 4 machines: CNC-01 (running), CNC-02 (stopped), FILL-01 (running), PACK-01 (running).

**Machine Card Anatomy:**

```
┌─ Header (border-bottom)
│  ├─ Status dot (14px circle)
│  ├─ Machine name: "CNC-01" (26px, 600 weight)
│  └─ Type label: "CNC Machining" (9px monospace, uppercase)
├─ Body
│  ├─ Sparkline tile: RPM history (SVG line chart)
│  ├─ Arc tiles: Spindle Load, Tool Wear (circular progress)
│  └─ Data tiles: Temperature, Feed Rate (value cards)
└─ Actions (border-top)
   ├─ Stop / Start button (toggles state)
   └─ Pulse button (sends simulated pulse)
```

**Status dot behavior:**
- Running: Green, animated halo glow (`sdp` keyframe, 2s) — 0 0 0 4px halo + 10px green glow
- Stopped: Red, static — 0 0 0 3px halo, no animation

**Running card border:** Turns green with glow. Stopped card border turns red.

**Metric Tile (mtile):**
- Background: `#EEF1F8`
- Border: 1px `#DDE1EA`
- Rounded: 8px
- Label: 12px monospace, uppercase, gray
- Value: 26px monospace, 500 weight, color-coded (green/orange/red by threshold)
- Unit: 15px monospace, gray
- Hover/active: border turns blue, background turns light blue
- Small chevron `>` pseudo-element top-right corner (indicates clickable → Detail)

**Sparkline tile (stile):**
- SVG line chart, last 32 values, no axes
- Color and gradient fill are metric-dependent
- Last data point marked with small circle

**Arc tile (arctile):**
- SVG circular progress arc
- Background track + colored fill arc
- Color determined by value thresholds (green/orange/red)
- Center: value + unit label

**Stop/Start button:** Toggling a running machine changes card border, dot state, and button between danger (red tones) and success (green tones) styles.

**Pulse button:** Ghost style. Simulates a production pulse event on that machine.

**Clicking any metric tile** → opens **Detail View** for that metric.

---

### Tanks

**Purpose:** Visual fill-level monitoring with drag-to-adjust interaction.

**Layout:** 3 equal columns side by side, full height. Tanks: Raw Material (68%), Production (28%), Waste (11%).

**Tank Shape (CSS mask):**
The tank shape is created using an SVG mask applied via `webkit-mask-image`:
- Cylinder body: `<rect rx="7">` — square with rounded corners at top
- Cone bottom: `<path>` using quadratic Bézier curves from ~78% height tapering to a point at 100%

This allows a smooth, industrial-looking storage vessel silhouette.

**Tank Fill Animation:**
- Animated wave using sine/cosine math in a `requestAnimationFrame` loop
- Wave amplitude: ~2px normally, ~5px when dragging (slosh effect)
- Gradient fill: light gray (`#ECF0F2`) to slightly darker shade
- White shine overlay: 180° gradient from top to 35%, fading to transparent
- Rising bubbles: 3 SVG circles per tank, each with staggered `rise` keyframe (1.8–3.3s)

**Level color thresholds:**

| Level | Color | Status badge |
|---|---|---|
| 0–15% | Red `#BB0000` | "Critical" |
| 15–30% | Orange `#E76500` | "Low Level" |
| 30–75% | Blue `#0070F2` | "Operational" |
| 75–100% | Cyan `#0092D1` | "Operational" |

**Percentage overlay:** Centered at ~24% from the bottom of the tank. White text with shadow.

**Tick marks:** Three horizontal marks at 25%, 50%, 75% with labels on the right edge.

**Indicator strip (bottom, 5 rows per tank):**
Each row: colored dot (8px) + label (12px monospace) + value (18px monospace) + unit (11px gray).
Metrics: LEVEL, TEMP, FLOW IN, pH, TURBIDITY.

**Tank label button (below tank body):**
- Tank name (9px monospace, uppercase)
- Status badge (color follows level threshold)
- **Clicking** → opens **Tank Overview**

**Interactions:**

| Action | Behavior |
|---|---|
| Click tank body | Opens Detail View for level metric |
| Drag up/down on tank | Adjusts fill %, cursor becomes ns-resize |
| Drag start | Slosh amplitude increases to 5px |
| Drag release | Splash particle animation: 24×24px border circle scales out + fades (`splash-out`, 0.4s) |
| Click label button | Opens Tank Overview |

---

### Tank Overview

**Purpose:** Deep-dive into one specific tank with all its metrics.

Breadcrumb: `← / Tank Overview`.

Contains:
- Larger tank visualization (same shape, bigger)
- All metric tiles for that tank (clickable → Detail)
- Status badge more prominent

**Actions:** Clicking any metric tile → Detail View.

---

### Detail View

**Purpose:** Full-screen view for a single metric — see current value, history, and modify it.

Breadcrumb: `← / [metric name]`.

**Components (top to bottom):**
1. **Metric header:** Name + current value large
2. **Visualization** (metric-dependent, see below)
3. **Input control** (step input or slider)
4. **Random value generator:** [Generate Once] + [Start / Stop Interval] + interval dropdown + min/max inputs

**Visualization types by metric:**

| Viz type | Used for | Description |
|---|---|---|
| Sparkline | RPM, Flow, Pressure | SVG line chart with gradient fill, last 32 values |
| Arc gauge | Spindle Load, Tool Wear, Turbidity | Circular progress arc |
| Thermometer | Temperature | Vertical bar + bulb; color-coded by temp |
| pH scale | pH | Horizontal rainbow gradient bar with pointer |
| Tank | Level | Same animated cylinder+cone as main Tanks screen |
| Bullet chart | Feed Rate, Pressure | Horizontal bar with 60%/80% threshold markers |

**Input: Step input** (used for RPM, integer-like metrics):
- Three-part: [−] [number field] [+]
- Buttons: 80px wide
- Input field: 80px tall, monospace 26px
- Focus: 3px blue ring

**Input: Slider** (used for Level, percentage metrics):
- 6px track, light gray
- 28px blue thumb
- Focus: 3px white border + blue shadow

**Random generator controls:**
- [Generate Once]: fires one random value in `[randMin, randMax]`
- [Start] / [Stop]: toggles interval generation
- Interval dropdown: 0.5s / 1s / 2s / 5s / 10s
- Min / Max inputs for range customization

Value updates flash the display: `vflash` keyframe — scales value text + briefly changes color.

---

### Debug

**Purpose:** System diagnostics and live MQTT log.

**Info Grid (2×2):**

| | |
|---|---|
| Broker | 10.8.0.1:1883 |
| MQTT State | "Connected" (green text) |
| Device IP | 192.168.43.112 |
| RSSI | -61 dBm (blue text) |

**Log Panel:**
- Dark background: `#080F1C`
- Monospace font, 18px line-height
- Max 7 lines visible, newest at bottom
- Each line format: `[timestamp] [type] message`

| Line class | Color | Meaning |
|---|---|---|
| `.new` | Light blue `#E8EDF5` | Most recent entry |
| `.ok` | Green `#4CC87A` | Successful operation |
| `.err` | Red `#FF6666` | Error/failure |
| `.pub` | Blue `#5BB2FF` | MQTT publish |

Example entries:
```
00:12:34  PUB  factory/machines/cnc01/rpm → 4850
00:12:31  OK   NFC badge read · UID A3F7C2 → M.Haegens
00:12:28  OK   MQTT reconnected · rc=0
```

**Simulate button:** Fires a simulated MQTT publish and adds it to the log.

**Actions:** No navigation. Log auto-scrolls on new entries.

---

### NFC

**Purpose:** Operator identification — physical badge tap or virtual badge selection.

**Mode toggle (top):** Two buttons — "Physical" and "Virtual". Active mode: blue background + white text. Inactive: gray.

#### NFC Physical Mode

**Visual center element — NFC Ring (150×150px):**
- Outer ring: Blue border + radial gradient fill
- Spinning ring: Dashed blue border, rotates 360° in 5s (`spin` keyframe)
- Inner ring: Blue border
- NFC icon (48×48px): Pulsing scale 1 → 1.1 → 1 (`nfcp`, 2.5s)

**Below ring:**
- Instruction: "Tap Badge to Identify Operator"
- Note: "MIFARE Classic · Hold badge within 5 cm of device"
- [Simulate Tap] button (ghost style)

**After scan:** Scanned result card slides up (`slideup`, 0.35s) showing employee name, role, UID.

#### NFC Virtual Mode — Badge Carousel

5 employee badge cards in a 3D perspective carousel.

**Carousel layout:**
- Cards: 390×232px, 22px gap
- Active (center): `scale(1)`, `translateY(0)`, full shadow
- ±1 from center: `scale(0.84)`, `translateY(14px)`
- ±2 from center: `scale(0.72)`, `translateY(22px)`, `opacity: 0.55`
- Transition: `cubic-bezier(.4,0,.2,1)`, 0.38s

**Badge Card Anatomy:**
```
┌─ Holographic top strip (4px) — rainbow gradient, animates position (holoshift, 3s)
├─ Security background pattern — diagonal stripes (repeating-linear-gradient -45°)
├─ Role-based gradient background (3-stop)
├─ Top row: SAP logo | Plant BE01 | Employee ID
├─ Middle: Avatar circle (68px, initials) + Name + Role chip + Cert badges
└─ Footer: UID | Department | NFC icon
```

**Role gradient themes:**

| Role | Gradient |
|---|---|
| Operator | Blue: `#0F2647 → #1A3A6B → #102040` |
| Shift Lead | Green: `#0A3D2B → #155F3C → #0A3020` |
| Engineer | Brown: `#3D1F00 → #6B3800 → #341A00` |
| Supervisor | Purple: `#1A0A3D → #2E1566 → #140830` |
| Quality | Red: `#2A0000 → #5A0000 → #220000` |

**Carousel navigation:**
- Left/right arrow buttons (52×52px circles, ghost style)
- Swipe: drag more than 60px left or right triggers prev/next
- Dot indicators: inactive = 7px gray circle, active = 22px wide blue bar
- Counter label: "1 of 5"

**[Select Operator] button:**
- Primary blue, full-width, 80px tall
- On confirm: pulse animation (`cpulse`, 0.5s) — box-shadow expands 0→18px
- Turns green with checkmark after confirmation

---

## Color Palette

### Semantic Colors

| Token | Hex | Usage |
|---|---|---|
| `--hi` | `#0070F2` | Primary blue — active states, primary buttons |
| `--hi-dk` | `#0050BE` | Darker blue — button borders, hover |
| `--hi-lt` | `#E8F2FF` | Light blue — hover fills, badge backgrounds |
| `--hi-mid` | `#C5DCFF` | Mid blue — borders in blue contexts |
| `--ok` | `#107E3E` | Green — running, connected, success |
| `--ok-lt` | `#E3F5EC` | Light green — success button fill |
| `--ok-bd` | `#9ADDB8` | Green border |
| `--warn` | `#E76500` | Orange — warnings, MQTT KPI |
| `--warn-lt` | `#FEF0E5` | Light orange |
| `--warn-bd` | `#F8C07E` | Orange border |
| `--err` | `#BB0000` | Red — errors, critical, stopped |
| `--err-lt` | `#FFE8E8` | Light red — stop button fill |
| `--err-bd` | `#FFAAAA` | Red border |
| `--teal` | `#045B7A` | Teal — RSSI KPI |
| `--pur` | `#6A28C8` | Purple |

### Neutral Colors

| Token | Hex | Usage |
|---|---|---|
| `--bg` | `#F2F4FA` | Page background |
| `--base` | `#FFFFFF` | Card/surface background |
| `--base2` | `#EEF1F8` | Slightly off-white — metric tiles |
| `--base3` | `#E3E6EF` | Lighter fill |
| `--bd` | `#DDE1EA` | Standard border |
| `--bd-str` | `#B8BFCC` | Strong border |
| `--tx` | `#1D2D3E` | Primary text |
| `--tx2` | `#4A6080` | Secondary text |
| `--tx3` | `#8090A8` | Tertiary / placeholder text |

---

## Typography

### Size Scale (CSS variables)

| Token | px value | Used for |
|---|---|---|
| `--f-xl` | 40px | Extra large headings |
| `--f-lg` | 32px | Large headings, big metric values |
| `--f-md` | 26px | Metric tile values, step input |
| `--f-sm` | 22px | Shell title, buttons |
| `--f-xs` | 18px | Breadcrumb, log entries |
| `--f-xxs` | 15px | Labels, badges, small monospace |

### Touch Target Heights
- `--ts` = 80px — primary buttons, step input height
- `--ts-sm` = 64px — select/dropdown height

---

## Graphical Style Decisions

### Tank Shape
The key visual identity of the Tanks screen. Shape achieved via SVG `<mask>`:
- **Top portion (0–78%):** `<rect rx="7">` — straight sides with slightly rounded top corners
- **Bottom portion (78–100%):** Bézier quadratic curves tapering to a narrow cone point

This represents an industrial vessel — taller than wide, with the characteristic tapered bottom of a storage silo or mixing tank. The shape is unique and immediately recognizable at a glance.

### Filled Water Effect
Three techniques stacked:
1. **Wave animation** — sine/cosine path redrawn each animation frame
2. **Gradient fill** — fills from `#ECF0F2` to a slightly darker shade
3. **Shine overlay** — white linear-gradient from top to 35%, opacity fades out

Combines to give the impression of a real liquid surface catching light.

### Card Color Bars
KPI cards use a 4px solid top border strip to encode status at a glance. Avoids putting color on the entire card background — keeps the UI calm while still signaling urgency.

### Badge Cards
Designed to resemble physical access control cards:
- Holographic rainbow strip along the top (background-position animation)
- Repeating diagonal stripe security pattern as background noise
- Dark role-toned gradient (not flat, but 3-stop) for depth
- All text is white or semi-transparent white for contrast

### Status Dots (Machines)
Running state uses a layered glow effect — not just color, but a real-time pulse that makes it feel like the machine is truly alive. Stopped state has a static halo — present, but inert.

### Borders
- Most UI: 1px borders
- Interactive or emphasized elements: 1.5px borders
- Standard radius: 12px for cards, 8px for tiles, 6px for small elements
- Tab bar active indicator: 3px underline (no radius — flat, technical feel)

---

## Animations Summary

| Name | Duration | Trigger | Effect |
|---|---|---|---|
| `pdot` | 2s | Continuous | Shell status dot opacity pulse |
| `sdp` | 2s | Running machine | Green dot glow pulse |
| `spin` | 5s | NFC physical mode | Dashed ring rotation |
| `nfcp` | 2.5s | NFC physical mode | NFC icon scale pulse |
| `holoshift` | 3s | Badge cards | Holographic strip moves |
| `rise` | 1.8–3.3s | Tanks screen | Bubbles rise + fade |
| `splash-out` | 0.4s | Tank drag release | Splash ring expands + fades |
| `slideup` | 0.35s | NFC scan result | Card slides up from bottom |
| `vflash` | 0.4s | Value update | Metric value scales + flashes |
| `cpulse` | 0.5s | Confirm button | Box-shadow pulse |
| `rAF loop` | ~16ms | Tanks screen | Wave + bubble redraw |

---

## Mock Data

### Machines
4 machines defined in JS constant `MACH`:
- **CNC-01** (running): RPM 4850, Spindle Load 62%, Tool Wear 34%, Feed Rate 380, Temp 41.2°C
- **CNC-02** (stopped): same metric structure, all at rest values
- **FILL-01** (running): filling machine metrics (flow rate, pressure, level)
- **PACK-01** (running): packaging machine metrics

Each machine has a 32-value history array per metric for sparkline rendering.

### Tanks
3 tanks in `TANKS` constant:
- **Raw Material** (68%): Level, Temp 22.4°C, Flow In 4.2 L/min
- **Production** (28%): same metric set
- **Waste** (11%): same metric set

### Badges (NFC Virtual)
5 employees in `BADGES`:
- Michiel Haegens — Shift Lead — EMP-0042 — UID A3:F7:C2:91
- 4 others across Operator, Engineer, Supervisor, Quality roles

### Metric Definitions
~20+ metric types in `M` constant, each with: label, unit, min, max, step, viz type, input type, color, random range.
