# pod-designer Redesign — Plan B: Fixtures, ADRs, and R11 Documentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Validate the Plan A pipeline with three golden fixtures (noodles, plastics-caps, PCB) and fulfil R11 documentation duties: three ADRs, three contract files, updated vault agent stub, updated network README.

**Architecture:** Fixtures are hand-authored JSON files that represent expected intermediate and final artifacts for each demo scenario. ADRs and contracts follow the established vault pattern. No code is generated — this plan is entirely documentation and test artifacts.

**Tech Stack:** JSON (fixtures), Markdown (ADRs, contracts, agent stub).

**Prerequisite:** Plan A must be merged before Plan B begins.

**Spec:** `DM_MQTT/docs/superpowers/specs/2026-07-08-pod-designer-redesign.md`

---

## File map

| Action | Path | Responsibility |
|---|---|---|
| Create | `~/.claude/agents/dm-demo-shared/tests/fixtures/noodles-pod-concept.json` | Noodles concept — mixing WC |
| Create | `~/.claude/agents/dm-demo-shared/tests/fixtures/noodles-pod-dm-mapping.json` | Noodles mapping — mixing WC (custom dashboard expected) |
| Create | `~/.claude/agents/dm-demo-shared/tests/fixtures/plastics-caps-pod-dm-mapping.json` | Plastics-caps — supervisor POD (custom OEE dashboard) |
| Create | `~/.claude/agents/dm-demo-shared/tests/fixtures/pcb-pod-dm-mapping.json` | PCB — SMT WC + inspection WC (custom) |
| Create | `Mitchell/.../decisions/ADR-009-pod-chain-topology.md` | Chain topology decision |
| Create | `Mitchell/.../decisions/ADR-010-pod-standard-first-policy.md` | Standard-first policy |
| Create | `Mitchell/.../decisions/ADR-011-pod-visual-stage-output.md` | Visual stage output format |
| Create | `Mitchell/.../contracts/pod-concept-v1.md` | Pod concept contract |
| Create | `Mitchell/.../contracts/pod-dm-mapping-v1.md` | DM mapping contract |
| Create | `Mitchell/.../contracts/pod-plugin-manifest-v1.md` | Plugin manifest contract |
| Modify | `Mitchell/.../agents/pod-from-image.md` | Status: redesigning → shipped, new architecture |
| Modify | `Mitchell/.../README.md` | Chain diagram update |

Base path for Mitchell: `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network`

---

## Task 1: Create branch

- [ ] **Step 1: Create branch from main (or from plan-a branch if not yet merged)**

```bash
cd /Users/I584748/DM_MQTT
git checkout -b plan-b/pod-designer-fixtures-and-docs
```

Expected: `Switched to a new branch 'plan-b/pod-designer-fixtures-and-docs'`

---

## Task 2: Create fixtures directory

- [ ] **Step 1: Ensure the fixtures directory exists**

```bash
mkdir -p /Users/I584748/.claude/agents/dm-demo-shared/tests/fixtures
```

---

## Task 3: Write noodles-pod-concept.json (mixing WC)

**Files:**
- Create: `~/.claude/agents/dm-demo-shared/tests/fixtures/noodles-pod-concept.json`

This fixture represents what `pod-concept-visualizer` should produce for the noodles demo, mixing work center. The mixing WC has 3 resources with live MQTT metrics (temp, pressure, motor speed), so a custom machine status tab is expected alongside standard WI/DC/NC.

- [ ] **Step 1: Write the fixture**

