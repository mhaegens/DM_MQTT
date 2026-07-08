# pod-designer Redesign Spec

**Date:** 2026-07-08
**Status:** Approved for implementation
**Replaces:** `~/.claude/skills/pod-from-image/skill.md` (showcase-oriented, single-shot)

---

## Why this change

The current `pod-from-image` skill was built to drop jaws on a big stage. Every invocation produces 4 custom NEXUS widgets, demo-only flourishes (live clock, animated quality ring, fake order numbers), and a fixed 4-tab structure. It has no DemoPlan integration, no assumptions trail, no mode switch, and writes to `~/Desktop/` unconditionally.

The agent network needs the opposite: operator-usable, standard-first PODs produced by a chain that integrates with the DemoPlan artifact, honours R1-R11, and supports both unattended auto runs and interactive human-in-the-loop review.

---

## Chain topology

Four agents. `pod-from-image` becomes a shim alias calling `pod-designer`.

```
Input (DemoPlan slice | image | screenshot | free-text)
    |
    v
pod-concept-visualizer
    Out: pod-concept.json (versioned) + image-gen prompt OR text wireframe
    Interactive gate: human generates concept image externally, feeds it back
    Auto: text wireframe substituted, no gate
    |
    v
pod-dm-mapper
    Out: pod-dm-mapping.json (versioned) — standard widget assignments,
         DC group / NC code bindings, plugins_spec[] if custom needed
    Interactive gate: human reviews standard-vs-custom verdicts
    |
    v (only if plugins_spec[] non-empty)
pod-plugin-builder
    Out: pod-plugin-manifest.json (versioned) + .js widget files in /tmp/nexus_ext/
    Interactive gate: human reviews plugin spec before JS is generated
    |
    v
pod-emitter
    Out: pod-config.json + stepcards-extension.zip (only if plugins were built)
         Populates pods[] entry in DemoPlan
```

**Orchestration:** `dm-pod-designer` calls each stage sequentially, waiting for the prior stage's output artifact before calling the next. The Workflow fan-out across multiple WC PODs still happens at the `dm-pod-designer` level — each POD runs its own four-stage chain in parallel.

**Agent I/O shape (R8):** every agent takes `{input_path, mode}`, returns `{status, diff, assumptions_add[], gaps_add[]}`. Caller merges. All stages are idempotent.

---

## Entry points

| Entry point | Behaviour |
|---|---|
| `pod-designer` | Full chain. DemoPlan-aware. Primary network entry point. Writes to `<demo-folder>/pods/`. |
| `pod-from-image` | Alias shim. Calls `pod-designer` without a DemoPlan slice. Concept designer synthesises minimal context. Writes to `~/Desktop/` for backward compatibility. |

The shim sets `output_mode: "desktop"`. The emitter branches on this field.

---

## Interactive gates

Three gates in interactive mode:

1. **After pod-concept-visualizer** — the chain writes the image-gen prompt to disk and pauses. Human takes the prompt to an external LLM (Midjourney, DALL-E, etc.), receives a concept image, and feeds the image path back. Chain resumes with the concept image available to pod-dm-mapper.

2. **After pod-dm-mapper** — the chain surfaces `tab_mappings[].widget_decisions[]` with verdicts and rationales. Human can override any standard/custom verdict before the plugin builder runs.

3. **After pod-plugin-builder** (only if plugins needed) — the chain surfaces the plugin spec before any JS is written. Human reviews behavior_spec and visual_spec per plugin.

In auto mode: gate 1 substitutes a text wireframe; gates 2 and 3 proceed on defaults.

---

## Intermediate artifact schemas

All three are versioned JSON (R1). Schemas published in `~/.claude/agents/dm-demo-shared/`. Prose contracts in `Mitchell/Projects/Agent Network/contracts/`.

### pod-concept.json

```json
{
  "pod_concept_version": 1,
  "pod_id": "MIXING-WC-01",
  "kind": "work_center",
  "wc": "WC-MIX",
  "description": "Mixing station operator POD.",
  "nexus_namespace": "custom.pod2.acme-food-noodles-20260708.143022",
  "tabs": [
    {
      "id": "Tab_Worklist",
      "label": "Worklist",
      "purpose": "Order selection and status at a glance",
      "density_target": "standard",
      "widget_slots": [
        {
          "slot_id": "WL_Main",
          "purpose": "Active orders for this WC",
          "data_context": "orders filtered by wc",
          "standard_candidate": "Worklist widget",
          "custom_candidate": null
        }
      ]
    }
  ],
  "image_gen_prompt": "Industrial operator touchscreen UI, mixing station...",
  "text_wireframe": null,
  "concept_image_path": null,
  "assumptions_add": [],
  "gaps_add": []
}
```

`image_gen_prompt` populated in interactive mode. `text_wireframe` populated in auto mode. `concept_image_path` null until human feeds image back in.

