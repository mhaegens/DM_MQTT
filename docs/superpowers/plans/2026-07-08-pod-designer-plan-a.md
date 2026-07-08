# pod-designer Redesign — Plan A: Foundation + Chain

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single-shot `pod-from-image` showcase skill with a four-stage standard-first pipeline (`pod-concept-visualizer → pod-dm-mapper → pod-plugin-builder → pod-emitter`) that integrates with DemoPlan and respects R1-R11.

**Architecture:** Four new skill/agent files under `~/.claude/skills/pod-designer/`. `pod-from-image` becomes a thin shim. `dm-pod-designer` loads `pod-designer` instead. Three new versioned JSON artifact schemas in `dm-demo-shared/`. DemoPlan schema patched so `nexus_zip` is nullable.

**Tech Stack:** Markdown skill files (Claude agent instructions), JSON Schema draft-07, Node.js (dm-check CLI rule update only).

**Spec:** `DM_MQTT/docs/superpowers/specs/2026-07-08-pod-designer-redesign.md`

---

## File map

| Action | Path | Responsibility |
|---|---|---|
| Create | `~/.claude/skills/pod-designer/skill.md` | Entry point — mode routing, stage orchestration, input normalisation |
| Create | `~/.claude/skills/pod-designer/pod-concept-visualizer.md` | Stage 1 — concept artifact + image-gen prompt / text wireframe |
| Create | `~/.claude/skills/pod-designer/pod-dm-mapper.md` | Stage 2 — DM element binding + standard-vs-custom verdicts |
| Create | `~/.claude/skills/pod-designer/pod-plugin-builder.md` | Stage 3 (conditional) — NEXUS JS widget generation |
| Create | `~/.claude/skills/pod-designer/pod-emitter.md` | Stage 4 — pod-config.json + ZIP + DemoPlan pods[] population |
| Modify | `~/.claude/skills/pod-from-image/skill.md` | Shim redirect to pod-designer |
| Create | `~/.claude/agents/dm-demo-shared/pod-standard-first-rules.md` | Standard-vs-custom decision rule set |
| Create | `~/.claude/agents/dm-demo-shared/pod-concept-schema.json` | JSON Schema for pod-concept.json (pod_concept_version: 1) |
| Create | `~/.claude/agents/dm-demo-shared/pod-dm-mapping-schema.json` | JSON Schema for pod-dm-mapping.json (pod_dm_mapping_version: 1) |
| Create | `~/.claude/agents/dm-demo-shared/pod-plugin-manifest-schema.json` | JSON Schema for pod-plugin-manifest.json (pod_plugin_manifest_version: 1) |
| Modify | `~/.claude/agents/dm-demo-shared/demoplan-schema.json` | `nexus_zip` string -> string\|null |
| Modify | `~/.claude/agents/dm-demo-shared/checker-rules.md` | DEL_POD_ARTIFACTS_EXIST: exempt null nexus_zip |
| Modify | `~/.claude/agents/dm-pod-designer.md` | Load pod-designer instead of pod-from-image |

---

## Task 1: Create branch

**Files:** none (git only)

- [ ] **Step 1: Create and switch to the feature branch**

```bash
cd /Users/I584748/DM_MQTT
git checkout -b plan-a/pod-from-image-redesign-foundation
```

Expected: `Switched to a new branch 'plan-a/pod-from-image-redesign-foundation'`

---

## Task 2: Write pod-standard-first-rules.md

**Files:**
- Create: `~/.claude/agents/dm-demo-shared/pod-standard-first-rules.md`

This file is loaded by `pod-dm-mapper` at stage 2. It defines which widget slots must use standard POD 2.0 widgets and the four criteria that justify a custom NEXUS plugin.

- [ ] **Step 1: Write the file**

```markdown
# POD Standard-First Rules

Load this before making any standard-vs-custom widget decision.

## Always standard — no custom plugin justified

The following tab types map 1:1 to standard POD 2.0 widgets regardless of scenario focus.
Never generate a NEXUS plugin for these:

- **Work Instructions** — standard WI widget handles step cards, guided navigation, operation sequence
- **Data Collection** — standard DC widget handles parameter entry and group recording
- **Nonconformance logging** — standard NC widget handles defect code selection, quantity, notes
- **Worklist** — standard Worklist widget handles order selection and status
- **Order info display** — standard Order Summary widget
- **Certifications / operator sign-on** — standard Certification widget

## Custom plugin justified — criteria

A custom NEXUS plugin is justified when ANY ONE of the following is true.
Record the matching criterion as `rationale` in the widget_decision.
Every custom verdict must have a corresponding assumption record (confidence: medium unless concept image shows the layout explicitly).

### C1: Real-time MQTT visualisation
The scenario wires live machine metrics (temperature, pressure, OEE, speed, cycle time) AND a
single-glance status view would improve operator flow. The standard DC widget is entry-only;
it cannot render live incoming MQTT data as a dashboard. Trigger: resource has mqtt.metrics[]
with sampling_hz > 0 AND the POD's WC has 2+ resources publishing distinct metrics.

### C2: External system integration panel
The scenario includes operator-visible interaction with an adjacent system:
AGV tracking map, MES queue from an upstream system, warehouse sensor overlay.
No standard POD 2.0 widget covers cross-system panels.

### C3: Hyper-visual showcase
scenario.focus includes "operator-ux" AND the concept image (or text wireframe in auto mode)
shows a layout density or visual composition that standard widgets cannot produce — for example,
a multi-KPI overview card, an animated process flow, a colour-coded machine state grid.
This criterion is intentionally subjective. Justify with one sentence in rationale.

### C4: Multi-resource consolidated view
Three or more resources feed the same WC AND the operator needs all resource states visible
simultaneously. Standard widgets handle one resource context at a time.

## Mapper output format per slot

For every widget slot in every tab, output:

```json
{
  "slot_id": "<string>",
  "verdict": "standard" | "custom",
  "standard_widget_type": "<string or null if custom>",
  "dc_group_refs": ["<code>"],
  "nc_code_refs": ["<code>"],
  "plugin_id": "<string or null if standard>",
  "rationale": "<one sentence>",
  "assumption_id": "<A-POD-NNN>"
}
```

If verdict is "standard", plugin_id is null.
If verdict is "custom", standard_widget_type is null and plugin_id is set.
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/agents/dm-demo-shared/pod-standard-first-rules.md
git commit -m "feat(pod-designer): add standard-first decision rules"
```