```json
{
  "pod_concept_version": 1,
  "pod_id": "NOODLES-MIX-WC",
  "kind": "work_center",
  "wc": "WC-MIX",
  "description": "Noodles mixing station POD.",
  "nexus_namespace": "custom.pod2.acme-food-noodles-20260101.120000",
  "tabs": [
    {
      "id": "Tab_Worklist",
      "label": "Worklist",
      "purpose": "Active orders for mixing station",
      "density_target": "standard",
      "widget_slots": [
        {
          "slot_id": "WL_Main",
          "purpose": "Order selection and dispatch status",
          "data_context": "orders filtered by wc=WC-MIX",
          "standard_candidate": "Worklist widget",
          "custom_candidate": null
        }
      ]
    },
    {
      "id": "Tab_WI",
      "label": "Work Instructions",
      "purpose": "Mixing operation step-by-step instructions",
      "density_target": "standard",
      "widget_slots": [
        {
          "slot_id": "WI_Main",
          "purpose": "Guided step navigation for mixing operations",
          "data_context": "routing operations for WC-MIX",
          "standard_candidate": "Work Instructions widget",
          "custom_candidate": null
        }
      ]
    },
    {
      "id": "Tab_DC",
      "label": "Data Collection",
      "purpose": "Record mixing quality parameters",
      "density_target": "standard",
      "widget_slots": [
        {
          "slot_id": "DC_Main",
          "purpose": "Parameter entry for MIX-QC-01 group",
          "data_context": "dc_group MIX-QC-01 attached to WC-MIX",
          "standard_candidate": "Data Collection widget",
          "custom_candidate": null
        }
      ]
    },
    {
      "id": "Tab_NC",
      "label": "Nonconformance",
      "purpose": "Log mixing defects",
      "density_target": "standard",
      "widget_slots": [
        {
          "slot_id": "NC_Main",
          "purpose": "Defect logging for mixing NC codes",
          "data_context": "nc_codes group MIXING-NC",
          "standard_candidate": "Nonconformance widget",
          "custom_candidate": null
        }
      ]
    },
    {
      "id": "Tab_Machine",
      "label": "Machine Status",
      "purpose": "Real-time overview of 3 mixing resources",
      "density_target": "dense",
      "widget_slots": [
        {
          "slot_id": "MS_Dashboard",
          "purpose": "Live MQTT metrics for MIX-01, MIX-02, MIX-03",
          "data_context": "resources[wc=WC-MIX].mqtt.metrics (temp, pressure, motor_rpm)",
          "standard_candidate": null,
          "custom_candidate": "MachineStatusDashboard"
        }
      ]
    }
  ],
  "image_gen_prompt": "Industrial operator touchscreen UI, SAP Fiori style, light mode. Food manufacturing noodle mixing station. Five-tab layout: Worklist, Work Instructions, Data Collection, Nonconformance, Machine Status. Machine Status tab shows 3 mixer machines with real-time gauges for temperature (blue), pressure (green), motor RPM (amber). Dense information display, clear SAP Fiori typography, white background, no clutter. Colour palette: #0070F2 blue, #107E3E green, #E76500 amber, #BB0000 red.",
  "text_wireframe": null,
  "concept_image_path": null,
  "assumptions_add": [
    {
      "id": "A-POD-001",
      "phase": "pod-concept-visualizer",
      "field_path": "pods[id=NOODLES-MIX-WC].tabs[id=Tab_Machine]",
      "value_chosen": "custom machine status tab added",
      "rationale": "3 resources with live MQTT metrics; single-glance status view improves operator flow",
      "confidence": "medium",
      "source": "inferred",
      "resolved": false
    }
  ],
  "gaps_add": []
}
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/agents/dm-demo-shared/tests/fixtures/noodles-pod-concept.json
git commit -m "test(fixtures): add noodles mixing WC pod-concept fixture"
```

---

## Task 4: Write noodles-pod-dm-mapping.json (mixing WC)

**Files:**
- Create: `~/.claude/agents/dm-demo-shared/tests/fixtures/noodles-pod-dm-mapping.json`

Expected: WI/DC/NC/Worklist all standard. Machine Status tab gets a custom plugin (criterion C1: 3 resources, live MQTT).

- [ ] **Step 1: Write the fixture**

