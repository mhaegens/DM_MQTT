# Config Schema Reference

The firmware loads its demo scenario from a JSON config file. This document describes every field, the load priority, the capacity limits, and an annotated example.

For the load priority logic, see [architecture.md](architecture.md#config-load-priority).

---

## Load Order

1. SPIFFS flash partition: `/spiffs/sapdm_config.json` (pre-loaded via `idf.py spiffsgen`)
2. SD card: fallback when SPIFFS config is absent
3. Embedded C string: `DEFAULT_CONFIG_JSON` in `main/default_config.c`

The firmware reports the result on boot:

```
I (main): Config: SPIFFS   (or SD_CARD / EMBEDDED)
```

---

## Top-Level Fields

| Field | Type | Notes |
|-------|------|-------|
| `plant_name` | string | Shown in the shell bar subtitle on the device |
| `mqtt_broker` | string | Full WSS URI. Present in the config but **not parsed by config_loader.c**. The broker URI is compile-time in `config.h`. |
| `wifi_ssid` | string | Present in the config but **not parsed**. WiFi credentials are stored in NVS after provisioning. |
| `wifi_password` | string | Same as `wifi_ssid`: present but not read by the loader. |
| `machines` | array | See Machine fields below |
| `tanks` | array | See Tank fields below |
| `people` | array | See Badge fields below |

---

## Machine Fields

```json
{
  "id": "cnc01",
  "name": "CNC-01",
  "type": "CNC Machining",
  "running": true,
  "pulses": 347,
  "metrics": [ ... ]
}
```

| Field | Type | Max length | Notes |
|-------|------|-----------|-------|
| `id` | string | 15 chars | Published in MQTT topics as `factory/machines/{id}/...` |
| `name` | string | 31 chars | Display name on the machines tab |
| `type` | string | 47 chars | Subtitle under the machine name |
| `running` | bool | | Initial running state. Affects sim publishing and the start/stop toggle. |
| `pulses` | int | | Starting pulse count. Increments every 5 sim ticks (~7.5 s) when running. |
| `metrics` | array | Max 8 | See Metric fields below |

---

## Tank Fields

```json
{
  "id": "raw",
  "name": "Raw Material",
  "metrics": [ ... ]
}
```

Same as Machine minus `running` and `pulses`. The first metric (index 0) is treated as the fill level and controls the wave animation height.

| Field | Type | Max length | Notes |
|-------|------|-----------|-------|
| `id` | string | 15 chars | Published in MQTT topics as `factory/tanks/{id}/...` |
| `name` | string | 31 chars | Display name |
| `metrics` | array | Max 8 | See Metric fields below |

---

## Metric Fields

```json
{
  "key": "rpm",
  "label": "Spindle RPM",
  "unit": "RPM",
  "viz": "spark",
  "input": "step",
  "color": "0x0070F2",
  "init_val": 4850,
  "min_val": 0,
  "max_val": 8000,
  "step": 50,
  "thresholds": [
    { "limit": 40, "color": "0x107E3E" },
    { "limit": 70, "color": "0xE76500" },
    { "limit": 100, "color": "0xBB0000" }
  ],
  "sim": {
    "profile": "random_walk",
    "rand_min": 2000,
    "rand_max": 7000
  }
}
```

| Field | Type | Notes |
|-------|------|-------|
| `key` | string | Max 31 chars. Used as the MQTT metric key and as a unique identifier within the machine/tank. |
| `label` | string | Max 47 chars. Display label. |
| `unit` | string | Max 15 chars. Displayed next to the value. |
| `viz` | string | One of: `spark`, `arc`, `thermo`, `ph`, `tank`, `bullet` |
| `input` | string | One of: `step` (buttons), `slider`. Controls the manual override input type in the detail view. |
| `color` | string | Hex string `"0xRRGGBB"`. Used when no threshold rule matches. Default: `0x0070F2` (SAP blue). |
| `init_val` | float | Starting value on boot |
| `min_val` | float | Lower clamp for simulation and manual input |
| `max_val` | float | Upper clamp |
| `step` | float | Step size for step input and random_walk profile |
| `thresholds` | array | Max 3. Evaluated in order, lowest to highest. First matching rule wins. |
| `sim` | object | Simulation profile config. See Sim Object below. |

### Visualization Types

| `viz` | What it shows |
|-------|---------------|
| `spark` | Sparkline chart with current value and unit |
| `arc` | Semicircular arc gauge |
| `thermo` | Vertical thermometer bar |
| `ph` | pH scale (0-14 gradient) |
| `tank` | Animated wave fill (used for tank level) |
| `bullet` | Horizontal bullet chart with threshold bands |

### Threshold Object

```json
{ "limit": 70, "color": "0xE76500" }
```

The `limit` is the upper boundary for the color range. The firmware evaluates thresholds in index order and uses the color from the first threshold where `value <= limit`. If no threshold matches, the last threshold color is used (meaning the value is above all limits).

---

## Sim Object

| Field | Type | Profiles | Notes |
|-------|------|----------|-------|
| `profile` | string | all | One of: `random_walk`, `sine`, `ramp`, `steady` |
| `rand_min` | float | `random_walk` | Lower clamp for random walk. Defaults to `min_val` if absent or 0. |
| `rand_max` | float | `random_walk` | Upper clamp. Defaults to `max_val` if absent or 0. |
| `amplitude` | float | `sine` | Half-amplitude of the sine wave |
| `period_s` | float | `sine` | Period in seconds |
| `offset` | float | `sine` | Center value of the sine wave |

### Simulation Profiles

**`random_walk`**: Each tick adds a random delta in the range `[-step, +step]` and clamps the result to `[rand_min, rand_max]`.

**`sine`**: Value oscillates around `offset` with the given `amplitude` and `period_s`. The formula (from `sim_task.c`):

```
phase_acc += (2 * PI / period_s) * 1.5
value = clamp(offset + amplitude * sin(phase_acc), min_val, max_val)
```

The tick interval is 1.5 seconds. The `* 1.5` factor makes `period_s` work in wall-clock seconds regardless of tick rate.

**`ramp`**: Value increases by `step` each tick. Wraps back to `min_val` when it exceeds `max_val`.

**`steady`**: No change. The `metric_push` call is skipped, so the history sparkline does not grow.

---

## Badge (People) Fields

```json
{
  "id": "EMP-0042",
  "name": "Michiel Haegens",
  "role": "Shift Lead",
  "role_key": "lead",
  "dept": "Production",
  "plant": "Plant BE01",
  "uid": "A3:F7:C2:91",
  "avatar_color": "0x0050BE",
  "certs": ["ISO 9001", "Lean", "PLC Certified"]
}
```

| Field | Type | Max length | Notes |
|-------|------|-----------|-------|
| `id` | string | 15 chars | Employee ID |
| `name` | string | 47 chars | Full name |
| `role` | string | 47 chars | Display role label |
| `role_key` | string | 15 chars | One of: `operator`, `lead`, `engineer`, `supervisor`, `quality`. Controls badge styling. |
| `dept` | string | 31 chars | Department |
| `plant` | string | 31 chars | Plant name, included in the MQTT operator login message |
| `uid` | string | 23 chars | NFC UID in `XX:XX:XX:XX` format. Used to match a physical MIFARE card scan. |
| `avatar_color` | string | | Hex color `"0xRRGGBB"` for the avatar circle background |
| `certs` | string array | Max 6, 31 chars each | Certification labels shown on the badge detail |

---

## Capacity Limits

| Entity | Max count | Defined in |
|--------|-----------|-----------|
| Machines | 10 | `MAX_MACHINES` in `data.h` |
| Tanks | 3 | `MAX_TANKS` |
| Badges | 12 | `MAX_BADGES` |
| Metrics per machine | 8 | `MAX_MACH_METRICS` |
| Metrics per tank | 8 | `MAX_TANK_METRICS` |
| Certs per badge | 6 | `MAX_CERTS` |
| Config file read buffer | 32 KB | `config_loader.c` |

Entities beyond these limits are silently ignored.

---

## Minimal Annotated Example

```json
{
  "plant_name": "Demo Plant",

  "machines": [
    {
      "id": "m01",
      "name": "Machine 01",
      "type": "CNC Machining",
      "running": true,
      "pulses": 0,
      "metrics": [
        {
          "key": "rpm",
          "label": "Spindle RPM",
          "unit": "RPM",
          "viz": "spark",
          "input": "step",
          "color": "0x0070F2",
          "init_val": 4500,
          "min_val": 0,
          "max_val": 8000,
          "step": 100,
          "sim": {
            "profile": "random_walk",
            "rand_min": 3000,
            "rand_max": 6000
          }
        },
        {
          "key": "temp",
          "label": "Temperature",
          "unit": "°C",
          "viz": "thermo",
          "input": "step",
          "init_val": 42.0,
          "min_val": 20,
          "max_val": 90,
          "step": 0.5,
          "sim": {
            "profile": "sine",
            "amplitude": 5.0,
            "period_s": 60,
            "offset": 42.0
          }
        }
      ]
    }
  ],

  "tanks": [
    {
      "id": "t01",
      "name": "Tank 01",
      "metrics": [
        {
          "key": "level",
          "label": "Fill Level",
          "unit": "%",
          "viz": "tank",
          "input": "slider",
          "init_val": 60,
          "min_val": 0,
          "max_val": 100,
          "step": 1,
          "thresholds": [
            { "limit": 20, "color": "0xBB0000" },
            { "limit": 50, "color": "0xE76500" }
          ],
          "sim": { "profile": "random_walk", "rand_min": 40, "rand_max": 80 }
        }
      ]
    }
  ],

  "people": [
    {
      "id": "EMP-0001",
      "name": "Your Name",
      "role": "Operator",
      "role_key": "operator",
      "dept": "Production",
      "plant": "Demo Plant",
      "uid": "00:00:00:00",
      "avatar_color": "0x0070F2",
      "certs": ["Safety Basic"]
    }
  ]
}
```