---

## Task 3: Write three intermediate artifact JSON Schemas

**Files:**
- Create: `~/.claude/agents/dm-demo-shared/pod-concept-schema.json`
- Create: `~/.claude/agents/dm-demo-shared/pod-dm-mapping-schema.json`
- Create: `~/.claude/agents/dm-demo-shared/pod-plugin-manifest-schema.json`

- [ ] **Step 1: Write pod-concept-schema.json**

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://haegens.be/schemas/pod-concept-v1.json",
  "title": "PodConcept",
  "type": "object",
  "additionalProperties": false,
  "required": ["pod_concept_version","pod_id","kind","wc","description","nexus_namespace","tabs","image_gen_prompt","text_wireframe","concept_image_path","assumptions_add","gaps_add"],
  "properties": {
    "pod_concept_version": {"type": "integer", "const": 1},
    "pod_id":              {"type": "string", "pattern": "^[A-Z0-9_-]{1,20}$"},
    "kind":                {"enum": ["work_center","supervisor","line_monitor"]},
    "wc":                  {"type": ["string","null"]},
    "description":         {"type": "string", "maxLength": 40, "pattern": "^[\\x20-\\x7E]+$"},
    "nexus_namespace":     {"type": "string", "pattern": "^custom\\.pod2\\.[a-z0-9-]+\\.\\d{6}$"},
    "tabs": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["id","label","purpose","density_target","widget_slots"],
        "properties": {
          "id":             {"type": "string"},
          "label":          {"type": "string"},
          "purpose":        {"type": "string"},
          "density_target": {"enum": ["standard","dense","custom"]},
          "widget_slots": {
            "type": "array",
            "items": {
              "type": "object",
              "additionalProperties": false,
              "required": ["slot_id","purpose","data_context","standard_candidate","custom_candidate"],
              "properties": {
                "slot_id":            {"type": "string"},
                "purpose":            {"type": "string"},
                "data_context":       {"type": "string"},
                "standard_candidate": {"type": ["string","null"]},
                "custom_candidate":   {"type": ["string","null"]}
              }
            }
          }
        }
      }
    },
    "image_gen_prompt":    {"type": ["string","null"]},
    "text_wireframe":      {"type": ["string","null"]},
    "concept_image_path":  {"type": ["string","null"]},
    "assumptions_add":     {"type": "array"},
    "gaps_add":            {"type": "array"}
  }
}
```

- [ ] **Step 2: Write pod-dm-mapping-schema.json**

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://haegens.be/schemas/pod-dm-mapping-v1.json",
  "title": "PodDmMapping",
  "type": "object",
  "additionalProperties": false,
  "required": ["pod_dm_mapping_version","pod_id","tab_mappings","plugins_spec","assumptions_add","gaps_add"],
  "properties": {
    "pod_dm_mapping_version": {"type": "integer", "const": 1},
    "pod_id":                 {"type": "string"},
    "tab_mappings": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["tab_id","widget_decisions"],
        "properties": {
          "tab_id": {"type": "string"},
          "widget_decisions": {
            "type": "array",
            "items": {
              "type": "object",
              "additionalProperties": false,
              "required": ["slot_id","verdict","standard_widget_type","dc_group_refs","nc_code_refs","plugin_id","rationale","assumption_id"],
              "properties": {
                "slot_id":              {"type": "string"},
                "verdict":              {"enum": ["standard","custom"]},
                "standard_widget_type": {"type": ["string","null"]},
                "dc_group_refs":        {"type": "array", "items": {"type": "string"}},
                "nc_code_refs":         {"type": "array", "items": {"type": "string"}},
                "plugin_id":            {"type": ["string","null"]},
                "rationale":            {"type": "string"},
                "assumption_id":        {"type": "string"}
              }
            }
          }
        }
      }
    },
    "plugins_spec": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["plugin_id","slot_ref","behavior_spec","data_sources","visual_spec"],
        "properties": {
          "plugin_id":     {"type": "string"},
          "slot_ref":      {"type": "string"},
          "behavior_spec": {"type": "string"},
          "data_sources":  {"type": "array", "items": {"type": "string"}},
          "visual_spec":   {"type": "string"}
        }
      }
    },
    "assumptions_add": {"type": "array"},
    "gaps_add":        {"type": "array"}
  }
}
```

