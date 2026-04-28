# Product Requirements Document
## SAP Digital Manufacturing MQTT Shop Floor Simulator UI

**Version:** 1.0  
**Device:** Waveshare ESP32-P4 Smart 86 Box (720×720 IPS display)  
**Target Application:** SAP Digital Manufacturing shop floor monitoring and simulation

---

## Executive Summary

This document describes a comprehensive shop floor monitoring and simulation interface for SAP Digital Manufacturing. The application provides real-time visibility into machine operations, tank levels, MQTT connectivity, and operator management through a multi-view, touch-enabled dashboard running on a 720×720 smart display device.

---

## 1. Overall System Architecture

### 1.1 Core Structure
- **Display Resolution:** 720×720 pixels
- **Interface Framework:** HTML5 + CSS3 + JavaScript (vanilla, no framework dependencies)
- **Device:** Waveshare ESP32-P4 Smart 86 Box with LVGL 9 support
- **Connectivity:** MQTT protocol for real-time data telemetry
- **Design System:** SAP Fiori-inspired color palette and typography (IBM Plex Sans/Mono fonts)

### 1.2 Navigation Model
- **Tab-based main navigation:** 5 primary views accessible via persistent tab bar at bottom
- **Hierarchical detail navigation:** Breadcrumb trail for returning from detail views
- **Contextual views:** Detail view overlays metric-specific editing interfaces
- **State Persistence:** Current view and detail context maintained during navigation

---

## 2. Primary Views (Tab-Based Navigation)

### 2.1 HOME View (Default Landing Screen)

#### Purpose
Provide a high-level dashboard with system health KPIs and connection status.

#### Layout Components

**Shell Header (Fixed Top)**
- **Left Section:**
  - SAP badge logo (44×44px blue square with "SAP" text)
  - Application title: "Shop Floor Simulator"
  - Subtitle: "SAP Digital Manufacturing · Plant BE01"
- **Right Section:**
  - MQTT connection status indicator (green pulsing dot with "MQTT" label)
  - WiFi connection status indicator (blue pulsing dot with "WiFi" label)

**KPI Grid (2×2 Layout)**

1. **Running Machines KPI**
   - Label: "Running"
   - Value: "3 / 4" (numerator = running, denominator = total)
   - Metadata: Shows stopped machine (e.g., "CNC-02 stopped")
   - Color Indicator: Green accent bar at top

2. **Daily Pulses KPI**
   - Label: "Pulses Today"
   - Value: "1,284" (comma-formatted)
   - Metadata: Recent delta (e.g., "+42 last 5 min")
   - Color Indicator: Blue accent bar at top

3. **MQTT Message Count KPI**
   - Label: "MQTT Msgs"
   - Value: "387"
   - Metadata: Time context (e.g., "since boot")
   - Color Indicator: Orange accent bar at top

4. **WiFi Signal Strength KPI**
   - Label: "WiFi RSSI"
   - Value: "-61" (dBm)
   - Metadata: Channel info (e.g., "dBm · Ch 6")
   - Color Indicator: Teal accent bar at top

**Connection Status Cards**
Three horizontal cards showing configuration:
- **MQTT Broker:** Address and port (e.g., "10.8.0.1 : 1883") with "Connected" badge
- **Device IP:** IP address (e.g., "192.168.43.112") with "DHCP" badge
- **Operator:** Current operator name with "Active" badge

**Tab Bar (Fixed Bottom)**
- 5 equal-width tabs with icons and labels:
  1. Home (house icon)
  2. Machines (machinery icon)
  3. Tanks (tank icon)
  4. Debug (monitor icon)
  5. NFC (NFC signals icon)

#### User Interactions
- **Tab Selection:** Tap any tab to navigate to that view
- **Status Awareness:** KPIs update in real-time via MQTT updates
- **Operator Display:** Shows currently authenticated operator name

---

### 2.2 MACHINES View

#### Purpose
Display all machines on the shop floor with their operating status, key metrics, and control actions.

#### Layout Components

**Machine Grid Layout**
- **Grid:** 2 columns, 1-2 rows (dependent on machine count)
- **Spacing:** 10px gap between cards

**Machine Card Structure** (for each of 4 machines: CNC-01, CNC-02, FILL-01, PACK-01)