### pod-dm-mapping.json

```json
{
  "pod_dm_mapping_version": 1,
  "pod_id": "MIXING-WC-01",
  "tab_mappings": [
    {
      "tab_id": "Tab_DC",
      "widget_decisions": [
        {
          "slot_id": "DC_Main",
          "verdict": "standard",
          "standard_widget_type": "DataCollection",
          "dc_group_refs": ["MIX-QC-01"],
          "nc_code_refs": [],
          "rationale": "Standard DC widget covers all required parameters.",
          "assumption_id": "A-POD-003"
        }
      ]
    }
  ],
  "plugins_spec": [],
  "assumptions_add": [],
  "gaps_add": []
}
```

`plugins_spec` is empty array when all decisions are standard. Emitter skips plugin builder when empty.

### pod-plugin-manifest.json (conditional)

```json
{
  "pod_plugin_manifest_version": 1,
  "pod_id": "MIXING-WC-01",
  "nexus_namespace": "custom.pod2.acme-food-noodles-20260708.143022",
  "widgets": [
    {
      "plugin_id": "MachineStatusDashboard",
      "module_path": "custom/pod2/acme-food-noodles-20260708/143022/plugins/MachineStatusDashboard",
      "widget_type": "custom.pod2.acme-food-noodles-20260708.143022.plugins.MachineStatusDashboard",
      "js_file": "plugins/MachineStatusDashboard.js",
      "status": "generated"
    }
  ],
  "extension_json_path": "/tmp/nexus_ext/extension.json",
  "zip_path": null,
  "assumptions_add": [],
  "gaps_add": []
}
```

---

## Standard-vs-custom decision rules

Lives in `~/.claude/agents/dm-demo-shared/pod-standard-first-rules.md`.

### Always standard — no plugin justified

- Work Instructions (step cards, guided navigation, operation sequence)
- Data Collection (parameter entry, group recording)
- Nonconformance logging (defect code selection, quantity, notes)
- Worklist (order selection, status)
- Order info display
- Certifications / operator sign-on

### Custom plugin justified when ANY is true

1. **Real-time MQTT visualisation** — scenario wires live machine metrics (temp, pressure, OEE, speed) and a single-glance status view would improve operator flow. Standard DC widget is entry-only; it cannot render live incoming data as a dashboard.

2. **External system integration panel** — scenario includes interaction with an adjacent system visible to the operator (AGV tracking, MES queue, sensor map). No standard widget covers this.

3. **Hyper-visual showcase** — `scenario.focus` includes `operator-ux` AND the concept image shows a layout density or visual style standard widgets cannot produce. Mapper must justify with a one-line rationale.

4. **Multi-resource overview** — three or more resources feed the same WC and the operator needs a consolidated view. Standard widgets handle one resource at a time.

### Mapper output per slot

`verdict: "standard" | "custom"` with `rationale` (one sentence) and `assumption_id` linking to the assumptions trail (R4). Every custom verdict is an assumption with `confidence: medium` unless a concept image explicitly shows the layout.

---

## DemoPlan schema patch

`pods[].artifact_paths.nexus_zip` changes from `string` (required) to `string | null`. This is a DemoPlan v1 patch — minor, no version bump, but requires:

- Changelog entry in `demoplan-schema.json`
- Guard update in `dm-emitter` (treat null as "no extension step")
- Guard update in `dm-consistency-checker` (DEL_POD_ARTIFACTS_EXIST exempts null nexus_zip)

---

## Deliverable set per POD

**Standard-only POD:**
- `<demo-folder>/pods/<POD-ID>.json`
- `pods[].artifact_paths.nexus_zip = null`
- `pods[].plugins = []`
- Deployment: import POD JSON only

**POD with plugins:**
- `<demo-folder>/pods/<POD-ID>.json`
- `<demo-folder>/pods/stepcards-extension.zip` (one ZIP per demo batch, shared)
- `pods[].artifact_paths.nexus_zip` = absolute path to ZIP
- Deployment: extension ZIP first, then POD JSONs in ascending `import_order`

---

## Golden fixtures (three Build Demo demos)

| Demo | Focus | Expected standard-vs-custom mix |
|---|---|---|
| **noodles** | operator-ux, quality-rework | Mixing WC: standard WI + DC + NC + custom machine dashboard (3 resources, live MQTT temp/pressure). Cooking WC: all standard. Packaging WC: all standard. Supervisor: all standard. |
| **plastics-caps** | throughput, oee | All WC PODs: standard. Supervisor: custom OEE dashboard (multi-machine, real-time cycle time). |
| **PCB** | quality-rework, operator-ux | SMT WC: custom machine status panel (placement head metrics). Inspection WC: custom visual defect map. Rework WC: all standard. |