- [ ] **Step 3: Write pod-plugin-manifest-schema.json**

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://haegens.be/schemas/pod-plugin-manifest-v1.json",
  "title": "PodPluginManifest",
  "type": "object",
  "additionalProperties": false,
  "required": ["pod_plugin_manifest_version","pod_id","nexus_namespace","widgets","extension_json_path","zip_path","assumptions_add","gaps_add"],
  "properties": {
    "pod_plugin_manifest_version": {"type": "integer", "const": 1},
    "pod_id":                      {"type": "string"},
    "nexus_namespace":             {"type": "string", "pattern": "^custom\\.pod2\\.[a-z0-9-]+\\.\\d{6}$"},
    "widgets": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["plugin_id","module_path","widget_type","js_file","status"],
        "properties": {
          "plugin_id":   {"type": "string"},
          "module_path": {"type": "string"},
          "widget_type": {"type": "string"},
          "js_file":     {"type": "string"},
          "status":      {"enum": ["generated","failed"]}
        }
      }
    },
    "extension_json_path": {"type": ["string","null"]},
    "zip_path":            {"type": ["string","null"]},
    "assumptions_add":     {"type": "array"},
    "gaps_add":            {"type": "array"}
  }
}
```

- [ ] **Step 4: Commit**

```bash
git add /Users/I584748/.claude/agents/dm-demo-shared/pod-concept-schema.json \
        /Users/I584748/.claude/agents/dm-demo-shared/pod-dm-mapping-schema.json \
        /Users/I584748/.claude/agents/dm-demo-shared/pod-plugin-manifest-schema.json
git commit -m "feat(pod-designer): add three intermediate artifact JSON Schemas (v1)"
```

---

## Task 4: Patch DemoPlan schema — nexus_zip nullable

**Files:**
- Modify: `~/.claude/agents/dm-demo-shared/demoplan-schema.json` (line ~381)

The `pod.artifact_paths.nexus_zip` property must accept `null` for standard-only PODs.

- [ ] **Step 1: Open demoplan-schema.json and locate the pod artifact_paths block**

Find this block (currently around line 381):

```json
"artifact_paths": {
  "type": "object",
  "additionalProperties": false,
  "required": ["pod_json","nexus_zip"],
  "properties": {
    "pod_json": {"type": "string"},
    "nexus_zip": {"type": "string"}
  }
}
```

- [ ] **Step 2: Replace nexus_zip type with string-or-null**

Change to:

```json
"artifact_paths": {
  "type": "object",
  "additionalProperties": false,
  "required": ["pod_json","nexus_zip"],
  "properties": {
    "pod_json":  {"type": "string"},
    "nexus_zip": {"type": ["string","null"], "_changelog": "v1 patch: null when no NEXUS plugins needed"}
  }
}
```

- [ ] **Step 3: Commit**

```bash
git add /Users/I584748/.claude/agents/dm-demo-shared/demoplan-schema.json
git commit -m "fix(demoplan-schema): allow nexus_zip null for standard-only PODs"
```

---

## Task 5: Update checker-rules.md — DEL_POD_ARTIFACTS_EXIST exemption

**Files:**
- Modify: `~/.claude/agents/dm-demo-shared/checker-rules.md`

- [ ] **Step 1: Find the DEL_POD_ARTIFACTS_EXIST row in checker-rules.md**

Current text:

```
| DEL_POD_ARTIFACTS_EXIST | Every `pods[].artifact_paths.pod_json` and `.nexus_zip` point to a file that exists on disk | dm-pod-designer |
```

- [ ] **Step 2: Replace with the updated rule including null exemption**

```
| DEL_POD_ARTIFACTS_EXIST | Every `pods[].artifact_paths.pod_json` points to a file that exists on disk. `pods[].artifact_paths.nexus_zip` must EITHER be null OR point to a file that exists on disk. A null nexus_zip is valid and means no NEXUS extension was needed. | dm-pod-designer |
```

- [ ] **Step 3: Commit**

```bash
git add /Users/I584748/.claude/agents/dm-demo-shared/checker-rules.md
git commit -m "fix(checker-rules): DEL_POD_ARTIFACTS_EXIST exempts null nexus_zip"
```

---

## Task 6: Write pod-concept-visualizer.md (Stage 1)

**Files:**
- Create: `~/.claude/skills/pod-designer/pod-concept-visualizer.md`

- [ ] **Step 1: Create the skills directory and write the file**

```bash
mkdir -p /Users/I584748/.claude/skills/pod-designer
```

Then write `/Users/I584748/.claude/skills/pod-designer/pod-concept-visualizer.md`:

```markdown
---
name: pod-concept-visualizer
description: Stage 1 of pod-designer pipeline. Produces a pod-concept.json artifact containing the tab/widget slot plan, and either an image-gen prompt (interactive) or a text wireframe (auto). Validates against pod-concept-schema.json.
---

# pod-concept-visualizer

## Purpose

Given a DemoPlan slice (or synthesised context), plan what the POD should contain: tabs, slots per tab, data context per slot. Then produce visual guidance for stage 2:
- **interactive mode**: write an image-gen prompt the human will feed to an external LLM to produce a concept image
- **auto mode**: write a structured text wireframe describing the intended layout

## Load first

- `/Users/I584748/.claude/agents/dm-demo-shared/pod-constraints.md`
- `/Users/I584748/.claude/agents/dm-demo-shared/pod-standard-first-rules.md`
- `/Users/I584748/.claude/agents/dm-demo-shared/pod-concept-schema.json`

## Input

```json
{
  "input_path": "<path to DemoPlan JSON or raw context JSON>",
  "pod_scope": {
    "pod_id": "<POD ID>",
    "kind": "work_center | supervisor | line_monitor",
    "wc": "<WC code or null>",
    "nexus_namespace": "<custom.pod2.<slug>.<HHmmss>>"
  },
  "mode": "interactive | auto"
}
```

If `input_path` points to a full DemoPlan, extract the relevant WC, dc_groups, nc_codes, routings, resources, and scenario.focus. If it points to a raw context (standalone invocation), use whatever fields are present.

## What to produce

### 1. Tab plan

Determine the tabs this POD needs. For a WC POD the minimum is:
- Worklist (always)
- Work Instructions (always, unless no routing operations for this WC)
- Data Collection (if any dc_groups attach to this WC or its operations)
- Nonconformance (if scenario.include.nc_codes = true)
- Additional tabs only if the scenario or concept image justifies them (e.g. machine status dashboard, AGV panel)