**Card Header**
- **Left Section:**
  - Machine name (e.g., "CNC-01") - large font, bold
  - Machine type (e.g., "CNC Machining") - small, uppercase, gray
- **Right Section:**
  - Status indicator dot:
    - **Green pulsing dot** = Running (with halo glow animation)
    - **Red static dot** = Stopped
- **Card Styling:**
  - Running state: Green border glow, success color border
  - Stopped state: Red border, error color border

**Metric Visualization Area**
Displays 1-4 metrics based on visualization type:

**1. Sparkline Metric** (for machines with historical data)
- **Example:** Spindle RPM
- **Layout:** Tile with metric name, value, and small SVG sparkline chart
- **Interaction:** Tap to open detail view for this metric
- **Color:** Styled by metric status (OK/Warn/Error)

**2. Arc Gauge Metrics** (for percentage/bounded metrics)
- **Examples:** Spindle Load, Tool Wear, OEE
- **Layout:** Grid row of up to 3 circular arc gauge tiles
- **Components per tile:**
  - Metric name (small, uppercase)
  - SVG arc gauge visualization (circular progress indicator)
  - Current value with color-coded status text
- **Interaction:** Tap any arc to open detail view
- **Color Coding:**
  - Green: 0-40%
  - Orange: 40-70%
  - Red: 70-100%

**3. Standard Metric Tiles** (for other metrics)
- **Examples:** Feed Rate, Temperature
- **Layout:** Grid of up to 3 tiles
- **Components per tile:**
  - Metric label (small, uppercase, gray)
  - Metric value (large, monospace font)
  - Unit label (small gray)
- **Interaction:** Tap to navigate to detail view
- **Styling:** White background, light border, subtle shadow

**Card Actions** (Bottom of card)
- **Grid Layout:** 2 equal-width buttons
- **Left Button:**
  - Running machine: "Stop" button (danger style - red background)
  - Stopped machine: "Start" button (success style - green background)
  - Action: Toggle machine running state
- **Right Button:**
  - "Pulse" button (ghost style - outline button)
  - Action: Increment pulse counter for the machine

#### Data Models for Machines

**Machine Definitions:**
1. **CNC-01** (Running)
   - Type: CNC Machining
   - Metrics: RPM (sparkline), Spindle Load (arc), Tool Wear (arc), Feed Rate (tile), Temperature (tile)
   - MQTT Topic: `factory/machines/cnc01`

2. **CNC-02** (Stopped)
   - Type: CNC Machining
   - Metrics: RPM (sparkline), Spindle Load (arc), Tool Wear (arc), Feed Rate (tile), Temperature (tile)
   - MQTT Topic: `factory/machines/cnc02`

3. **FILL-01** (Running)
   - Type: Filling Line
   - Metrics: Flow Rate (sparkline), Pressure (arc), Temperature (tile)
   - MQTT Topic: `factory/machines/fill01`

4. **PACK-01** (Running)
   - Type: Packaging
   - Metrics: Line Speed (sparkline), OEE (arc), Temperature (tile)
   - MQTT Topic: `factory/machines/pack01`

#### User Interactions
- **Tab Navigation:** Switch between Home/Machines/Tanks/Debug/NFC
- **Metric Detail View:** Tap any metric tile to open detail editor
- **Machine Control:**
  - Start/Stop button: Toggle machine running state
  - Pulse button: Increment machine pulse counter
- **Real-time Updates:** Machine metrics update as MQTT messages arrive
- **Status Feedback:** Visual feedback via color changes and animations

---

### 2.3 TANKS View

#### Purpose
Monitor tank fill levels and associated metrics with interactive fill level adjustment via dragging.

#### Layout Components

**Tanks Screen Layout**
- **Container:** Full-height flex container with flexible spacing
- **Row Structure:** Single row of 3 tank columns with equal flex distribution

**Tank Column** (repeated for Raw Material, Production, Waste tanks)

**Tank Name Label**
- **Content:** Tank name in uppercase (e.g., "RAW MATERIAL")
- **Styling:** Centered, small uppercase monospace font, gray color
- **Position:** Above tank visualization