```json
{
  "pod_dm_mapping_version": 1,
  "pod_id": "NOODLES-MIX-WC",
  "tab_mappings": [
    {
      "tab_id": "Tab_Worklist",
      "widget_decisions": [
        {
          "slot_id": "WL_Main",
          "verdict": "standard",
          "standard_widget_type": "Worklist",
          "dc_group_refs": [],
          "nc_code_refs": [],
          "plugin_id": null,
          "rationale": "Maps 1:1 to standard Worklist widget.",
          "assumption_id": "A-POD-010"
        }
      ]
    },
    {
      "tab_id": "Tab_WI",
      "widget_decisions": [
        {
          "slot_id": "WI_Main",
          "verdict": "standard",
          "standard_widget_type": "WorkInstructions",
          "dc_group_refs": [],
          "nc_code_refs": [],
          "plugin_id": null,
          "rationale": "Standard WI widget handles guided step navigation.",
          "assumption_id": "A-POD-011"
        }
      ]
    },
    {
      "tab_id": "Tab_DC",
      "widget_decisions": [
        {
          "slot_id": "DC_Main",
          "verdict": "standard",
          "standard_widget_type": "DataCollection",
          "dc_group_refs": ["MIX-QC-01"],
          "nc_code_refs": [],
          "plugin_id": null,
          "rationale": "Standard DC widget covers all MIX-QC-01 parameters.",
          "assumption_id": "A-POD-012"
        }
      ]
    },
    {
      "tab_id": "Tab_NC",
      "widget_decisions": [
        {
          "slot_id": "NC_Main",
          "verdict": "standard",
          "standard_widget_type": "Nonconformance",
          "dc_group_refs": [],
          "nc_code_refs": ["MIX-NC-BURN", "MIX-NC-CLUMP", "MIX-NC-TEMP"],
          "plugin_id": null,
          "rationale": "Standard NC widget handles defect logging.",
          "assumption_id": "A-POD-013"
        }
      ]
    },
    {
      "tab_id": "Tab_Machine",
      "widget_decisions": [
        {
          "slot_id": "MS_Dashboard",
          "verdict": "custom",
          "standard_widget_type": null,
          "dc_group_refs": [],
          "nc_code_refs": [],
          "plugin_id": "MachineStatusDashboard",
          "rationale": "C1: 3 resources (MIX-01, MIX-02, MIX-03) with live MQTT metrics; standard DC widget is entry-only.",
          "assumption_id": "A-POD-014"
        }
      ]
    }
  ],
  "plugins_spec": [
    {
      "plugin_id": "MachineStatusDashboard",
      "slot_ref": "Tab_Machine.MS_Dashboard",
      "behavior_spec": "Displays real-time MQTT metrics (temperature, pressure, motor RPM) for 3 mixing resources as colour-coded gauge cards, updating on each MQTT message.",
      "data_sources": ["resources[wc=WC-MIX].mqtt.metrics"],
      "visual_spec": "3-column card grid, one column per resource. Each card: resource name header, 3 gauge rows (temp blue, pressure green, RPM amber), threshold breach highlighted in red. White background, 1px grey border per card."
    }
  ],
  "assumptions_add": [
    {
      "id": "A-POD-014",
      "phase": "pod-dm-mapper",
      "field_path": "pods[id=NOODLES-MIX-WC].tab_mappings[Tab_Machine].MS_Dashboard.verdict",
      "value_chosen": "custom",
      "rationale": "C1: 3 resources with live MQTT metrics; standard DC widget is entry-only and cannot render a real-time dashboard.",
      "confidence": "medium",
      "source": "inferred",
      "resolved": false
    }
  ],
  "gaps_add": []
}
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/agents/dm-demo-shared/tests/fixtures/noodles-pod-dm-mapping.json
git commit -m "test(fixtures): add noodles mixing WC pod-dm-mapping fixture"
```

---

## Task 5: Write plastics-caps-pod-dm-mapping.json (supervisor POD)

**Files:**
- Create: `~/.claude/agents/dm-demo-shared/tests/fixtures/plastics-caps-pod-dm-mapping.json`

Plastics-caps scenario focus: throughput, oee. WC PODs are all standard. Supervisor POD gets a custom OEE dashboard (criterion C4: multi-machine consolidated view + C1: real-time metrics).

- [ ] **Step 1: Write the fixture**