For a Supervisor POD:
- Line Status (always)
- Active Orders (always)
- OEE / KPIs (always)

For a Line Monitor POD:
- OEE (always)
- Throughput (always)
- Reject Rate (always)

For each tab, list the widget slots with `standard_candidate` and `custom_candidate` (can both be non-null at this stage — the mapper decides in stage 2).

### 2. POD description

Set `description` to a max-40-char, printable ASCII phrase ending with a period. Example: `"Mixing station operator POD."`

### 3a. Interactive mode — image-gen prompt

Write `image_gen_prompt` as a detailed text-to-image prompt. Include:
- "Industrial operator touchscreen UI, SAP Fiori style, light mode"
- The specific product / industry context
- The tab labels and approximate widget layout
- Colour palette: blue #0070F2, green #107E3E, amber #E76500, white background
- "Dense information display, clear typography, no clutter"

Leave `text_wireframe: null` and `concept_image_path: null`.

Then PAUSE. Print:

```
[STAGE 1 GATE — INTERACTIVE]
Image-gen prompt written to: <output_path>
Feed this prompt to your image LLM and save the result locally.
Then resume and provide the concept image path so the chain can continue.
```

Do not proceed until the human provides `concept_image_path`.

### 3b. Auto mode — text wireframe

Write `text_wireframe` as a structured markdown block:

```
## <POD_ID> — text wireframe

### Tab: <label>
- Slot: <slot_id> | <purpose> | data: <data_context> | candidate: <standard_candidate or custom_candidate>

### Tab: <label>
...
```

Leave `image_gen_prompt: null` and `concept_image_path: null`.

## Output

Write the completed `pod-concept.json` to `<demo-folder>/pods/<POD-ID>-concept.json` (network mode) or `/tmp/pod-designer/<POD-ID>-concept.json` (desktop mode).

Return:
```json
{
  "status": "ok",
  "diff": {},
  "assumptions_add": [...],
  "gaps_add": [...],
  "output_path": "<absolute path to pod-concept.json>"
}
```

## Hard constraints

- `description` <= 40 chars, printable ASCII only (pod-constraints.md)
- `nexus_namespace` matches `^custom\.pod2\.[a-z0-9-]+\.\d{6}$`
- `pod_concept_version: 1` at root
- All `slot_id` values unique within the file
- ASCII only in all string fields
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/skills/pod-designer/pod-concept-visualizer.md
git commit -m "feat(pod-designer): add pod-concept-visualizer stage 1"
```

---

## Task 7: Write pod-dm-mapper.md (Stage 2)

**Files:**
- Create: `~/.claude/skills/pod-designer/pod-dm-mapper.md`

- [ ] **Step 1: Write the file**

Write `/Users/I584748/.claude/skills/pod-designer/pod-dm-mapper.md`:

```markdown
---
name: pod-dm-mapper
description: Stage 2 of pod-designer pipeline. Reads pod-concept.json + DemoPlan context + optional concept image. For every widget slot, decides standard vs custom, assigns dc_group_refs and nc_code_refs, and produces pod-dm-mapping.json. Emits plugins_spec[] if any custom decisions were made.
---

# pod-dm-mapper

## Purpose

Wire the abstract concept (tabs + slots) to real DM elements (dc_groups, nc_codes, resources, operations). Decide for each slot whether a standard POD 2.0 widget suffices or a custom NEXUS plugin is required.

## Load first

- `/Users/I584748/.claude/agents/dm-demo-shared/pod-constraints.md`
- `/Users/I584748/.claude/agents/dm-demo-shared/pod-standard-first-rules.md`
- `/Users/I584748/.claude/agents/dm-demo-shared/pod-dm-mapping-schema.json`

## Input

```json
{
  "concept_path": "<path to pod-concept.json>",
  "demoplan_path": "<path to demo-plan.json or null for desktop mode>",
  "concept_image_path": "<path to concept image or null in auto mode>",
  "mode": "interactive | auto"
}
```

## What to produce

For every tab in `pod-concept.json`, for every widget slot in that tab:

1. **Evaluate standard-first criteria** using `pod-standard-first-rules.md`. Default to standard. Upgrade to custom only if a criterion is met.

2. **If standard**, identify the correct POD 2.0 widget type and bind real DM element refs:
   - DC slots: find matching dc_groups from DemoPlan where `dc_group.attach_to.wc_ref == pod.wc` or `operation_ref` matches an operation on this WC's routing
   - NC slots: find matching nc_codes from DemoPlan
   - Worklist slot: no ref needed
   - WI slot: no ref needed (standard WI widget reads operations from the dispatched order)

3. **If custom**, set `verdict: "custom"` and write a `plugins_spec` entry with:
   - `plugin_id`: short PascalCase name e.g. `MachineStatusDashboard`
   - `slot_ref`: `<tab_id>.<slot_id>`
   - `behavior_spec`: one sentence describing what the widget does at runtime
   - `data_sources`: which DemoPlan fields or MQTT topics the widget reads (e.g. `resources[].mqtt.metrics`, `orders[]`)
   - `visual_spec`: layout description (grid, columns, colour coding, real-time update behaviour)

4. **Concept image** (if provided): use the image to validate or override the density_target and visual_spec. If the image clearly shows a custom layout for a slot that would otherwise be standard, upgrade to custom with rationale `"concept image shows custom layout"`.

5. **Assumption records**: every verdict produces one assumption record with `phase: "pod-dm-mapper"`, `field_path: "pods[id=<pod_id>].tab_mappings[<tab_id>].<slot_id>.verdict"`, `confidence: "high"` for standard, `"medium"` for custom.