**Tank Body Visualization** (Main Hero Component)
- **Dimensions:** Flex-fill height, with width set by column
- **Appearance:**
  - Light blue background (#ECF0F8)
  - Rounded corners (12px top, 8px bottom)
  - Dark border (2px, stroke color varies by level)
  - Subtle inset shadow
- **Interactive Elements:**
  - **Animated Fill Visualization:**
    - SVG-rendered water fill with gradient (darker at bottom)
    - Animated wave effect at surface
    - Wave amplitude increases during drag
    - Multiple bubble animation rising through liquid
  - **Fill Level Overlay:**
    - Large percentage text centered near bottom (e.g., "68%")
    - Text color adjusts (white on dark fill, dark on light fill)
    - Real-time update as fill changes
  - **Reference Tick Marks:**
    - Vertical tick lines at 25%, 50%, 75% fill levels
    - Small percentage labels on right edge

**Tank Interaction - Dragging Fill Level**
- **Gesture:** Vertical swipe/drag on tank body
- **Feedback:** Visual wave amplification during drag
- **Range:** 0-100% (constrained)
- **Update Behavior:**
  - Fill level updates in real-time during drag
  - Water level graphic re-renders smoothly
  - Percentage text updates continuously
  - Splash particle effect appears at drag location
- **Gesture Completion:**
  - Release to finalize new level
  - Wave animation dampens to normal

**Tank Metric Indicator Strip** (Below each tank)
- **Layout:** Horizontal flex row of buttons
- **Button per Metric:** 1 button for each non-level metric
- **Button Components:**
  - Small colored dot indicator (status color)
  - Metric name (small, uppercase)
  - Current value with unit
  - Styled border that highlights on hover
- **Interaction:** Tap to open detail view for that metric

**Tank Definitions**

1. **Raw Material Tank**
   - ID: `raw`
   - MQTT Topic: `factory/tanks/raw`
   - Metrics: Level (%), Temperature (°C), Flow In (L/min)
   - Default State: 68% fill

2. **Production Tank**
   - ID: `prod`
   - MQTT Topic: `factory/tanks/prod`
   - Metrics: Level (%), Temperature (°C), Pressure (bar)
   - Default State: 28% fill

3. **Waste Tank**
   - ID: `waste`
   - MQTT Topic: `factory/tanks/waste`
   - Metrics: Level (%), Temperature (°C), pH Level, Flow Out (L/min)
   - Default State: 11% fill

#### Tank Visualization States

**Color-Coded Fill Levels:**
- **Red** (0-14%): Critical low
- **Orange** (15-39%): Warning low
- **Blue** (40-74%): Optimal
- **Teal** (75-100%): Near full

#### User Interactions
- **Drag to Adjust:** Vertical drag on tank to change fill level
- **Metric Inspection:** Tap metric button to view/edit metric
- **Tap Tank:** Single tap (without drag) opens level detail editor
- **Visual Feedback:** Wave animation, splash effects, color changes

---

### 2.4 DEBUG View

#### Purpose
Provide system diagnostics, configuration visibility, and simulated MQTT message log.

#### Layout Components

**Debug Information Grid** (2×2 Layout)
- **Broker:** Connection broker address and port (e.g., "10.8.0.1:1883")
- **MQTT State:** Connection status (styled green for "Connected")
- **Device IP:** Local IP address (e.g., "192.168.43.112")
- **RSSI:** WiFi signal strength (e.g., "-61 dBm", styled blue)

**Message Log Panel**
- **Container:** Dark background (dark blue-gray #080F1C)
- **Dimensions:** Full width, scrollable, monospace font
- **Log Format:** Timestamp + Message type + Message content + Value
- **Message Line Structure:**
  - **Timestamp:** 5-char seconds indicator (e.g., "00043s") in gray
  - **Message Type:** Color-coded classification:
    - Blue "PUB" for MQTT publish messages
    - Green "OK" for successful operations
    - Red "ERR" for errors
  - **Message Content:** Message text with placeholder for dynamic value
- **Animation:** New messages slide in from top, briefly highlighted white, then fade to normal color
- **Scrolling:** Auto-scroll to show newest messages, older messages pushed off top
- **Example Messages:**
  - `PUB factory/machines/cnc01/rpm → 4850`
  - `PUB factory/machines/fill01/flow_rate → 12.4`
  - `NFC badge read · UID A3F7C2 → M.Haegens`
  - `MQTT reconnected · rc=0`

**Action Button**
- **Label:** "Simulate MQTT Publish"
- **Style:** Primary button (blue background)
- **Behavior:** Generates random message and appends to log
- **Message Selection:** Randomly picks from predefined message templates

#### User Interactions
- **View Logs:** Scroll through message history
- **Simulate Messages:** Tap button to add simulated message to log
- **Real-time Monitoring:** Live updates as MQTT messages arrive from device

---

### 2.5 NFC View

#### Purpose
Enable operator identification and authentication via physical or virtual NFC badge system.

#### Layout Components

**NFC Mode Toggle** (Top of View)
- **Layout:** Two-button toggle group
- **Button 1: "Physical Badge"**
  - Left button with NFC icon
  - Inactive state: gray background
  - Active state: blue background with white text
- **Button 2: "Virtual Badge"** (Default)
  - Right button with phone/screen icon
  - Inactive state: gray background
  - Active state: blue background with white text
- **Interaction:** Tap to switch between physical and virtual badge modes

#### PHYSICAL BADGE MODE

**NFC Ring Animation** (Center of Screen)
- **Visual Elements:**
  - Outer circle: Blue gradient background with outer glow
  - Middle ring: Dashed blue border with continuous rotation animation
  - Inner circle: White background with blue NFC icon
  - Icon animation: Scales up and down (pulsing effect)
- **Layout:** Centered in middle of view

**Instructions**
- **Title:** "Tap Badge to Identify Operator"
- **Subtitle:** "MIFARE Classic · Hold badge within 5 cm of device"
- **Styling:** Gray text, centered

**Scan Result Area** (Dynamic)
- **Hidden by Default:** Only shows after scan attempt
- **Content:** Displays operator card (see Virtual Mode card format)
- **Feedback:** Highlights on successful scan

**Simulation Button**
- **Label:** "Simulate Physical Scan"
- **Style:** Primary button
- **Behavior:** Triggers scan with randomly selected operator badge
- **Result:** Displays operator card in result area

#### VIRTUAL BADGE MODE (Default)

**Badge Carousel**
- **Layout:** Full-height carousel with horizontal scroll
- **Card Dimensions:** 390×232px cards in flexbox
- **Spacing:** 22px margin between cards
- **Cards:** One card per operator badge (5 total)

**Individual Badge Card Components**

**Card Visual Design:**
- **Holographic effect:** Subtle gradient overlay on top
- **Role-based styling:** Border and background colors change by role
- **Role Color Scheme:**
  - Operator: Blue (#007AFF)
  - Shift Lead: Green (#00C864)
  - Process Engineer: Orange (#FFA500)
  - Quality Inspector: Red (#FF5050)
  - Production Supervisor: Purple (#B464FF)

**Card Sections:**

1. **Top Section:**
   - **Logo Area (Left):**
     - Small SAP logo in blue square
     - "Digital Manufacturing" heading
     - Plant name (e.g., "Plant BE01")
   - **Employee ID (Right):**
     - ID number (e.g., "EMP-0042")
     - Small monospace font

2. **Main Content Section:**
   - **Avatar (Left):**
     - Circle with initials
     - Color-coded by role
     - Sized 50×50px
   - **Info (Right):**
     - Employee name (bold, large)
     - Role badge/pill (color-coded background with text)
     - Certification list: Small horizontal pills showing certifications
     - Example certifications: "ISO 9001", "Lean", "PLC Certified"

3. **Footer Section:**
   - **NFC UID:** "UID: A3:F7:C2:91" (monospace)
   - **Department:** Employee's department (e.g., "Production")
   - **NFC Icon:** Small NFC icon in top-right

**Operator Badge Definitions**
1. **Michiel Haegens** (Shift Lead)
   - ID: EMP-0042
   - Role: Shift Lead (green)
   - Department: Production
   - UID: A3:F7:C2:91
   - Certifications: ISO 9001, Lean, PLC Certified

2. **Jana Kovacevic** (CNC Operator)
   - ID: EMP-0117
   - Role: CNC Operator (blue)
   - Department: Machining
   - UID: B1:2E:9A:04
   - Certifications: CNC Level 3, Safety

3. **Lars Vermeulen** (Process Engineer)
   - ID: EMP-0089
   - Role: Process Engineer (orange)
   - Department: Engineering
   - UID: C4:88:F1:3D
   - Certifications: Six Sigma GB, HAZOP, ISO 14001

4. **Amira El-Sayed** (Quality Inspector)
   - ID: EMP-0201
   - Role: Quality Inspector (red)
   - Department: Quality
   - UID: D7:A3:55:E9
   - Certifications: ISO 9001 Lead, FMEA, 8D

5. **Pieter van Dijk** (Production Supervisor)
   - ID: EMP-0033
   - Role: Production Supervisor (purple)
   - Department: Operations
   - UID: E2:CF:71:B8
   - Certifications: MES Certified, Lean Black, IFS

**Carousel Navigation**

**Navigation Controls (Below Carousel):**
- **Left Arrow Button:** Navigate to previous badge card
- **Badge Counter:** "1 of 5", "2 of 5", etc. centered between arrows
- **Right Arrow Button:** Navigate to next badge card
- **Styling:** Arrow buttons with icon, gray outlined style

**Carousel Dots (Below Arrow Navigation):**
- **Dot Indicators:** Row of small circles, one per badge
- **Active Dot:** Filled/highlighted
- **Inactive Dots:** Empty circles
- **Interaction:** Tap any dot to jump to that badge

**Card Interaction:**
- **Tap Card:** Selects card and highlights it
- **Drag/Swipe:** Horizontal swipe animates carousel to next/previous card
- **Smooth Animation:** Cards translate smoothly with easing (cubic-bezier timing)

**Selected Badge Display** (Below Carousel)
- **Hidden by Default:** Only appears after badge selection
- **Content:** Operator card (same as physical mode result)
- **Styling:** Green success styling with border

**Activate Badge Button**
- **Label:** "Activate Badge"
- **Style:** Primary button with NFC icon
- **Full Width:** Spans entire button area
- **Behavior:** Confirms operator selection and logs in operator
- **Disabled State:** Grayed out if no badge is selected

#### User Interactions
- **Mode Switch:** Tap Physical/Virtual toggle to change mode
- **Physical Scan:** Tap "Simulate Physical Scan" button to simulate badge tap
- **Virtual Browse:** Scroll/drag carousel to browse available badges
- **Quick Navigation:** Tap dots or arrow buttons for direct navigation
- **Card Selection:** Tap card to select operator
- **Activation:** Tap "Activate Badge" to confirm operator login
- **Real-time Status:** Selected operator updates home screen display

---

### 2.6 DETAIL View (Modal/Overlay)

#### Purpose
Provide deep-dive editing interface for individual metrics with metric-specific visualization and input controls.

#### Navigation Context
- **Breadcrumb Navigation:** Shows "< [Entity Name]" back button and current metric name
- **Accessibility:** Tap back button to return to previous view

#### Layout Structure
- **Header Card:** Shows metric information and current large value
- **Visualization Card:** Metric-specific visualization (varies by metric type)
- **Input Control:** Metric-specific input method (step input, slider, or step generator)
- **Form Sections:** Additional controls like random value generator

#### Detail View Components by Metric Type

**1. SPARKLINE METRICS** (RPM, Flow Rate, Line Speed)
- **Visualization:** SVG sparkline chart showing historical values
- **Data:** Last 40 recorded data points in mini chart
- **Input:** Step input control (increment/decrement buttons with numeric input)
- **Range:** Min to Max defined for metric

**2. ARC GAUGE METRICS** (Spindle Load, Tool Wear, Pressure, OEE)
- **Visualization:** Circular arc gauge showing current value as arc fill
- **Color Coding:** Arc color changes based on thresholds
  - Green: Safe range
  - Orange: Warning range
  - Red: Critical range
- **Input:** Slider control for continuous value adjustment
- **Range:** 0-100% with visual indicator

**3. THERMOMETER METRICS** (Temperature metrics)
- **Visualization:** Vertical bar thermometer with color gradient
- **Color Scale:** Blue (cold) → Red (hot) gradient
- **Input:** Step input or slider
- **Range:** Min to Max temperature defined per metric

**4. PH METRICS** (pH Level)
- **Visualization:** Horizontal pH scale bar
- **Color Gradient:** Red (acidic) → Yellow (neutral) → Blue (basic)
- **Range:** 0-14 pH
- **Input:** Slider control
- **Pointer:** Vertical indicator shows current pH level

**5. BULLET CHART METRICS** (Feed Rate, other ranges)
- **Visualization:** Horizontal progress bar with threshold markers
- **Components:** Progress fill, 60% and 80% threshold marks
- **Input:** Step input control

**6. TANK VISUALIZATION** (Tank Level detail)
- **Visualization:** Tall tank container showing animated fill
- **Elements:** Same as tanks view tank rendering (wave animation, bubbles)
- **Input:** Slider or drag interface
- **Percentage Display:** Shows current fill percentage

#### Detail View Controls

**Step Input Control** (for discrete metrics)
- **Layout:** Horizontal flex with buttons on sides, numeric center
- **Components:**
  - Left Button: "-" (minus) - Decrement by step
  - Center Input: Numeric text field showing current value
  - Right Button: "+" (plus) - Increment by step
- **Behavior:**
  - Buttons: Click to increment/decrement by defined step
  - Text field: Click to select and type exact value
  - Range: Constrained to min/max for metric
  - Update: Value updates in real-time

**Slider Control** (for continuous metrics)
- **Layout:** Horizontal slider with min/max labels below
- **Components:**
  - Slider track: Light gray background (#E3E6EF)
  - Slider thumb: Blue circular handle with shadow
  - Thumb feedback: Enlarges on hover/active
- **Labels:** Min and max values shown below slider
- **Behavior:**
  - Drag thumb to change value
  - Touch-friendly with large hit target
  - Real-time value update
  - Range: Constrained to metric min/max

**Random Value Generator** (for machines view)
- **Layout:** Form section with fields and button
- **Fields (3 columns):**
  - Min value input
  - Max value input
  - Generate button
- **Behavior:**
  - Click "Generate" button
  - Randomly picks value between min and max
  - Sets metric to random value
- **Status Display:**
  - Shows "Generating..." status indicator
  - Green badge "ON" while active
  - Lists refresh rate when enabled

#### Detail View Interactions
- **Value Modification:** Use step input, slider, or generator to change value
- **Real-time Updates:** Chart/visualization updates as value changes
- **History Tracking:** Metric history updates with new values
- **Back Navigation:** Tap breadcrumb to return to parent view
- **Data Persistence:** Values persist until explicitly changed

---

## 3. Cross-View Features

### 3.1 Real-time MQTT Integration
- **Data Source:** All values derived from MQTT topic subscriptions
- **Update Frequency:** Metrics update as messages arrive
- **Topic Structure:** 
  - Machines: `factory/machines/{machine-id}/{metric-key}`
  - Tanks: `factory/tanks/{tank-id}/{metric-key}`
- **Message Format:** Numeric values (JSON or raw numeric payloads)
- **Connection Status:** Pulsing indicator in top-right shell
- **Reconnection:** Auto-reconnect with visual feedback in log

### 3.2 Operator Authentication (NFC System)
- **Integration Points:** 
  - Selectable via NFC view (physical or virtual)
  - Displayed on home screen operator card
  - Used for audit logging and shift tracking
- **Persistence:** Operator selection persists during session
- **Attributes Tracked:** Name, role, department, certifications, UID

### 3.3 Color-Coding System

**Status Colors:**
- **Green (#107E3E):** OK / Running / Safe
- **Orange (#E76500):** Warning / Caution
- **Red (#BB0000):** Error / Stopped / Critical
- **Blue (#0070F2):** Primary / Informational
- **Teal (#045B7A):** Secondary / Special status

**Visual Applications:**
- Metric values: Color matches threshold status
- Gauges: Arc fill color represents status
- KPI cards: Colored accent bar (top border)
- Status dots: Running (green) vs Stopped (red)
- Tank levels: Color-coded fill by level range

### 3.4 History/Trending
- **Sparkline History:** Last 40 data points stored per metric
- **Auto-pruning:** History limited to 40 samples (oldest removed when new added)
- **Rendering:** Mini charts in machine cards, full charts in detail view
- **Data Source:** Updated from MQTT messages and manual adjustments

### 3.5 Responsive Interactions

**Touch Gestures Supported:**
- Tap: Select/navigate/toggle
- Swipe: Carousel navigation (NFC badges)
- Drag: Tank fill adjustment (vertical), machine metrics range selection
- Drag/Swipe: Carousel slide (badges)

**Accessibility:**
- All buttons have minimum 56×56px touch targets (except small UI elements)
- Visual feedback (color change, shadow) on interactive elements
- Status indicators clearly show state (running/stopped, connected/disconnected)

---

## 4. Metric Definitions Reference

### Machine Metrics

| Metric | Machine | Unit | Range | Visualization | Input | Default |
|--------|---------|------|-------|---------------|-------|---------|
| Spindle RPM | CNC-01, CNC-02 | RPM | 0-8000 | Sparkline | Step | 4850 |
| Spindle Load | CNC-01, CNC-02 | % | 0-100 | Arc | Slider | 62% |
| Tool Wear | CNC-01, CNC-02 | % | 0-100 | Arc | Slider | 34% |
| Feed Rate | CNC-01, CNC-02 | mm/min | 0-600 | Tile | Step | 380 |
| Machine Temp | All CNC/Fill/Pack | °C | 15-80 | Tile | Step | 28-41°C |
| Flow Rate | FILL-01 | L/min | 0-25 | Sparkline | Step | 12.4 |
| Pressure | FILL-01, PACK-01 | bar | 0-5 | Arc | Slider | 2.1-1.4 |
| Line Speed | PACK-01 | u/min | 0-80 | Sparkline | Step | 48 |
| OEE | PACK-01 | % | 0-100 | Arc | Slider | 91% |

### Tank Metrics

| Tank | Metric | Unit | Range | Visualization | Input | Default |
|------|--------|------|-------|---------------|-------|---------|
| Raw Material | Level | % | 0-100 | Tank | Slider/Drag | 68% |
| Raw Material | Temp | °C | 5-70 | Tile | Step | 22.4°C |
| Raw Material | Flow In | L/min | 0-20 | Tile | Step | 4.2 |
| Production | Level | % | 0-100 | Tank | Slider/Drag | 28% |
| Production | Temp | °C | 5-70 | Tile | Step | 38.1°C |
| Production | Pressure | bar | 0-5 | Arc | Slider | 1.4 |
| Waste | Level | % | 0-100 | Tank | Slider/Drag | 11% |
| Waste | Temp | °C | 5-70 | Tile | Step | 18.6°C |
| Waste | pH | pH | 0-14 | pH Scale | Slider | 5.2 |
| Waste | Flow Out | L/min | 0-20 | Tile | Step | 1.1 |

---

## 5. Data Models

### Machine Object Structure
```
{
  id: 'cnc01',
  name: 'CNC-01',
  type: 'CNC Machining',
  topic: 'factory/machines/cnc01',
  running: true,
  pulses: 347,
  metrics: {
    rpm: { value: 4850, history: [...] },
    spindleLoad: { value: 62 },
    toolWear: { value: 34 },
    feedRate: { value: 380 },
    machTemp: { value: 41.2 }
  }
}
```

### Tank Object Structure
```
{
  id: 'raw',
  name: 'Raw Material',
  topic: 'factory/tanks/raw',
  metrics: {
    level: { value: 68, history: [...] },
    tankTemp: { value: 22.4 },
    flowIn: { value: 4.2 }
  },
  visualization: {
    fillPercentage: 68,
    wavePhase: 0,
    sloshing: 0,
    isDragging: false
  }
}
```

### Operator Badge Structure
```
{
  id: 'EMP-0042',
  name: 'Michiel Haegens',
  role: 'Shift Lead',
  roleKey: 'lead',
  department: 'Production',
  plant: 'Plant BE01',
  uid: 'A3:F7:C2:91',
  certifications: ['ISO 9001', 'Lean', 'PLC Certified'],
  avatarColor: '#0050BE'
}
```

---

## 6. Visual Design System

### Color Palette
- **Primary Blue:** #0070F2
- **Primary Dark Blue:** #0050BE
- **Success Green:** #107E3E
- **Warning Orange:** #E76500
- **Error Red:** #BB0000
- **Teal:** #045B7A
- **Purple:** #6A28C8
- **Background:** #F2F4FA
- **Surface:** #FFF
- **Border:** #DDE1EA
- **Text Primary:** #1D2D3E
- **Text Secondary:** #4A6080
- **Text Tertiary:** #8090A8

### Typography
- **Font Family:** IBM Plex Sans (UI), IBM Plex Mono (data/metrics)
- **Font Sizes:**
  - XL: 40px (KPI values)
  - Large: 32px (Machine names)
  - Medium: 26px (Card titles)
  - Small: 22px (Button text)
  - XSmall: 18px (Regular body)
  - XXSmall: 15px (Labels/captions)

### Spacing & Sizing
- **Border Radius:** 12px (standard), 8px (small elements)
- **Shadow:** Subtle (0 1px 4px, 0 3px 12px) to Medium (0 4px 20px)
- **Gaps:** 8-14px between elements
- **Touch Targets:** Minimum 56×56px for buttons
- **Tab Height:** 72px with 24px icon, 3px active indicator

### Animations
- **Transitions:** 0.12-0.15s for hover/active states
- **Flash Effect:** KPI buttons flash (0.28s) when updated
- **Wave Effect:** Tank water waves (5s loop, customizable amplitude)
- **Pulsing:** Status dots pulse (2s) when running
- **Carousel:** 0.38s smooth transform with cubic-bezier easing
- **Splash:** Particles rise with 2-2.5s animations

---

## 7. Error Handling & Edge Cases

### Connection States
- **MQTT Connected:** Green pulsing indicator, full functionality
- **MQTT Disconnected:** Gray indicator, cached data displays, reconnecting status shown in log
- **WiFi Weak:** Blue indicator with low RSSI value (<-70 dBm)

### Data Validation
- **Metric Range Enforcement:** All values clamped to min/max before saving
- **Step Rounding:** Values rounded to defined step increments
- **NaN Handling:** Invalid values default to 0 or last valid value

### State Recovery
- **Session Persistence:** Current view and detail context maintained
- **History Preservation:** Metric history retained across view switches
- **Operator Persistence:** Selected operator remains active until manually changed

---

## 8. Performance Considerations

### Rendering Optimization
- **Canvas/SVG Rendering:** Custom SVG for tanks, sparklines, and gauges
- **Animation Frame Loop:** Single requestAnimationFrame loop for all tank physics
- **DOM Updates:** Minimal DOM updates, in-place value replacements
- **History Limits:** Max 40 samples per metric history to limit memory

### MQTT Data Handling
- **Debouncing:** Value updates debounced by network latency
- **Batch Updates:** Related metrics updated together
- **Message Queue:** Handles high-frequency messages gracefully

---

## 9. Accessibility Features

### Color Contrast
- All text meets WCAG AA standards (4.5:1 minimum for body text)
- Status indicated by color + symbol (green dot, red dot, etc.)
- No color-only status indication

### Touch Interaction
- All interactive elements: 56×56px minimum
- Labels always present for buttons/controls
- Visual feedback on press/hover states
- No gesture-only controls (tap + drag alternatives available)

### Information Architecture
- Consistent navigation across views
- Breadcrumb trail for hierarchical navigation
- Clear section headings and labels
- Logical grouping of related information

---

## 10. Future Enhancements (Out of Scope)

1. **Multi-shift scheduling** with operator shift assignments
2. **Alert/notification system** for threshold breaches
3. **Data export** (CSV/JSON) for analytics
4. **Custom dashboards** per role
5. **Remote MQTT broker configuration** UI
6. **Network diagnostic tools** (packet inspection, bandwidth test)
7. **Machine maintenance tracking** and history
8. **Predictive maintenance** using ML on historical data
9. **Multi-language support**
10. **Dark mode theme option**

---

## 11. Conclusion

This SAP Digital Manufacturing MQTT Simulator UI provides a comprehensive, touch-friendly interface for factory floor monitoring and simulation. The five-view navigation model delivers intuitive access to operational data, machine controls, tank management, system diagnostics, and operator authentication. Real-time MQTT integration, responsive interactions, and color-coded status indicators create an effective tool for shop floor visualization and simulation.

---

**Document Version:** 1.0  
**Last Updated:** 2026-04-28  
**Status:** Complete