```json
{
  "pod_dm_mapping_version": 1,
  "pod_id": "PLASTICS-SUPERVISOR",
  "tab_mappings": [
    {
      "tab_id": "Tab_LineStatus",
      "widget_decisions": [
        {
          "slot_id": "LS_Grid",
          "verdict": "standard",
          "standard_widget_type": "LineStatus",
          "dc_group_refs": [],
          "nc_code_refs": [],
          "plugin_id": null,
          "rationale": "Standard Line Status widget handles machine state grid.",
          "assumption_id": "A-POD-020"
        }
      ]
    },
    {
      "tab_id": "Tab_Orders",
      "widget_decisions": [
        {
          "slot_id": "AO_List",
          "verdict": "standard",
          "standard_widget_type": "ActiveOrders",
          "dc_group_refs": [],
          "nc_code_refs": [],
          "plugin_id": null,
          "rationale": "Standard Active Orders widget covers order list and dispatch state.",
          "assumption_id": "A-POD-021"
        }
      ]
    },
    {
      "tab_id": "Tab_OEE",
      "widget_decisions": [
        {
          "slot_id": "OEE_Dashboard",
          "verdict": "custom",
          "standard_widget_type": null,
          "dc_group_refs": [],
          "nc_code_refs": [],
          "plugin_id": "OeeDashboard",
          "rationale": "C4: 4 injection moulding machines need consolidated OEE view; C1: real-time cycle time metrics per resource.",
          "assumption_id": "A-POD-022"
        }
      ]
    }
  ],
  "plugins_spec": [
    {
      "plugin_id": "OeeDashboard",
      "slot_ref": "Tab_OEE.OEE_Dashboard",
      "behavior_spec": "Displays real-time OEE (Availability, Performance, Quality) across all 4 injection moulding resources, with per-machine breakdown and shift-level trend sparklines.",
      "data_sources": ["resources[].mqtt.metrics", "orders[state=active]", "dc_groups[]"],
      "visual_spec": "Top row: 3 large KPI tiles (A/P/Q) for the line as a whole. Below: 4-column machine grid, each showing live cycle time, current order, and last quality check result. Shift sparkline at bottom. Blue/green/amber palette per threshold."
    }
  ],
  "assumptions_add": [
    {
      "id": "A-POD-022",
      "phase": "pod-dm-mapper",
      "field_path": "pods[id=PLASTICS-SUPERVISOR].tab_mappings[Tab_OEE].OEE_Dashboard.verdict",
      "value_chosen": "custom",
      "rationale": "C4: 4 machines need consolidated view; C1: real-time cycle time metrics. Standard OEE widget covers one machine at a time.",
      "confidence": "medium",
      "source": "inferred",
      "resolved": false
    }
  ],
  "gaps_add": []
}
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/agents/dm-demo-shared/tests/fixtures/plastics-caps-pod-dm-mapping.json
git commit -m "test(fixtures): add plastics-caps supervisor pod-dm-mapping fixture"
```

---

## Task 6: Write pcb-pod-dm-mapping.json (SMT + inspection WCs)

**Files:**
- Create: `~/.claude/agents/dm-demo-shared/tests/fixtures/pcb-pod-dm-mapping.json`