## Interactive gate

After completing all decisions, print a summary table:

```
[STAGE 2 GATE — INTERACTIVE]
Standard-vs-custom verdicts for <POD_ID>:

Tab          | Slot          | Verdict  | Widget / Plugin        | Rationale
-------------|---------------|----------|------------------------|---------------------------
Tab_Worklist | WL_Main       | standard | Worklist               | Maps 1:1 to standard widget
Tab_DC       | DC_Main       | standard | DataCollection         | DC groups MIX-QC-01 bound
Tab_Machine  | MS_Dashboard  | custom   | MachineStatusDashboard | C1: 3 resources with live MQTT metrics

Plugins to build: 1 (MachineStatusDashboard)
Proceed? (yes / override <slot_id> to standard|custom)
```

Wait for confirmation or override instructions before writing output.

In **auto mode**: skip the gate. Write output immediately.

## Output

Write `pod-dm-mapping.json` to `<demo-folder>/pods/<POD-ID>-dm-mapping.json` (network) or `/tmp/pod-designer/<POD-ID>-dm-mapping.json` (desktop).

Return:
```json
{
  "status": "ok",
  "diff": {},
  "assumptions_add": [...],
  "gaps_add": [...],
  "output_path": "<absolute path to pod-dm-mapping.json>",
  "plugins_needed": true | false
}
```

`plugins_needed` is `true` iff `plugins_spec` array is non-empty.

## Hard constraints

- `pod_dm_mapping_version: 1` at root
- Every dc_group_ref must resolve to a real dc_group code in the DemoPlan (or be empty array if no DemoPlan provided)
- Every nc_code_ref must resolve to a real nc_code id in the DemoPlan (or be empty array if no DemoPlan provided)
- ASCII only in all string fields
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/skills/pod-designer/pod-dm-mapper.md
git commit -m "feat(pod-designer): add pod-dm-mapper stage 2"
```

---

## Task 8: Write pod-plugin-builder.md (Stage 3, conditional)

**Files:**
- Create: `~/.claude/skills/pod-designer/pod-plugin-builder.md`

- [ ] **Step 1: Write the file**

Write `/Users/I584748/.claude/skills/pod-designer/pod-plugin-builder.md`:

```markdown
---
name: pod-plugin-builder
description: Stage 3 of pod-designer pipeline (conditional). Reads pod-dm-mapping.json, generates one .js widget file per plugins_spec[] entry, writes extension.json, produces pod-plugin-manifest.json. Only invoked when plugins_needed = true.
---

# pod-plugin-builder

## Purpose

Given a `pod-dm-mapping.json` with a non-empty `plugins_spec[]`, generate the NEXUS extension widget JavaScript files and `extension.json`. Only runs when the mapper flagged `plugins_needed: true`.

## Load first

- `/Users/I584748/.claude/agents/dm-demo-shared/pod-constraints.md`
- `/Users/I584748/.claude/agents/dm-demo-shared/pod-plugin-manifest-schema.json`

## Input

```json
{
  "mapping_path": "<path to pod-dm-mapping.json>",
  "concept_path":  "<path to pod-concept.json>",
  "concept_image_path": "<path to concept image or null>",
  "nexus_namespace": "<custom.pod2.<slug>.<HHmmss>>",
  "mode": "interactive | auto"
}
```

## Interactive gate

Before writing any JS, print the plugin spec summary:

```
[STAGE 3 GATE — INTERACTIVE]
Plugins to generate for <POD_ID>:

Plugin ID              | Slot              | Behavior
-----------------------|-------------------|------------------------------------------
MachineStatusDashboard | Tab_Machine.MS_Dashboard | Real-time MQTT indicators for 3 resources

Proceed with JS generation? (yes / edit spec)
```

Wait for confirmation. In **auto mode**: skip the gate.

## Widget JS generation rules

For each entry in `plugins_spec[]`:

1. **File path**: `plugins/<plugin_id>.js` inside the work directory `/tmp/nexus_ext_<HHmmss>/`

2. **Scaffold** (mandatory structure — all four lines at top):

```javascript
sap.ui.define([
    "sap/m/library",
    "sap/ui/core/HTML",
    "sap/dm/dme/pod2/widget/Widget"
], function(SapMLibrary, HTML, Widget) {
    "use strict";

    var FlexRendertype = SapMLibrary.FlexRendertype;

    class <PluginId>Widget extends Widget {
        static getIcon()          { return "sap-icon://factory"; }
        static getCategory()      { return "MACH Test <product> <HHmmss>"; }
        static getDefaultConfig() { return { properties: {} }; }

        _createView() {
            var sId = this.getConfig().id;
            var oHTML = new HTML({ preferDOM: true, sanitizeContent: false });
            oHTML.setContent(this._buildHTML());
            var oScroll = new sap.m.ScrollContainer({
                horizontal: false, vertical: true,
                height: "100%", width: "100%",
                content: [oHTML]
            });
            return new sap.m.VBox(sId, {
                height: "100%",
                renderType: FlexRendertype.Bare,
                items: [oScroll]
            });
        }

        _buildHTML() {
            // NO template literals. String concatenation only.
            var css  = "<style>" + "/* styles */" + "</style>";
            var html = "<div>" + "<!-- content -->" + "</div>";
            return css + html;
        }
    }

    return <PluginId>Widget;
});
```

3. **Content rules**:
   - No template literals (backticks) anywhere — concatenate with `+` throughout
   - `var` at module scope, not `const`/`let`
   - `function` callbacks inside loops, not arrow functions
   - Root control ID must be `this.getConfig().id`
   - Use SAP Fiori colours: `#0070F2` blue, `#107E3E` green, `#E76500` amber, `#BB0000` red, `#89919A` secondary text
   - Light mode only (white backgrounds)
   - Use `visual_spec` from plugins_spec to determine layout; use concept image if provided