Each fixture produces `pod-concept.json`, `pod-dm-mapping.json`, and (where applicable) `pod-plugin-manifest.json` as test artifacts. The dm-consistency-checker validates the final `pods[]` against the DemoPlan schema.

---

## Documentation duties (R11)

On completion of implementation:

1. Update `Mitchell/Projects/Agent Network/agents/pod-from-image.md` — status `redesigning` during work, `shipped` when done. Replace content with new architecture description.
2. New ADRs in `Mitchell/Projects/Agent Network/decisions/`:
   - `ADR-009-pod-chain-topology.md` — four-agent chain, merge of concept+visual, conditional plugin builder
   - `ADR-010-pod-standard-first-policy.md` — the four custom-plugin trigger criteria
   - `ADR-011-pod-visual-stage-output.md` — image-gen prompt (interactive) vs. text wireframe (auto)
3. New contracts in `Mitchell/Projects/Agent Network/contracts/`:
   - `pod-concept-v1.md`
   - `pod-dm-mapping-v1.md`
   - `pod-plugin-manifest-v1.md`
4. Update `Mitchell/Projects/Agent Network/README.md` chain diagram to show the four-stage pod pipeline.
5. Update `dm-pod-designer.md` to load `pod-designer` instead of `pod-from-image`.

---

## Hard constraints (non-negotiable, from pod-constraints.md)

- `description` max 40 chars, printable ASCII only
- `showFooter: false` always
- `createdFromTemplate: "WORK_CENTER"` always
- `_version: 1` at root
- Namespace: `^custom\.pod2\.[a-z0-9-]+\.\d{6}$`
- All PODs in one batch share same `<HHmmss>` stamp
- Extension ZIP imported before POD JSON(s)
- No template literals in widget JS; `var` at module scope; ES6 class inheritance; `function` callbacks in loops

---

## Files to create or modify

| File | Action |
|---|---|
| `~/.claude/skills/pod-designer/skill.md` | Create — full chain entry point (was pod-from-image) |
| `~/.claude/skills/pod-from-image/skill.md` | Modify — replace body with shim redirect to pod-designer |
| `~/.claude/skills/pod-designer/pod-concept-visualizer.md` | Create — stage 1 agent |
| `~/.claude/skills/pod-designer/pod-dm-mapper.md` | Create — stage 2 agent |
| `~/.claude/skills/pod-designer/pod-plugin-builder.md` | Create — stage 3 agent (conditional) |
| `~/.claude/skills/pod-designer/pod-emitter.md` | Create — stage 4 agent |
| `~/.claude/agents/dm-demo-shared/pod-standard-first-rules.md` | Create — decision rule set |
| `~/.claude/agents/dm-demo-shared/pod-concept-schema.json` | Create — JSON Schema for pod-concept.json |
| `~/.claude/agents/dm-demo-shared/pod-dm-mapping-schema.json` | Create — JSON Schema for pod-dm-mapping.json |
| `~/.claude/agents/dm-demo-shared/pod-plugin-manifest-schema.json` | Create — JSON Schema for pod-plugin-manifest.json |
| `~/.claude/agents/dm-demo-shared/demoplan-schema.json` | Modify — nexus_zip nullable patch |
| `~/.claude/agents/dm-pod-designer.md` | Modify — load pod-designer instead of pod-from-image |
| `~/.claude/agents/dm-demo-shared/tests/fixtures/` | Create — golden fixtures for noodles, plastics-caps, PCB |
| `Mitchell/Projects/Agent Network/agents/pod-from-image.md` | Modify — status redesigning, new architecture description |
| `Mitchell/Projects/Agent Network/decisions/ADR-009*.md` | Create |
| `Mitchell/Projects/Agent Network/decisions/ADR-010*.md` | Create |
| `Mitchell/Projects/Agent Network/decisions/ADR-011*.md` | Create |
| `Mitchell/Projects/Agent Network/contracts/pod-concept-v1.md` | Create |
| `Mitchell/Projects/Agent Network/contracts/pod-dm-mapping-v1.md` | Create |
| `Mitchell/Projects/Agent Network/contracts/pod-plugin-manifest-v1.md` | Create |
| `Mitchell/Projects/Agent Network/README.md` | Modify — chain diagram update |

---

## Verification

1. Run the full chain on the noodles fixture in auto mode — confirm it produces a standard-only POD JSON for the cooking WC and a POD JSON + plugin for the mixing WC.
2. Run the full chain on the plastics-caps fixture in auto mode — confirm all WC PODs are standard and the supervisor POD triggers a plugin build.
3. Run dm-consistency-checker against all three fixture demo-plan.json files — confirm zero violations.
4. In interactive mode, run noodles mixing WC — confirm the chain pauses at all three gates in the right order.
5. Invoke `pod-from-image` shim with a product image (existing showcase path) — confirm it calls pod-designer, writes to `~/Desktop/`, and produces a functional POD JSON.