PCB scenario focus: quality-rework, operator-ux. SMT WC: custom machine status panel (placement head metrics). Inspection WC: custom visual defect map (C3: hyper-visual, standard widgets can't render a board defect overlay). Rework WC: all standard.

- [ ] **Step 1: Write the fixture**

```json
{
  "pod_dm_mapping_version": 1,
  "pod_id": "PCB-SMT-WC",
  "tab_mappings": [
    {
      "tab_id": "Tab_Worklist",
      "widget_decisions": [
        {
          "slot_id": "WL_Main",
          "verdict": "standard",
          "standard_widget_type": "Worklist",
          "dc_group_refs": [],
          "nc_code_refs": [],
          "plugin_id": null,
          "rationale": "Maps 1:1 to standard Worklist widget.",
          "assumption_id": "A-POD-030"
        }
      ]
    },
    {
      "tab_id": "Tab_Machine",
      "widget_decisions": [
        {
          "slot_id": "MS_PlacementHeads",
          "verdict": "custom",
          "standard_widget_type": null,
          "dc_group_refs": [],
          "nc_code_refs": [],
          "plugin_id": "PlacementHeadMonitor",
          "rationale": "C1: SMT machine has 4 placement heads each with live MQTT metrics (pick rate, error count, nozzle pressure).",
          "assumption_id": "A-POD-031"
        }
      ]
    },
    {
      "tab_id": "Tab_DC",
      "widget_decisions": [
        {
          "slot_id": "DC_Main",
          "verdict": "standard",
          "standard_widget_type": "DataCollection",
          "dc_group_refs": ["SMT-QC-01"],
          "nc_code_refs": [],
          "plugin_id": null,
          "rationale": "Standard DC widget covers solder paste and placement quality parameters.",
          "assumption_id": "A-POD-032"
        }
      ]
    }
  ],
  "plugins_spec": [
    {
      "plugin_id": "PlacementHeadMonitor",
      "slot_ref": "Tab_Machine.MS_PlacementHeads",
      "behavior_spec": "Displays real-time metrics for 4 placement heads (pick rate, error count, nozzle pressure) as a 2x2 grid of status cards with per-head trend line.",
      "data_sources": ["resources[wc=WC-SMT].mqtt.metrics"],
      "visual_spec": "2x2 card grid. Each card: head label (H1-H4), pick rate gauge (green/amber/red by threshold), error count badge, nozzle pressure indicator. Live update on MQTT message. Amber highlight when error count > 0."
    }
  ],
  "assumptions_add": [
    {
      "id": "A-POD-031",
      "phase": "pod-dm-mapper",
      "field_path": "pods[id=PCB-SMT-WC].tab_mappings[Tab_Machine].MS_PlacementHeads.verdict",
      "value_chosen": "custom",
      "rationale": "C1: 4 placement heads with distinct live MQTT metrics; standard widget cannot render per-head real-time breakdown.",
      "confidence": "medium",
      "source": "inferred",
      "resolved": false
    }
  ],
  "gaps_add": []
}
```

- [ ] **Step 2: Commit**

```bash
git add /Users/I584748/.claude/agents/dm-demo-shared/tests/fixtures/pcb-pod-dm-mapping.json
git commit -m "test(fixtures): add PCB SMT WC pod-dm-mapping fixture"
```

---

## Task 7: Write ADR-009 — Chain topology

**Files:**
- Create: `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/decisions/ADR-009-pod-chain-topology.md`

- [ ] **Step 1: Write the ADR**

```markdown
---
type: adr
tags: [agent-network, adr, decision, pod-designer]
status: accepted
date: 2026-07-08
---

# ADR-009: pod-designer chain topology

## Status

Accepted.

## Context

The original pod-from-image skill was a single-shot, showcase-oriented invocation that always
produced 4 custom NEXUS widgets. The agent network needs standard-first, operator-usable PODs
integrated with DemoPlan. A multi-stage chain was required.

## Decision

The pod-designer pipeline consists of four agents:

1. pod-concept-visualizer: tab/slot plan + image-gen prompt (interactive) or text wireframe (auto)
2. pod-dm-mapper: standard-vs-custom verdicts + DM element bindings + plugins_spec[]
3. pod-plugin-builder: NEXUS JS widget generation (conditional, runs only if plugins_spec non-empty)
4. pod-emitter: pod-config.json + optional ZIP + DemoPlan pods[] population

Concept and visual stages are merged (pod-concept-visualizer) because they share all context and
neither makes sense without the other. Pod-plugin-builder is separate from pod-dm-mapper because
deciding widget requirements and writing JavaScript are distinct tasks with different failure modes.

The conditional fork: mapper emits plugins_needed flag. Orchestrator skips pod-plugin-builder when
plugins_needed=false. This keeps the no-plugin path lean for the majority of PODs.

Entry points: pod-designer (canonical), pod-from-image (backward-compatibility shim, calls pod-designer with output_mode=desktop).

## Consequences

- Three interactive gates per POD in interactive mode (concept, mapper verdict, plugin spec)
- Auto mode: gate 1 substitutes text wireframe; gates 2+3 proceed on defaults
- dm-pod-designer loads pod-designer; the fan-out per POD still happens at the dm-pod-designer level
- Three new versioned intermediate artifacts (pod-concept, pod-dm-mapping, pod-plugin-manifest)

## Links

- Spec: `DM_MQTT/docs/superpowers/specs/2026-07-08-pod-designer-redesign.md`
- Rules: [[../../../.claude/agents/dm-demo-shared/pod-standard-first-rules.md]]
- [[ADR-010-pod-standard-first-policy]]
- [[ADR-011-pod-visual-stage-output]]
```

- [ ] **Step 2: Commit**

```bash
git add "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/decisions/ADR-009-pod-chain-topology.md"
git commit -m "docs(adr): ADR-009 pod-designer chain topology"
```

---

## Task 8: Write ADR-010 — Standard-first policy

**Files:**
- Create: `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/decisions/ADR-010-pod-standard-first-policy.md`

- [ ] **Step 1: Write the ADR**

```markdown
---
type: adr
tags: [agent-network, adr, decision, pod-designer]
status: accepted
date: 2026-07-08
---

# ADR-010: POD standard-first policy and custom plugin trigger criteria

## Status

Accepted.

## Context

The original skill always produced 4 custom NEXUS widgets. This created maintenance cost
and obscured what standard SAP DM widgets could do. The redesign needed a principled,
defensible rule set for when custom widgets are justified.

## Decision

Default to standard POD 2.0 widgets. Custom NEXUS plugins are justified only when one
of four criteria is met (see pod-standard-first-rules.md for full definitions):

- C1: Real-time MQTT visualisation (standard DC widget is entry-only, cannot render live data)
- C2: External system integration panel (AGV, MES, sensor map)
- C3: Hyper-visual showcase (scenario.focus includes operator-ux AND concept image shows layout standard widgets cannot produce)
- C4: Multi-resource consolidated view (3+ resources, operator needs simultaneous overview)

Always standard regardless of scenario: Work Instructions, Data Collection, Nonconformance
logging, Worklist, Order info, Certifications.

Every custom verdict is a structured assumption record (confidence: medium, source: inferred)
unless a concept image explicitly confirms the layout.

## Consequences

- Most WC PODs will be standard-only (no NEXUS extension needed)
- Custom plugins remain possible for machine dashboards, AGV panels, OEE consoles
- The mapper stage is the single owner of standard-vs-custom decisions
- Human can override in interactive mode at the Stage 2 gate

## Links

- Rules file: `~/.claude/agents/dm-demo-shared/pod-standard-first-rules.md`
- [[ADR-009-pod-chain-topology]]
```

- [ ] **Step 2: Commit**

```bash
git add "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/decisions/ADR-010-pod-standard-first-policy.md"
git commit -m "docs(adr): ADR-010 pod standard-first policy"
```

---

## Task 9: Write ADR-011 — Visual stage output format

**Files:**
- Create: `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/decisions/ADR-011-pod-visual-stage-output.md`

- [ ] **Step 1: Write the ADR**

```markdown
---
type: adr
tags: [agent-network, adr, decision, pod-designer]
status: accepted
date: 2026-07-08
---

# ADR-011: pod-designer visual stage output format

## Status

Accepted.

## Context

The pipeline needed a way to incorporate visual intent into POD layout decisions.
Options considered: generated image (requires image-gen API), structured HTML mockup,
text wireframe, or human-in-the-loop with external LLM.

## Decision

The visual stage (pod-concept-visualizer) does not generate images itself.

In **interactive mode**: it produces a text-to-image prompt. The human feeds this prompt
to an external LLM (Midjourney, DALL-E, etc.), receives a concept image, and provides the
image path back to the chain. The concept image is then available to pod-dm-mapper for
layout validation and C3 criterion assessment.

In **auto mode**: it produces a structured text wireframe (markdown table of tabs and slots
with density targets). The mapper uses this instead of an image. No external LLM required.
The auto mode path trades image-quality reinforcement for non-blocking execution.

## Rationale

An image-gen API call inside the chain would add latency, cost, and an external dependency.
The human-in-the-loop breakpoint is already required in interactive mode for other review gates.
Delegating image generation to the human's preferred tool (which may produce better results
than a default API call) is the pragmatic choice.

## Consequences

- Interactive mode has a mandatory human handoff between Stage 1 and Stage 2
- Auto mode produces text-based layout specs only; C3 criterion (hyper-visual showcase) is harder
  to trigger in auto mode without explicit scenario.focus = operator-ux
- The concept_image_path field in pod-concept.json is null in auto mode and null until the human
  provides the image in interactive mode

## Links

- [[ADR-009-pod-chain-topology]]
```

- [ ] **Step 2: Commit**

```bash
git add "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/decisions/ADR-011-pod-visual-stage-output.md"
git commit -m "docs(adr): ADR-011 pod visual stage output format"
```

---

## Task 10: Write three contract files

**Files:**
- Create: `Mitchell/.../contracts/pod-concept-v1.md`
- Create: `Mitchell/.../contracts/pod-dm-mapping-v1.md`
- Create: `Mitchell/.../contracts/pod-plugin-manifest-v1.md`

Base path: `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/contracts/`

- [ ] **Step 1: Write pod-concept-v1.md**

```markdown
---
type: contract
tags: [agent-network, contracts, pod-designer]
version: 1
status: v1 ready
---

# PodConcept v1

Intermediate artifact produced by pod-concept-visualizer (Stage 1 of pod-designer).
Consumed by pod-dm-mapper (Stage 2).

## Executable schema

`~/.claude/agents/dm-demo-shared/pod-concept-schema.json` (JSON Schema draft-07)

## Key fields

- `pod_concept_version: 1`
- `pod_id`: uppercase, max 20 chars, ASCII, no spaces
- `kind`: work_center | supervisor | line_monitor
- `description`: max 40 chars, printable ASCII, ends with period
- `nexus_namespace`: matches `^custom\.pod2\.[a-z0-9-]+\.\d{6}$`
- `tabs[]`: one entry per planned tab, each with `widget_slots[]`
- `image_gen_prompt`: non-null in interactive mode, null in auto mode
- `text_wireframe`: non-null in auto mode, null in interactive mode
- `concept_image_path`: null until human provides image (interactive) or always null (auto)

## Ownership

Produced by: pod-concept-visualizer
Consumed by: pod-dm-mapper

## Related

- [[../decisions/ADR-009-pod-chain-topology]]
- [[../decisions/ADR-011-pod-visual-stage-output]]
- Schema: `~/.claude/agents/dm-demo-shared/pod-concept-schema.json`
```

- [ ] **Step 2: Write pod-dm-mapping-v1.md**

```markdown
---
type: contract
tags: [agent-network, contracts, pod-designer]
version: 1
status: v1 ready
---

# PodDmMapping v1

Intermediate artifact produced by pod-dm-mapper (Stage 2 of pod-designer).
Consumed by pod-plugin-builder (Stage 3, conditional) and pod-emitter (Stage 4).

## Executable schema

`~/.claude/agents/dm-demo-shared/pod-dm-mapping-schema.json` (JSON Schema draft-07)

## Key fields

- `pod_dm_mapping_version: 1`
- `tab_mappings[]`: one entry per tab, each with `widget_decisions[]`
- `widget_decisions[].verdict`: "standard" or "custom"
- `widget_decisions[].dc_group_refs[]`: resolving dc_group codes from DemoPlan
- `widget_decisions[].nc_code_refs[]`: resolving nc_code IDs from DemoPlan
- `plugins_spec[]`: empty array for standard-only PODs; non-empty triggers pod-plugin-builder

## Ownership

Produced by: pod-dm-mapper
Consumed by: pod-plugin-builder (if plugins_spec non-empty), pod-emitter

## Related

- [[../decisions/ADR-009-pod-chain-topology]]
- [[../decisions/ADR-010-pod-standard-first-policy]]
- Schema: `~/.claude/agents/dm-demo-shared/pod-dm-mapping-schema.json`
```

- [ ] **Step 3: Write pod-plugin-manifest-v1.md**

```markdown
---
type: contract
tags: [agent-network, contracts, pod-designer]
version: 1
status: v1 ready
---

# PodPluginManifest v1

Intermediate artifact produced by pod-plugin-builder (Stage 3 of pod-designer, conditional).
Consumed by pod-emitter (Stage 4).
Only produced when pod-dm-mapping.plugins_spec is non-empty.

## Executable schema

`~/.claude/agents/dm-demo-shared/pod-plugin-manifest-schema.json` (JSON Schema draft-07)

## Key fields

- `pod_plugin_manifest_version: 1`
- `nexus_namespace`: matches `^custom\.pod2\.[a-z0-9-]+\.\d{6}$`
- `widgets[]`: one entry per generated JS widget file
- `widgets[].status`: "generated" | "failed"
- `extension_json_path`: absolute path to extension.json in /tmp/nexus_ext_<HHmmss>/
- `zip_path`: null until pod-emitter creates the ZIP

## Ownership

Produced by: pod-plugin-builder
Consumed by: pod-emitter

## Related

- [[../decisions/ADR-009-pod-chain-topology]]
- Schema: `~/.claude/agents/dm-demo-shared/pod-plugin-manifest-schema.json`
```

- [ ] **Step 4: Commit all three contracts**

```bash
git add \
  "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/contracts/pod-concept-v1.md" \
  "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/contracts/pod-dm-mapping-v1.md" \
  "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/contracts/pod-plugin-manifest-v1.md"
git commit -m "docs(contracts): add pod-concept, pod-dm-mapping, pod-plugin-manifest v1 contracts"
```

---

## Task 11: Update vault agent stub — pod-from-image.md

**Files:**
- Modify: `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/agents/pod-from-image.md`

- [ ] **Step 1: Replace the file with updated architecture description**

```markdown
---
type: agent
tags: [agent-network, agents, pod-designer, pod-from-image]
status: shipped
---

# pod-designer (formerly pod-from-image)

## Location

Entry point: `~/.claude/skills/pod-designer/skill.md`
Shim alias: `~/.claude/skills/pod-from-image/skill.md` (calls pod-designer with output_mode=desktop)

## Purpose

Produce operator-usable, standard-first POD 2.0 JSON and optional NEXUS extension ZIP for one POD
in a SAP Digital Manufacturing demo. Integrates with DemoPlan (pods[] section).

## Chain

```
pod-concept-visualizer  →  pod-dm-mapper  →  pod-plugin-builder (conditional)  →  pod-emitter
```

Stages live in `~/.claude/skills/pod-designer/`.

## Input modes

- DemoPlan slice + pod_scope (primary network flow, dm-pod-designer caller)
- Image path (standalone showcase, via pod-from-image shim)
- Screenshot path (standalone)
- Free-text description (standalone)

## Standard-first policy

Default to standard POD 2.0 widgets. Custom NEXUS plugins only when one of four criteria is met
(real-time MQTT visualisation, external system panel, hyper-visual showcase, multi-resource overview).
Rules: `~/.claude/agents/dm-demo-shared/pod-standard-first-rules.md`.

## Interactive gates (3)

1. After concept stage: human generates concept image from prompt, feeds path back
2. After mapper stage: human reviews standard-vs-custom verdicts
3. After plugin stage (if plugins needed): human reviews plugin spec before JS generation

## Intermediate artifacts

- `pod-concept.json` (v1) — schema: `pod-concept-schema.json`
- `pod-dm-mapping.json` (v1) — schema: `pod-dm-mapping-schema.json`
- `pod-plugin-manifest.json` (v1, conditional) — schema: `pod-plugin-manifest-schema.json`

## Invoked by

- `dm-pod-designer` (one call per POD, in a Workflow parallel fan-out)
- Manual standalone invocations via pod-from-image shim

## Related

- [[../decisions/ADR-009-pod-chain-topology]]
- [[../decisions/ADR-010-pod-standard-first-policy]]
- [[../decisions/ADR-011-pod-visual-stage-output]]
- [[../contracts/pod-concept-v1]]
- [[../contracts/pod-dm-mapping-v1]]
- [[../contracts/pod-plugin-manifest-v1]]
- [[../decisions/ADR-007-shared-knowledge-pack]]
```

- [ ] **Step 2: Commit**

```bash
git add "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/agents/pod-from-image.md"
git commit -m "docs(vault): update pod-from-image agent stub with redesigned architecture"
```

---

## Task 12: Update network README chain diagram

**Files:**
- Modify: `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/README.md`

- [ ] **Step 1: Find the chain diagram block**

Current content (around line 13):
```
## The chain

```
Production Research  →  Demo Brainstorm  →  Build Demo  →  runnable demo
                                                 ↓ subagents
                                       Asset Sim, pod-from-image
```
```

- [ ] **Step 2: Replace with updated diagram showing pod-designer chain**

```markdown
## The chain

```
Production Research  →  Demo Brainstorm  →  Build Demo  →  runnable demo
                                                 ↓ subagents
                                       Asset Sim, pod-designer
                                                 ↓ pod-designer stages
                              concept-visualizer → dm-mapper → plugin-builder → emitter
```
```

- [ ] **Step 3: Find the Agents section and update the pod-from-image entry**

Current:
```
- [[agents/pod-from-image]]: existing skill; sub-invoked by dm-pod-designer
```

Replace with:
```
- [[agents/pod-from-image]]: redesigned as pod-designer — four-stage standard-first pipeline; sub-invoked by dm-pod-designer
```

- [ ] **Step 4: Commit**

```bash
git add "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/README.md"
git commit -m "docs(vault): update network README with pod-designer chain diagram"
```

---

## Self-review against spec

**Spec section coverage check:**

- [x] Golden fixtures — noodles (Tasks 3+4), plastics-caps (Task 5), PCB (Task 6)
- [x] ADR-009 chain topology — Task 7
- [x] ADR-010 standard-first policy — Task 8
- [x] ADR-011 visual stage output — Task 9
- [x] Contract pod-concept-v1 — Task 10
- [x] Contract pod-dm-mapping-v1 — Task 10
- [x] Contract pod-plugin-manifest-v1 — Task 10
- [x] Vault agent stub updated — Task 11
- [x] Network README updated — Task 12

**Placeholder scan:** No TBDs or incomplete steps found.

**Type consistency:** All `pod_concept_version`, `pod_dm_mapping_version`, `pod_plugin_manifest_version` field names in fixtures match the schema names defined in Plan A Task 3.

**Missing from both plans:** The spec mentions updating `DM_MQTT/README.md` (top-level repo README) once shipped. That belongs in a follow-on commit after both plans are merged — it references the shipped state, not the in-progress redesign.