4. **extension.json** (one file for all widgets in this POD batch):

```json
{
  "widgets": [
    {
      "modulePath":   "custom/pod2/<slug>/<HHmmss>/plugins/<PluginId>",
      "type":         "custom.pod2.<slug>.<HHmmss>.plugins.<PluginId>",
      "name":         "NEXUS <PluginId>",
      "description":  "<product> <purpose>. MACH Test <YYYYMMDD>."
    }
  ],
  "actions": []
}
```

Note: `description` in extension.json must be <= 40 chars per pod-constraints.md.

## Output

Write files to `/tmp/nexus_ext_<HHmmss>/`. Write manifest to `<demo-folder>/pods/<POD-ID>-plugin-manifest.json` (network) or `/tmp/pod-designer/<POD-ID>-plugin-manifest.json` (desktop).

Return:
```json
{
  "status": "ok",
  "diff": {},
  "assumptions_add": [...],
  "gaps_add": [...],
  "output_path": "<absolute path to pod-plugin-manifest.json>",
  "work_dir": "/tmp/nexus_ext_<HHmmss>/"
}
```

## Hard constraints

- `pod_plugin_manifest_version: 1` at root
- Widget category: `MACH Test <product> <HHmmss>` (from pod-constraints.md)
- Extension namespace must match pod's `nexus_namespace`
- No template literals in any generated JS
- `status` per widget: `"generated"` on success, `"failed"` on error (do not abort — log the failure and continue)
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/skills/pod-designer/pod-plugin-builder.md
git commit -m "feat(pod-designer): add pod-plugin-builder stage 3 (conditional)"
```

---

## Task 9: Write pod-emitter.md (Stage 4)

**Files:**
- Create: `~/.claude/skills/pod-designer/pod-emitter.md`

- [ ] **Step 1: Write the file**

Write `/Users/I584748/.claude/skills/pod-designer/pod-emitter.md`:

```markdown
---
name: pod-emitter
description: Stage 4 of pod-designer pipeline. Assembles final pod-config.json from pod-dm-mapping.json + pod-concept.json. If plugin manifest exists, zips the extension and merges it into the shared stepcards-extension.zip. Populates pods[] DemoPlan entry.
---

# pod-emitter

## Purpose

Produce the deployable POD artifacts and populate the `pods[]` entry in DemoPlan. This is the only stage that writes pod-config.json and touches the DemoPlan diff.

## Load first

- `/Users/I584748/.claude/agents/dm-demo-shared/pod-constraints.md`
- `/Users/I584748/.claude/skills/pod-from-image/pod-template.json`

## Input

```json
{
  "concept_path":  "<path to pod-concept.json>",
  "mapping_path":  "<path to pod-dm-mapping.json>",
  "manifest_path": "<path to pod-plugin-manifest.json or null>",
  "demoplan_path": "<path to demo-plan.json or null>",
  "output_mode":   "demo_folder | desktop",
  "demo_folder":   "<absolute path to demo folder or null>",
  "shared_zip_path": "<absolute path to shared stepcards-extension.zip or null>"
}
```

## What to produce

### 1. pod-config.json

Fill `pod-template.json` with values from `pod-concept.json` and `pod-dm-mapping.json`:

- `id` from `pod_concept.pod_id`
- `description` from `pod_concept.description` (already validated <= 40 chars)
- `_version: 1`
- `showFooter: false`
- `createdFromTemplate: "WORK_CENTER"`
- Tabs from `pod_concept.tabs[]`, widgets from `pod_dm_mapping.tab_mappings[]`:
  - Standard slots: use the correct SAP DM widget type (e.g. `sap.dm.dme.pod2.widget.worklist.WorklistPlugin`, `sap.dm.dme.bpd.worklist.WorklistPlugin` — verify against the template)
  - Custom slots: use `<nexus_namespace>.plugins.<plugin_id>` as the widget type
- All widget IDs must be unique within the POD JSON

Write to:
- `<demo-folder>/pods/<POD-ID>.json` (demo_folder mode)
- `~/Desktop/pod-config.json` (desktop mode)

### 2. Extension ZIP (only if manifest_path is non-null)

The extension ZIP is **shared across all PODs in a batch**. To avoid overwriting another POD's widgets:

1. If `shared_zip_path` exists on disk, unzip it to `/tmp/nexus_merge_<HHmmss>/`
2. Copy this POD's widget `.js` files from `/tmp/nexus_ext_<HHmmss>/plugins/` into `/tmp/nexus_merge_<HHmmss>/plugins/`
3. Merge this POD's `extension.json` widgets array into `/tmp/nexus_merge_<HHmmss>/extension.json`
4. Re-zip to `shared_zip_path` (or `<demo-folder>/pods/stepcards-extension.zip` if this is the first POD with plugins)

```bash
cd /tmp/nexus_merge_<HHmmss> && zip -r <shared_zip_path> extension.json plugins/
```

### 3. Deployment instructions (always print)

```
DEPLOY IN THIS ORDER — import order is mandatory:

1. Upload stepcards-extension.zip to Extension Manager
   Wait for namespace <nexus_namespace> to show as registered.
   (Skip this step if no nexus_zip was produced — standard-only POD)

2. Import pod-config.json to Production Operations Designer.

Files:
  <pod_json_path>          -- SAP DM POD import
  <nexus_zip_path or "n/a">  -- Extension Manager upload

POD ID    : <pod_id>
Namespace : <nexus_namespace>
Tabs      : <tab labels joined by " | ">
Plugins   : <plugin IDs joined by ", " or "none (standard-only)">
```

### 4. DemoPlan pods[] entry

Return as part of `diff.pods[]`:

```json
{
  "id": "<pod_id>",
  "kind": "<kind>",
  "wc": "<wc or null>",
  "template": "WORK_CENTER",
  "version": 1,
  "description": "<description>",
  "nexus_namespace": "<nexus_namespace>",
  "extensions": [],
  "plugins": [
    {"plugin_id": "<id>", "slot": "<slot_ref>", "config": {}}
  ],
  "artifact_paths": {
    "pod_json":   "<absolute path>",
    "nexus_zip":  "<absolute path or null>"
  },
  "import_order": "<assigned by dm-pod-designer>",
  "deployment_verdict": null
}
```

`plugins` array contains one entry per custom plugin from the manifest. Empty array for standard-only PODs.

## Return

```json
{
  "status": "ok",
  "diff": {
    "pods": [{ ...the pods[] entry above... }]
  },
  "assumptions_add": [],
  "gaps_add": []
}
```

## Hard constraints

- `showFooter: false` always
- `createdFromTemplate: "WORK_CENTER"` always
- `_version: 1` at root of pod-config.json
- NavigateBack/Start/Hold/Complete actions must NOT have a `properties` field
- NavigateToPage/ShowDialog use `"page"`/`"dialog"` keys (not `"pageId"`/`"dialogId"`)
- No `layoutData` inside `contentAreas`
- No `Clock` widget in toolbar
- All widget IDs unique within the POD JSON
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/skills/pod-designer/pod-emitter.md
git commit -m "feat(pod-designer): add pod-emitter stage 4"
```

---

## Task 10: Write pod-designer/skill.md (entry point)

**Files:**
- Create: `~/.claude/skills/pod-designer/skill.md`

- [ ] **Step 1: Write the file**

Write `/Users/I584748/.claude/skills/pod-designer/skill.md`:

```markdown
---
name: pod-designer
description: Standard-first POD 2.0 pipeline for SAP Digital Manufacturing demos. Accepts DemoPlan slice, image, screenshot, or free-text. Produces operator-usable POD JSON and optional NEXUS extension ZIP through a four-stage chain. Replaces pod-from-image (now an alias shim). Integrates with DemoPlan (pods[] section).
args: "<input-path-or-description> [mode=interactive|auto]"
---

# pod-designer

## What this produces

A deployable POD 2.0 package for one POD in a SAP Digital Manufacturing demo:
- `pod-config.json` — always
- `stepcards-extension.zip` — only when custom NEXUS widgets are needed (standard-first: most PODs won't need this)

When called from the agent network, also populates `pods[]` in DemoPlan.

## Input modes

| Mode | What to pass | Typical caller |
|---|---|---|
| DemoPlan slice | Path to demo-plan.json + pod_scope object | dm-pod-designer (agent network) |
| Image | Path to a product image, printed WI, or process diagram | Manual / pod-from-image shim |
| Screenshot | Path to an existing POD screenshot or competitor UI | Manual |
| Free-text | Plain description of what the POD should do | Manual |

For image, screenshot, and free-text inputs, a minimal DemoPlan context is synthesised automatically. Output goes to `~/Desktop/` (desktop mode).

## Chain

```
[Stage 1] pod-concept-visualizer  →  pod-concept.json + image-gen prompt / text wireframe
[Stage 2] pod-dm-mapper            →  pod-dm-mapping.json  (standard-vs-custom verdicts + DM bindings)
[Stage 3] pod-plugin-builder       →  pod-plugin-manifest.json + .js files  (conditional)
[Stage 4] pod-emitter              →  pod-config.json + optional ZIP + pods[] diff
```

## Execution

### Step 1 — Determine input mode and output mode

- If input is a DemoPlan path + pod_scope: `input_mode = "demoplan"`, `output_mode = "demo_folder"`
- If input is an image/screenshot path or free-text: `input_mode = "raw"`, `output_mode = "desktop"`

For `input_mode = "raw"`, synthesise a minimal context object:
```json
{
  "scenario": {"focus": ["operator-ux"], "include": {"nc_codes": true, "pod_plugins": true}},
  "work_centers": [{"code": "WC-01", "name": "Production", "manual": false}],
  "dc_groups": [], "nc_codes": [], "resources": [], "routings": []
}
```
Extract product/process details from the image or text and populate dc_groups, nc_codes, and resources accordingly.

### Step 2 — Determine mode

Default `mode = "interactive"` unless caller passes `mode=auto` or `meta.mode == "auto"` in the DemoPlan.

### Step 3 — Run Stage 1: pod-concept-visualizer

Load and follow `/Users/I584748/.claude/skills/pod-designer/pod-concept-visualizer.md`.

Pass:
- `input_path`: DemoPlan path or synthesised context path
- `pod_scope`: the POD being designed
- `mode`: as determined above

Record the returned `output_path` as `concept_path`.

**Interactive**: wait for human to provide `concept_image_path` before proceeding.
**Auto**: `concept_image_path = null`. Proceed immediately.

### Step 4 — Run Stage 2: pod-dm-mapper

Load and follow `/Users/I584748/.claude/skills/pod-designer/pod-dm-mapper.md`.

Pass:
- `concept_path`
- `demoplan_path` (or null for desktop mode)
- `concept_image_path` (or null)
- `mode`

**Interactive**: wait for human verdict confirmation before proceeding.

Record the returned `output_path` as `mapping_path` and `plugins_needed` flag.

### Step 5 — Run Stage 3 (conditional): pod-plugin-builder

Only if `plugins_needed = true`.

Load and follow `/Users/I584748/.claude/skills/pod-designer/pod-plugin-builder.md`.

Pass:
- `mapping_path`
- `concept_path`
- `concept_image_path`
- `nexus_namespace` (from pod_scope)
- `mode`

**Interactive**: wait for human plugin spec confirmation before JS is generated.

Record the returned `output_path` as `manifest_path`.

### Step 6 — Run Stage 4: pod-emitter

Load and follow `/Users/I584748/.claude/skills/pod-designer/pod-emitter.md`.

Pass:
- `concept_path`
- `mapping_path`
- `manifest_path` (or null if stage 3 was skipped)
- `demoplan_path`
- `output_mode`
- `demo_folder` (or null for desktop mode)
- `shared_zip_path` (from dm-pod-designer context, or null for desktop mode)

### Step 7 — Return

Return the emitter's return value. The caller (dm-pod-designer) merges `diff.pods[]` into the DemoPlan.

## Load first

- `/Users/I584748/.claude/agents/dm-demo-shared/pod-constraints.md`
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/skills/pod-designer/skill.md
git commit -m "feat(pod-designer): add pod-designer entry point skill"
```

---

## Task 11: Rewrite pod-from-image shim

**Files:**
- Modify: `~/.claude/skills/pod-from-image/skill.md`

- [ ] **Step 1: Read the current file to confirm content before replacing**

Read `/Users/I584748/.claude/skills/pod-from-image/skill.md` (already done in context).

- [ ] **Step 2: Replace the file body with a shim redirect**

The frontmatter stays, the body becomes a one-screen redirect:

```markdown
---
name: pod-from-image
description: DEPRECATED shim. Calls pod-designer with output_mode=desktop. Preserved for backward compatibility with standalone showcase invocations. For agent-network use, invoke pod-designer directly.
args: "<image-path>"
---

# pod-from-image (shim)

This skill is a backward-compatibility alias for `pod-designer`.

## What it does

Calls pod-designer in desktop mode with the provided image as input. Output goes to `~/Desktop/pod-config.json` and (if plugins were needed) `~/Desktop/stepcards-extension.zip`.

## Execution

Load and follow `/Users/I584748/.claude/skills/pod-designer/skill.md`.

Pass:
- `input_path`: the image path provided as the skill argument
- `output_mode`: `"desktop"`
- `mode`: `"interactive"` (standalone invocations are always interactive)

The full four-stage chain runs. For a purely visual concept image demo (showcase mode), expect the mapper to apply criterion C3 (hyper-visual showcase) and generate custom widgets for all tabs.

## Note on original behaviour

The original pod-from-image always produced 4 custom NEXUS widgets regardless of content.
The redesigned chain is standard-first: tabs that map cleanly to standard widgets (DC, NC, WI) will
use standard widgets. A showcase invocation from a product image will typically trigger custom plugins
for the Production Intelligence tab but may use standard DC and WI widgets for the other tabs.
```

- [ ] **Step 3: Commit**

```bash
git add /Users/I584748/.claude/skills/pod-from-image/skill.md
git commit -m "feat(pod-from-image): replace with shim redirect to pod-designer"
```

---

## Task 12: Update dm-pod-designer.md

**Files:**
- Modify: `~/.claude/agents/dm-pod-designer.md`

- [ ] **Step 1: Find the "Load first" section**

Current content:
```
## Load first

- `/Users/I584748/.claude/agents/dm-demo-shared/pod-constraints.md`
- `/Users/I584748/.claude/agents/dm-demo-shared/checker-rules.md`
- `/Users/I584748/.claude/skills/pod-from-image/skill.md`
```

- [ ] **Step 2: Replace pod-from-image reference with pod-designer**

```
## Load first

- `/Users/I584748/.claude/agents/dm-demo-shared/pod-constraints.md`
- `/Users/I584748/.claude/agents/dm-demo-shared/checker-rules.md`
- `/Users/I584748/.claude/skills/pod-designer/skill.md`
```

- [ ] **Step 3: Find the parallel generation section and update the shared_zip_path note**

Current text includes:
> one shared `stepcards-extension.zip` (if `include.pod_plugins`)

Add a note below that paragraph:

```
When using the pod-designer chain, pass `shared_zip_path = "<demo-folder>/pods/stepcards-extension.zip"`
to each parallel pod-emitter invocation. The emitter handles merging widget files into the shared ZIP.
For standard-only PODs, `artifact_paths.nexus_zip` will be null — this is valid and requires no extension step.
```

- [ ] **Step 4: Commit**

```bash
git add /Users/I584748/.claude/agents/dm-pod-designer.md
git commit -m "feat(dm-pod-designer): update to load pod-designer, document shared ZIP merge"
```

---

## Self-review against spec

**Spec section coverage check:**

- [x] Chain topology (four agents, facade + shim) — Tasks 6-11
- [x] Entry points (pod-designer + pod-from-image shim) — Tasks 10-11
- [x] Interactive gates (3 gates, correct stages) — Tasks 6, 7, 8
- [x] Three intermediate artifact schemas — Task 3
- [x] Standard-vs-custom decision rules — Task 2
- [x] DemoPlan schema patch — Task 4
- [x] Checker rule DEL_POD_ARTIFACTS_EXIST — Task 5
- [x] Deliverable set (standard-only vs with plugins) — Task 9 (pod-emitter)
- [x] Shared ZIP merge across PODs — Task 9 + Task 12
- [x] dm-pod-designer caller update — Task 12
- [ ] Golden fixtures — Plan B
- [ ] Documentation / ADRs / contracts — Plan B
- [ ] Vault agent stub update — Plan B

Plan A delivers a working, invocable pipeline. Plan B delivers validation fixtures and R11 documentation.
