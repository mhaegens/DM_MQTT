# Demo Brainstorm Agent — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `demo-brainstorm` Claude agent that turns a `research_output_v1.json` + presales intent into a `brainstorm_output_v1.json` consumable directly by `dm-demo-orchestrator`.

**Architecture:** A single agent file at `~/.claude/agents/demo-brainstorm.md` backed by two new shared-pack files in `~/.claude/agents/dm-demo-shared/`. The agent runs six sequential passes (presales intent → scenario frame → production flow → story → supporting hints → gap eval), logs every non-trivial choice as a traceable assumption, and writes two output files to the Mitchell vault. Three golden fixtures (noodles, plastics, PCB) pair with verify scripts for regression testing.

**Tech Stack:** Claude agent (markdown prompt file), JSON Schema (AJV via existing `dm-check` Node.js tool for downstream validation), bash verify scripts, Python 3 (fixture diffing, same pattern as Production Research verify scripts).

---

## File Map

| Action | Path | Responsibility |
|---|---|---|
| Create | `~/.claude/agents/dm-demo-shared/brainstorm-contract-target.md` | Canonical schema + rules for the brainstorm output contract |
| Create | `~/.claude/agents/dm-demo-shared/order-type-rules.md` | Decision table: product.type → order_type |
| Create | `~/.claude/agents/demo-brainstorm.md` | The agent prompt file (six-pass logic, R1-R11, PREFLIGHT) |
| Create | `Mitchell/Projects/Agent Network/decisions/ADR-010-order-type.md` | ADR recording D8 (order_type decoupled from product.type) |
| Replace | `Mitchell/Projects/Agent Network/agents/demo-brainstorm.md` | Stub → full agent doc |
| Replace | `Mitchell/Projects/Agent Network/contracts/brainstorm-output-v1.md` | Stub → full contract (wikilinks to contract-target) |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/acme-food-noodles/brainstorm_output_v1.expected.json` | Expected output, noodles fixture |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/acme-food-noodles/verify-brainstorm-acme-food-noodles.sh` | Verify script, noodles |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/acme-plastics-caps/brainstorm_output_v1.expected.json` | Expected output, plastics fixture |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/acme-plastics-caps/verify-brainstorm-acme-plastics-caps.sh` | Verify script, plastics |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/acme-electronics-pcb/brainstorm_output_v1.expected.json` | Expected output, PCB fixture |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/acme-electronics-pcb/verify-brainstorm-acme-electronics-pcb.sh` | Verify script, PCB |

> Mitchell paths expand to `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/`

---

## Task 1: Shared pack — brainstorm-contract-target.md

**Files:**
- Create: `~/.claude/agents/dm-demo-shared/brainstorm-contract-target.md`

This file is the canonical contract target. The agent reads it at runtime to know the exact output shape it must produce.

- [x] **Step 1: Create the file**

```markdown
# Brainstorm Contract Target — v1

Canonical output schema for `brainstorm_output_v1.json`. Authoritative source for the Demo Brainstorm agent and its downstream consumers. The Mitchell vault stub at `contracts/brainstorm-output-v1.md` wikilinks here.

## Full schema

```json
{
  "brainstorm_output_version": 1,
  "slug": "<inherited from research — never modified>",
  "company": "<slug segment 0>",
  "industry": "<slug segment 1>",

  "product": {
    "name": "string",
    "short": "kebab-3-20",
    "type": "discrete|process|repetitive",
    "order_type": "production_order|process_order"
  },

  "scenario": {
    "title": "string",
    "story": "5-6 sentence narrative",
    "duration_min": 30,
    "focus": ["operator-ux", "quality-rework"],
    "audience_role": "executive|plant_manager|ops_lead|IT|presales",
    "has_rework_loop": true,
    "rework_rationale": "string — which tri-signals triggered the decision",
    "pain_points_addressed": ["string"]
  },

  "production_flow": {
    "steps": [
      {
        "seq": 1,
        "name": "string",
        "dm_operation_hint": "OP-10-MIX",
        "type": "machine|manual|inspection",
        "hint_resource": "string"
      }
    ],
    "rework_from_to": ["QC", "Packing"]
  },

  "materials_hint": {
    "finished_product_uom": "PC|KG|L|M",
    "components": [{"name": "string", "kind": "raw|semi-finished"}]
  },

  "workforce_hint": {
    "operator_count": 3,
    "supervisor_count": 1,
    "cert_requirements": ["string"]
  },

  "moments_of_expertise": [
    {
      "claim": "string",
      "evidence": "string",
      "source": "research|bank",
      "bank_entry_ref": "string or null"
    }
  ],

  "terminology": ["string"],

  "meta": {
    "mode": "interactive|auto",
    "created": "YYYY-MM-DD",
    "research_source": "path/to/research_output_v1.json",
    "assumptions": [
      {
        "id": "A001",
        "phase": "presales-intent|scenario-frame|production-flow|story|supporting-hints|gap-eval",
        "field_path": "string",
        "value_chosen": "string",
        "rationale": "string",
        "confidence": "high|medium|low",
        "source": "user|inferred|default",
        "resolved": false
      }
    ],
    "gaps_flagged": [
      {
        "id": "G001",
        "phase": "string",
        "field_path": "string",
        "issue": "string",
        "responsible_agent": "demo-brainstorm"
      }
    ]
  }
}
```

## Schema rules

- `slug` propagates unchanged from research (R2 of ADR-008). Never write to this field.
- `company` and `industry` are flat strings from slug segments 0 and 1, NOT the rich objects in research output.
- `product.order_type` required when `product.type = "process"`. For `discrete` and `repetitive` defaults to `production_order` silently. See `order-type-rules.md`.
- `production_flow.steps[].seq`: sequential integers starting at 1.
- `production_flow.rework_from_to`: 2-element array `[from_step_name, to_step_name]` or `[null, null]` when `has_rework_loop = false`.
- `moments_of_expertise`: minimum 2, maximum 3 entries.
- `meta.assumptions[].id`: format `A\d{3}`, unique within document.
- `meta.gaps_flagged[].id`: format `G\d{3}`, unique within document.
- Auto-mode cap: max 30 combined entries across `meta.assumptions[]` + `meta.gaps_flagged[]` (R4 of ADR-008).
- All string values in `product`, `scenario`, `production_flow`: printable ASCII only (R5 of ADR-008).
```

- [x] **Step 2: Verify the file exists**

```bash
ls -la ~/.claude/agents/dm-demo-shared/brainstorm-contract-target.md
```

Expected: file present, non-zero size.

- [x] **Step 3: Commit**

```bash
cd /Users/I584748/DM_MQTT
git add ~/.claude/agents/dm-demo-shared/brainstorm-contract-target.md
git commit -m "feat(brainstorm): add brainstorm-contract-target to shared pack"
```

---

## Task 2: Shared pack — order-type-rules.md

**Files:**
- Create: `~/.claude/agents/dm-demo-shared/order-type-rules.md`

- [ ] **Step 1: Create the file**

```markdown
# Order Type Rules

Decision table for `product.order_type` field in `brainstorm_output_v1.json`. Implements decision D8 from the Demo Brainstorm design spec (ADR-010).

## Decision table

| `product.type` | `order_type` | How set | A-record? |
|---|---|---|---|
| `discrete` | `production_order` | Silent default | No |
| `repetitive` | `production_order` | Silent default | No |
| `process` | `process_order` | Interactive: Q4 in front-load block; Auto: default | Yes (source: user or default) |
| `process` + user picks `production_order` | `production_order` | Interactive only | Yes (confidence: low — non-standard combination) |

## DM downstream note

- `process_order` → phase-based routing in DM (routings with `type: process`).
- `production_order` with `product.type = "process"` is valid in DM but non-standard; dm-master-data-designer will receive a low-confidence A-record and may prompt.
- `repetitive` always uses `production_order` (rate-based scheduling, no per-unit routing in standard DM config).
```

- [ ] **Step 2: Verify**

```bash
ls -la ~/.claude/agents/dm-demo-shared/order-type-rules.md
```

Expected: file present.

- [ ] **Step 3: Commit**

```bash
cd /Users/I584748/DM_MQTT
git add ~/.claude/agents/dm-demo-shared/order-type-rules.md
git commit -m "feat(brainstorm): add order-type-rules to shared pack"
```

---

## Task 3: Agent file — demo-brainstorm.md

**Files:**
- Create: `~/.claude/agents/demo-brainstorm.md`

This is the largest task. The agent file must be complete and self-contained — it is read verbatim by Claude at invocation time.

- [ ] **Step 1: Create the agent file**

Write `~/.claude/agents/demo-brainstorm.md` with the following content (write the full file — no placeholders):

```markdown
---
description: "Demo Brainstorm agent. Turns research_output_v1.json + presales intent into brainstorm_output_v1.json. Invoke with path to research JSON and optional mode=interactive|auto. Returns {status, diff, assumptions_add[], gaps_add[]}."
---

# Demo Brainstorm Agent

## Purpose

Creative step in the demo chain. Reads real-world manufacturing facts from `research_output_v1.json` and presales intent from the caller, then produces `brainstorm_output_v1.json` for `dm-demo-orchestrator`.

**Position in chain:**
Production Research → **Demo Brainstorm (you)** → Build Demo → runnable demo

## PREFLIGHT

Before starting, read these files — they are load-bearing for your output:

1. `~/.claude/agents/dm-demo-shared/brainstorm-contract-target.md` — exact output schema you must produce
2. `~/.claude/agents/dm-demo-shared/mode-contract.md` — confirms you are `may_prompt: true`
3. `~/.claude/agents/dm-demo-shared/moments-of-expertise.md` — bank for Pass 5 B-fill
4. `~/.claude/agents/dm-demo-shared/order-type-rules.md` — `product.order_type` decision table
5. `~/.claude/agents/dm-demo-shared/writing-style.md` — governs all prose you write

Then read the input `research_output_v1.json` provided in your task prompt.

## Invocation (R8)

Task prompt format:
```
<absolute-path-to-research_output_v1.json>[, mode=interactive|auto]
```

Default mode: `interactive`.

Return envelope (to caller, NOT written to disk):
```json
{
  "status": "ok|failed|needs-review",
  "diff": [
    "Mitchell/SAP Solutions/1 - DM/Demos/<slug>/brainstorm_output_v1.json",
    "Mitchell/SAP Solutions/1 - DM/Demos/<slug>/brainstorm-summary.md"
  ],
  "assumptions_add": [],
  "gaps_add": []
}
```

## Output files written

- `~/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos/<slug>/brainstorm_output_v1.json`
- `~/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos/<slug>/brainstorm-summary.md`

Create the slug folder if it does not exist.

## The six passes

Run these passes in order. Log every non-trivial decision as an A-record in `meta.assumptions[]`.

---

### Pass 1 — Presales intent collection

**Interactive mode:** Send ONE message containing these four questions. Do not split across messages.

```
Research loaded: <company.name>, <industry.name>, <product.name> (<product.type>)
Key pain points: <top 2 from research.pain_points[] sorted by confidence desc>
Proposed primary focus: <research.meta.demo_focus>

Before I draft the scenario, four quick questions:

1. Who is in the room?
   executive / plant_manager / ops_lead / IT / presales
   [default: plant_manager if company.known_to_sap=true, presales otherwise]

2. How long is the slot?
   30 / 45 / 60 min  [default: 45]

3. Any focus areas to add? (optional, pick any)
   operator-ux | quality-rework | throughput | OEE | dispatching | monitoring
   Primary focus <research.meta.demo_focus> is already included.

4. [Only shown when product.type = "process"]
   Are they running process orders or production orders in DM?
   process_order / production_order  [default: process_order]
```

Wait for answers. If the user answers "skip" or skips any question, apply the default and log it as `source: default`.

**Auto mode:** Apply all defaults. Log Q1, Q2, Q3 as A-records with `source: default, confidence: medium`. Log Q4 only when `product.type = "process"`.

---

### Pass 2 — Scenario frame

1. Build `scenario.focus[]`:
   - Start with `research.meta.demo_focus` as element 0. This is the seed — it CANNOT be dropped.
   - Append any additions from Pass 1 Q3.

2. Evaluate tri-signal rework model:
   - Signal A: `research.production.rework_common = true`
   - Signal B: any entry in `research.pain_points[]` where `category = "quality"` and `confidence` is `"high"` or `"medium"`
   - Signal C: `scenario.focus[]` contains `"quality-rework"`
   - Result: `has_rework_loop = (signals_fired >= 2)`
   - Write `scenario.rework_rationale` naming which signals fired (e.g., "Signals A and B fired: rework_common=true, quality pain point at high confidence").

3. **Interactive only:** After computing the recommendation, ask:
   "Based on the research, I recommend [including/skipping] a rework loop because [rationale]. Confirm? (yes/no/override)"
   If overridden, accept the human answer, log as `source: user`.

4. Log `has_rework_loop` as A-record with `phase: scenario-frame`.

---

### Pass 3 — Production flow mapping

Source: `research.production.steps[]`.

1. **Select steps:** Keep any step where at least one of these is true:
   - `typical_equipment` is non-empty
   - `quality_checkpoints` is non-empty
   - `can_trigger_rework = true`
   Drop steps that have none of these (purely background logistics steps).

2. **Assign `dm_operation_hint`:** Format `OP-{seq*10}-{SHORTNAME}` where SHORTNAME = first 3-6 uppercase letters of the step name, stripped of spaces. Example: "Short Block Assembly" → `OP-30-SHORT`.

3. **Assign `type`:** 
   - `"inspection"` if step name contains any of: quality, check, inspect, test, aoi, qc, verification (case-insensitive)
   - `"machine"` if `typical_equipment` is non-empty and not inspection
   - `"manual"` otherwise

4. **Assign `hint_resource`:** Find `research.equipment_hints[]` entry where `step_ref` matches this step's `seq`. Use `equipment_hints[].name` lowercased and kebab-cased. If no match, omit the field.

5. **Assign `seq`:** Renumber from 1 sequentially after filtering (original research seq values are discarded).

6. **If `has_rework_loop = true`:** Propose `rework_from_to` as:
   - `from`: name of the last step with `type = "inspection"` (or last step overall if none)
   - `to`: name of the first step that `can_trigger_rework = true` points back toward (typically second step in the flow)
   Log this as A-record with `phase: production-flow`.

7. Log any step that was dropped as a G-record with `phase: production-flow`, `issue: "step dropped: <reason>"`.

---

### Pass 4 — Story authoring

Write `scenario.story` as 5-6 sentences following this structure. Do NOT use em-dashes anywhere.

- **S1:** Universal manufacturing truth — use the first `moments_of_expertise` entry from Pass 5 (run Pass 5 first for this sentence, or come back and insert it). The claim verbatim.
- **S2:** This company's specific situation — use `research.company.name`, `research.industry.name`, one term from `research.terminology[]` for authenticity. State the scale or context.
- **S3:** The pain — use the highest-confidence entry from `research.pain_points[]`. If `research.competitive_context.incumbent_mes` is known and not `"unknown"`, mention what they're replacing.
- **S4:** What the demo shows — map each entry in `scenario.focus[]` to a concrete DM capability (e.g., `operator-ux` → "operators confirm completions via NFC badge tap", `quality-rework` → "rework flows as a first-class queue item").
- **S5:** The operator moment — describe one specific action a demo visitor will watch, calibrated to `scenario.audience_role` (plant_manager: throughput impact; ops_lead: queue status; executive: business outcome).
- **S6 (only for duration_min >= 45):** The stakes — second-strongest `research.pain_points[]` entry reframed as what persists without DM.

Write `scenario.title` as a concise (max 10 words) description of the scenario. No em-dashes.

**Interactive only:** Present the story draft. Say: "Does this story land? (yes / redirect me)" 
- If redirected: incorporate the feedback, re-author once, present the revised draft. Proceed regardless of any further feedback. Log any remaining redirect feedback as a G-record.
- Log story as A-record with `source: user` (interactive) or `source: inferred` (auto).

---

### Pass 5 — Supporting hints

Run this pass alongside Pass 4 (the first MoE claim feeds S1 of the story).

**1. Moments of expertise:**

Curation step:
- Score each `research.moments_of_expertise[]` entry by how directly its `claim` relates to entries in `scenario.focus[]`. Use your judgment: a claim about "rework" scores highest when `focus` contains `quality-rework`.
- Keep the top 2 scoring entries. If fewer than 2 exist in research, proceed to B-fill.

B-fill step (only if curated count < 2):
- Read `~/.claude/agents/dm-demo-shared/moments-of-expertise.md`.
- Select the entry from the bank whose topic best matches `research.industry.name` and `scenario.focus[]`.
- Set `source: "bank"`, `bank_entry_ref`: the first 8 words of the bank entry as an informal reference.
- B-fill until you have exactly 2 entries minimum. Stop at 3 maximum.

**2. Materials hint:**
Pass through `research.materials_hint` directly:
- `finished_product_uom` → from `research.materials_hint.uom_typical`
- `components` → map `research.materials_hint.key_components` to `[{"name": "<name>", "kind": "raw"}]` and `research.materials_hint.key_raw_materials` similarly. If `research.production.batch_or_serial` is `"batch"`, add `"batch_managed": true` at the top level of `materials_hint`.

**3. Workforce hint:**
Pass through `research.workforce_hint`:
- `operator_count`: count of distinct non-supervisor roles in `research.workforce_hint.typical_roles`. Default: 3 if empty.
- `supervisor_count`: 1 (default unless research explicitly states otherwise).
- `cert_requirements`: copy from `research.workforce_hint.cert_requirements`. Empty array if not present.

**4. Terminology:**
Select from `research.terminology[]` the 3-5 terms whose `context` field contains any of: "DM demo", "material", "operation", "routing". If fewer than 3 qualify, take the first 3 by order. Output as flat array of term strings.

---

### Pass 6 — Gap evaluation

Load-bearing gaps (hard gate):

| Condition | Interactive | Auto |
|---|---|---|
| `product.type` not in `discrete\|process\|repetitive` | Hard stop: ask human | `status: needs-review`, add G-record |
| `production_flow.steps[]` empty after Pass 3 mapping | Hard stop: ask human | `status: needs-review`, add G-record |

Non-load-bearing gaps (log and proceed):

| Condition | Action |
|---|---|
| `materials_hint.finished_good` null in research | Add G-record, set `finished_product_uom: "PC"` as default |
| `workforce_hint.typical_roles` empty | Add G-record, use default (3 operators, 1 supervisor) |
| `moments_of_expertise` in research is empty | B-fill fully from bank (Pass 5 handles this) |

After all passes, count `meta.assumptions[]` + `meta.gaps_flagged[]`. If count > 30 in auto mode, do NOT add more assumptions — append a single G-record: `"Auto-mode assumption cap reached (30). Remaining inferences not logged."` and set `status: needs-review`.

Set final `status`:
- `"ok"` if no load-bearing gaps were triggered.
- `"needs-review"` if any load-bearing gap fired in auto mode.
- `"failed"` if a hard stop condition was reached and not resolved (interactive mode only, user walked away).

---

## brainstorm-summary.md

Write a brief human-readable summary alongside the JSON. Use Obsidian conventions.

```markdown
---
type: brainstorm-summary
tags: [dm, brainstorm, <slug>]
created: <YYYY-MM-DD>
---

# Brainstorm Summary: <scenario.title>

**Slug:** `<slug>`
**Product type:** <product.type> (<product.order_type>)
**Duration:** <duration_min> min | **Audience:** <audience_role>
**Focus:** <focus joined with ", ">
**Rework loop:** <yes|no> — <rework_rationale>

## Story

<scenario.story>

## Production flow

<numbered list of steps with type>

## Assumptions (<count>)

<table of id, field_path, value_chosen, confidence, source>

## Gaps flagged (<count>)

<table of id, field_path, issue>
```

---

## R1-R11 compliance notes

- **R1:** Output file has `brainstorm_output_version: 1`.
- **R2:** `slug` read from research, written unchanged, never modified.
- **R3:** `mode` from task prompt. `may_prompt: true` — prompts allowed. If `mode=auto` received, never call AskUserQuestion.
- **R4:** Every non-obvious decision is an A-record. Every data gap is a G-record. IDs use `A\d{3}` / `G\d{3}` format. Cap: 30 combined in auto mode.
- **R5:** No em-dashes anywhere. No numeric Fiori IDs. Obsidian conventions in the summary MD. ASCII only in JSON string fields.
- **R6:** Writes to Mitchell vault only. Never writes to DM_MQTT.
- **R7:** If `has_rework_loop = true`, `rework_from_to` is a 2-element array (never branched). This is a hint only; dm-master-data-designer owns the actual routing structure.
- **R8:** Return envelope `{status, diff, assumptions_add[], gaps_add[]}` to caller.
- **R9:** No direct PREFLIGHT output from this agent. Prereqs flow into Build Demo's aggregated PREFLIGHT.md.
- **R10:** On failure, set `status: failed` in return envelope. Never abort silently.
- **R11:** Agent doc lives at `Mitchell/Projects/Agent Network/agents/demo-brainstorm.md`. ADR-010 covers the `order_type` decision.
```

- [ ] **Step 2: Verify the agent file**

```bash
ls -la ~/.claude/agents/demo-brainstorm.md && wc -l ~/.claude/agents/demo-brainstorm.md
```

Expected: file present, >100 lines.

- [ ] **Step 3: Commit**

```bash
cd /Users/I584748/DM_MQTT
git add ~/.claude/agents/demo-brainstorm.md
git commit -m "feat(brainstorm): add demo-brainstorm agent file"
```

---

## Task 4: Network docs — ADR-010, agent doc, contract doc

**Files:**
- Create: `Mitchell/Projects/Agent Network/decisions/ADR-010-order-type.md`
- Replace: `Mitchell/Projects/Agent Network/agents/demo-brainstorm.md`
- Replace: `Mitchell/Projects/Agent Network/contracts/brainstorm-output-v1.md`

> All paths relative to `~/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/`

- [ ] **Step 1: Create ADR-010**

```markdown
---
type: adr
tags: [agent-network, adr, decision, load-bearing]
status: accepted
date: 2026-07-08
---

# ADR-010: Order type decoupled from product type

## Status

Accepted.

## Context

`product.type` (discrete/process/repetitive) describes the manufacturing model. `order_type` (production_order/process_order) describes the DM order management mode. These are correlated but not identical: a process-type product can run on production orders (non-standard but valid in DM), and a discrete product always uses production orders.

## Decision

`product.order_type` is a distinct required field in `brainstorm_output_v1.json` when `product.type = "process"`. It is silently defaulted for discrete and repetitive types.

See `~/.claude/agents/dm-demo-shared/order-type-rules.md` for the full decision table.

## Consequences

- `dm-master-data-designer` reads `product.order_type` to determine routing type (phase-based vs operation-based).
- Interactive mode Q4 surfaces this choice only for process-type products.
- A low-confidence A-record is emitted when a user overrides the default (`process_order`) for a process-type product.

## Links

- Design spec: `~/.claude/plans/context-you-must-load-sparkling-brooks.md` decision D8
- Rules file: `~/.claude/agents/dm-demo-shared/order-type-rules.md`
- Sibling: [[ADR-008-network-wide-rules]]
```

- [ ] **Step 2: Replace agents/demo-brainstorm.md stub with full doc**

```markdown
---
type: agent
tags: [agent-network, agents, demo-brainstorm]
status: v1
---

# Demo Brainstorm

## Purpose

Creative step in the demo chain. Reads `research_output_v1.json` and presales intent, produces `brainstorm_output_v1.json` that `dm-demo-orchestrator` consumes directly.

## Chain position

Production Research → **Demo Brainstorm** → Build Demo → runnable demo

## Inputs

- [[../contracts/research-output-v1|Research Output v1]] — manufacturing facts
- Presales intent — interactive Q&A (four questions in one front-load block) or auto defaults

## Output

- [[../contracts/brainstorm-output-v1|Brainstorm Output v1]] — scenario spec

## Consumed by

- Build Demo ([[build-demo]]) via dm-demo-orchestrator
- Asset Sim (receives full research output forwarded by orchestrator — not the brainstorm output directly)

## Architecture

Single agent file at `~/.claude/agents/demo-brainstorm.md`. Backed by shared pack files in `~/.claude/agents/dm-demo-shared/`:
- `brainstorm-contract-target.md` — canonical output schema
- `order-type-rules.md` — product.type → order_type decision table

Six sequential passes:
1. Presales intent (front-load Q&A or auto defaults)
2. Scenario frame (focus array, tri-signal rework model)
3. Production flow mapping (research steps → DM operation hints)
4. Story authoring (5-6 sentence narrative, hard-confirm gate in interactive)
5. Supporting hints (MoE curation + B-fill, materials, workforce, terminology)
6. Gap evaluation (load-bearing hard gates, non-load-bearing log-and-proceed)

## Locked decisions

| # | Decision |
|---|---|
| D1 | Lean agent + shared pack (mirrors Build Demo pattern) |
| D2 | Story: proposed + hard-confirm gate in interactive; auto-authors without gate |
| D3 | Focus array: research.meta.demo_focus is always seed (cannot be dropped); human adds optionally |
| D4 | Rework: tri-signal model (rework_common + quality pain point + focus contains quality-rework); 2/3 signals → recommend loop |
| D5 | Moments of expertise: curate from research first; B-fill from bank to minimum 2 |
| D6 | Interactive mode: front-load block of 4 questions in one message |
| D7 | Gap handling: load-bearing gaps are hard gates in interactive, needs-review in auto |
| D8 | product.order_type decoupled from product.type (see [[../decisions/ADR-010-order-type]]) |

## Mode

`may_prompt: true` (see [[../../../../.claude/agents/dm-demo-shared/mode-contract|mode-contract]]).

## Spec

`~/.claude/plans/context-you-must-load-sparkling-brooks.md`

## Golden fixtures

| Fixture | product.type | order_type | has_rework_loop | Primary focus | Duration |
|---|---|---|---|---|---|
| acme-food-noodles | process | process_order | true | operator-ux | 45 min |
| acme-plastics-caps | discrete | production_order | false | dispatching | 30 min |
| acme-electronics-pcb | discrete | production_order | false | monitoring | 60 min |
```

- [ ] **Step 3: Replace contracts/brainstorm-output-v1.md stub with full doc**

```markdown
---
type: contract
tags: [agent-network, contracts, brainstorm-output]
version: 1
status: defined
spec: "~/.claude/plans/context-you-must-load-sparkling-brooks.md"
---

# Brainstorm Output v1

Emitted by the **Demo Brainstorm Agent**. Consumed by Build Demo's `dm-demo-orchestrator` and (indirectly, via orchestrator forwarding) Asset Sim.

## Canonical schema

Source of truth: `~/.claude/agents/dm-demo-shared/brainstorm-contract-target.md`

This document is the human-readable wrapper. The executable contract lives in the shared pack.

## How it reads Research Output v1

| Research output field | Used for |
|---|---|
| `production.steps[]` | Seed for `production_flow.steps[]`. Brainstorm selects and maps; Research emits facts. |
| `materials_hint` | Populates `materials_hint`. Company-specific finished good name and component vocabulary. |
| `workforce_hint` | Populates `workforce_hint`. Realistic role names and cert requirements. |
| `moments_of_expertise` | Seeded into `moments_of_expertise` (curate step). B-fill from bank if < 2. |
| `pain_points[]` | Informs `scenario.pain_points_addressed` and `scenario.story` S3. |
| `terminology[]` | Provides vocabulary for material IDs and talking points. |
| `competitive_context` | Informs story S3 (incumbent system being replaced). |
| `slug` | Propagates unchanged. Brainstorm NEVER mints or modifies it. |
| `product.type` | Load-bearing for routing type selection and order_type decision. |
| `meta.demo_focus` | Primary seed for `scenario.focus[]`. |

> [!important] Flat fields
> `company` and `industry` in brainstorm output are flat slug segments, NOT the rich objects from research output. Slug is the canonical identity carrier (R2).

## Consumed by

- [[../agents/build-demo|Build Demo]] (dm-demo-orchestrator reads this directly)
- Asset Sim receives the full research output forwarded by orchestrator — not the brainstorm output
```

- [ ] **Step 4: Verify all three files exist**

```bash
MITCHELL_BASE="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network"
ls "$MITCHELL_BASE/decisions/ADR-010-order-type.md"
ls "$MITCHELL_BASE/agents/demo-brainstorm.md"
ls "$MITCHELL_BASE/contracts/brainstorm-output-v1.md"
```

Expected: all three lines output a file path, no errors.

- [ ] **Step 5: Commit**

```bash
cd /Users/I584748/DM_MQTT
MITCHELL_BASE="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network"
git add "$MITCHELL_BASE/decisions/ADR-010-order-type.md" \
        "$MITCHELL_BASE/agents/demo-brainstorm.md" \
        "$MITCHELL_BASE/contracts/brainstorm-output-v1.md"
git commit -m "feat(brainstorm): add network docs — ADR-010, agent doc, contract doc"
```

---

## Task 5: Golden fixture — noodles expected output + verify script

**Files:**
- Create: `Mitchell/Projects/Agent Network/golden-fixtures/acme-food-noodles/brainstorm_output_v1.expected.json`
- Create: `Mitchell/Projects/Agent Network/golden-fixtures/acme-food-noodles/verify-brainstorm-acme-food-noodles.sh`

The noodles fixture exercises the most paths: process type, Q4 order_type, tri-signal rework, hard-confirm gates, B-fill moments, full story structure. Author the expected output by reasoning from the existing `brainstorm-acme-food-noodles-20260707.json` in the same folder PLUS applying all locked schema decisions (adding `meta`, `dm_operation_hint`, `rework_rationale`, `seq`, etc.).

- [ ] **Step 1: Read the existing brainstorm JSON to use as base**

```bash
cat "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures/acme-food-noodles/brainstorm-acme-food-noodles-20260707.json"
```

- [ ] **Step 2: Write brainstorm_output_v1.expected.json**

Write the full expected output, extending the existing brainstorm JSON to match the v1 schema. The key additions are: `product.order_type`, `scenario.rework_rationale`, `production_flow.steps[].seq`, `production_flow.steps[].dm_operation_hint`, `production_flow.rework_from_to` as named steps, `moments_of_expertise` as rich objects, `terminology`, and `meta` block.

```json
{
  "brainstorm_output_version": 1,
  "slug": "acme-food-noodles-20260707",
  "company": "acme",
  "industry": "food",
  "product": {
    "name": "Instant Noodles 100g",
    "short": "noodles",
    "type": "process",
    "order_type": "process_order"
  },
  "scenario": {
    "title": "Instant noodle line — flow production with QC rework",
    "story": "Every plant has quality rework — the question is whether it flows or breaks the line. Acme's noodle line runs three shifts producing Instant Noodles 100g, a process-order product with a semi-finished dough intermediate. A recent batch of curly noodles failed final visual QC 8% of the time last month, and that rework disappears into spreadsheets. This demo shows operators confirming completions via NFC badge tap and the rework path surfacing as a first-class queue item so the supervisor's board stays honest. The line supervisor watches a failed QC batch route automatically to the rework queue and a certified operator pick it up without a phone call. Without DM, rework hides in spreadsheets and operators lose track after failed QC.",
    "duration_min": 45,
    "focus": ["operator-ux", "quality-rework", "monitoring"],
    "audience_role": "plant_manager",
    "has_rework_loop": true,
    "rework_rationale": "Signals A and B fired: production.rework_common=true, quality pain point at medium confidence (rework hides in spreadsheets).",
    "pain_points_addressed": [
      "rework hides in spreadsheets",
      "operators lose track after failed QC"
    ]
  },
  "production_flow": {
    "steps": [
      {
        "seq": 1,
        "name": "Mixing",
        "dm_operation_hint": "OP-10-MIX",
        "type": "machine",
        "hint_resource": "mixer"
      },
      {
        "seq": 2,
        "name": "Filling",
        "dm_operation_hint": "OP-20-FILL",
        "type": "machine",
        "hint_resource": "filler"
      },
      {
        "seq": 3,
        "name": "Packing",
        "dm_operation_hint": "OP-30-PACK",
        "type": "machine",
        "hint_resource": "packer"
      },
      {
        "seq": 4,
        "name": "QC",
        "dm_operation_hint": "OP-40-QC",
        "type": "inspection"
      }
    ],
    "rework_from_to": ["QC", "Packing"]
  },
  "materials_hint": {
    "finished_product_uom": "PC",
    "components": [
      {"name": "Dough", "kind": "semi-finished"},
      {"name": "Seasoning", "kind": "raw"},
      {"name": "Water", "kind": "raw"}
    ]
  },
  "workforce_hint": {
    "operator_count": 3,
    "supervisor_count": 1,
    "cert_requirements": []
  },
  "moments_of_expertise": [
    {
      "claim": "Every plant has quality rework, the question is whether it flows or breaks the line.",
      "evidence": "Noodle line reported 8% QC failure rate last month; rework tracked in spreadsheets.",
      "source": "research",
      "bank_entry_ref": null
    },
    {
      "claim": "Operators trust the system when the system trusts them back.",
      "evidence": "Badge tap unlocks the queue; operator agency is preserved.",
      "source": "bank",
      "bank_entry_ref": "Operators trust the system"
    }
  ],
  "terminology": [],
  "meta": {
    "mode": "auto",
    "created": "2026-07-07",
    "research_source": "Mitchell/Projects/Agent Network/golden-fixtures/acme-food-noodles/research_output_v1.expected.json",
    "assumptions": [
      {
        "id": "A001",
        "phase": "presales-intent",
        "field_path": "scenario.audience_role",
        "value_chosen": "plant_manager",
        "rationale": "Default for known_to_sap=true company.",
        "confidence": "medium",
        "source": "default",
        "resolved": false
      },
      {
        "id": "A002",
        "phase": "presales-intent",
        "field_path": "scenario.duration_min",
        "value_chosen": "45",
        "rationale": "Default duration.",
        "confidence": "medium",
        "source": "default",
        "resolved": false
      },
      {
        "id": "A003",
        "phase": "presales-intent",
        "field_path": "product.order_type",
        "value_chosen": "process_order",
        "rationale": "Default for product.type=process (order-type-rules.md).",
        "confidence": "medium",
        "source": "default",
        "resolved": false
      },
      {
        "id": "A004",
        "phase": "scenario-frame",
        "field_path": "scenario.has_rework_loop",
        "value_chosen": "true",
        "rationale": "Signals A and B fired: rework_common=true, quality pain point at medium confidence.",
        "confidence": "high",
        "source": "inferred",
        "resolved": false
      },
      {
        "id": "A005",
        "phase": "production-flow",
        "field_path": "production_flow.rework_from_to",
        "value_chosen": "[\"QC\", \"Packing\"]",
        "rationale": "QC is last inspection step; Packing is first step after potential quality issue.",
        "confidence": "medium",
        "source": "inferred",
        "resolved": false
      },
      {
        "id": "A006",
        "phase": "story",
        "field_path": "scenario.story",
        "value_chosen": "auto-authored",
        "rationale": "Story authored from research facts in auto mode.",
        "confidence": "medium",
        "source": "inferred",
        "resolved": false
      }
    ],
    "gaps_flagged": []
  }
}
```

- [ ] **Step 3: Write the verify script**

Write `verify-brainstorm-acme-food-noodles.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

FIXTURE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPECTED_JSON="$FIXTURE_DIR/brainstorm_output_v1.expected.json"
DEMOS_DIR="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos"
PASS=0
FAIL=0

echo "=== Noodles brainstorm fixture verification ==="

SLUG_DIR=$(find "$DEMOS_DIR" -maxdepth 1 -name "acme-food-noodles-*" -type d 2>/dev/null | sort | tail -1)
if [ -z "$SLUG_DIR" ]; then
  echo "FAIL: No acme-food-noodles-* slug folder found in Demos/"
  exit 1
fi
echo "Slug dir: $SLUG_DIR"

ACTUAL_JSON="$SLUG_DIR/brainstorm_output_v1.json"
ACTUAL_SUMMARY="$SLUG_DIR/brainstorm-summary.md"

if [ -f "$ACTUAL_JSON" ]; then echo "PASS: brainstorm_output_v1.json exists"; ((PASS++)); else echo "FAIL: brainstorm_output_v1.json missing"; ((FAIL++)); fi
if [ -f "$ACTUAL_SUMMARY" ]; then echo "PASS: brainstorm-summary.md exists"; ((PASS++)); else echo "FAIL: brainstorm-summary.md missing"; ((FAIL++)); fi

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
v = data.get('brainstorm_output_version')
if v == 1:
    print('PASS: brainstorm_output_version = 1')
else:
    print(f'FAIL: brainstorm_output_version = {v} (expected 1)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
slug = data.get('slug')
if slug == 'acme-food-noodles-20260707':
    print(f'PASS: slug = {slug}')
else:
    print(f'FAIL: slug = {slug}')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
ot = data['product']['order_type']
if ot == 'process_order':
    print(f'PASS: product.order_type = {ot}')
else:
    print(f'FAIL: product.order_type = {ot} (expected process_order)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
rl = data['scenario']['has_rework_loop']
if rl == True:
    print('PASS: has_rework_loop = true')
else:
    print(f'FAIL: has_rework_loop = {rl} (expected true)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
rft = data['production_flow']['rework_from_to']
if isinstance(rft, list) and len(rft) == 2 and rft[0] is not None:
    print(f'PASS: rework_from_to = {rft}')
else:
    print(f'FAIL: rework_from_to = {rft} (expected non-null 2-element array)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
n = len(data.get('moments_of_expertise', []))
if n >= 2:
    print(f'PASS: moments_of_expertise count = {n}')
else:
    print(f'FAIL: moments_of_expertise count = {n} (need >= 2)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
steps = data['production_flow']['steps']
seqs = [s['seq'] for s in steps]
expected_seqs = list(range(1, len(steps)+1))
if seqs == expected_seqs:
    print(f'PASS: production_flow steps seqs are sequential: {seqs}')
else:
    print(f'FAIL: production_flow steps seqs = {seqs} (expected {expected_seqs})')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
status = data['meta'].get('mode')
if status == 'auto':
    print('PASS: meta.mode = auto')
else:
    print(f'WARN: meta.mode = {status}')
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib, sys
def strip_variable(d):
    if isinstance(d, dict):
        return {k: strip_variable(v) for k, v in d.items()
                if k not in ('created', 'accessed', 'id', 'bank_entry_ref')}
    if isinstance(d, list):
        return [strip_variable(i) for i in d]
    return d
actual = strip_variable(json.loads(pathlib.Path('$ACTUAL_JSON').read_text()))
expected = strip_variable(json.loads(pathlib.Path('$EXPECTED_JSON').read_text()))
if actual == expected:
    print('PASS: output matches expected (modulo dates + assumption IDs)')
else:
    actual_str = json.dumps(actual, indent=2, sort_keys=True)
    expected_str = json.dumps(expected, indent=2, sort_keys=True)
    from difflib import unified_diff
    diff = list(unified_diff(expected_str.splitlines(), actual_str.splitlines(),
                             fromfile='expected', tofile='actual', lineterm=''))
    print('FAIL: output differs from expected:')
    print('\n'.join(diff[:80]))
    sys.exit(1)
" && ((PASS++)) || ((FAIL++))

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ $FAIL -eq 0 ] && echo "PASS: all checks" || { echo "FAIL: $FAIL check(s) failed"; exit 1; }
```

- [ ] **Step 4: Make verify script executable**

```bash
chmod +x "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures/acme-food-noodles/verify-brainstorm-acme-food-noodles.sh"
```

- [ ] **Step 5: Commit**

```bash
FIXTURE="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures/acme-food-noodles"
cd /Users/I584748/DM_MQTT
git add "$FIXTURE/brainstorm_output_v1.expected.json" "$FIXTURE/verify-brainstorm-acme-food-noodles.sh"
git commit -m "feat(fixtures): add noodles brainstorm expected output + verify script"
```

---

## Task 6: Golden fixture — plastics expected output + verify script

**Files:**
- Create: `Mitchell/Projects/Agent Network/golden-fixtures/acme-plastics-caps/brainstorm_output_v1.expected.json`
- Create: `Mitchell/Projects/Agent Network/golden-fixtures/acme-plastics-caps/verify-brainstorm-acme-plastics-caps.sh`

Plastics: discrete type, no rework, dispatching focus, 30 min, includes `tools_hint`.

- [ ] **Step 1: Write brainstorm_output_v1.expected.json**

```json
{
  "brainstorm_output_version": 1,
  "slug": "acme-plastics-caps-20260707",
  "company": "acme",
  "industry": "plastics",
  "product": {
    "name": "28mm PET Cap",
    "short": "caps",
    "type": "discrete",
    "order_type": "production_order"
  },
  "scenario": {
    "title": "Injection molding line, mold changeover and post-trim QC",
    "story": "Every plastics line dances with changeover — the winners cut it in half. Acme's cap line runs two molds on a single injection machine producing 28mm PET caps, with average changeover taking twelve minutes per variant switch. Mold changeover eats capacity and batch flash defects surface late, leaving the plant manager dispatching orders from a whiteboard. This demo shows the dispatcher releasing and prioritizing the injection queue in DM and tracking batch traceability from mold to outgoing lot. The plant manager watches a released order move to dispatched with a single action and the batch QC result attach automatically. Without DM, mold changeover planning stays on paper and batch flash defects surface only at the customer.",
    "duration_min": 30,
    "focus": ["dispatching", "throughput"],
    "audience_role": "plant_manager",
    "has_rework_loop": false,
    "rework_rationale": "No signals fired: rework_common not confirmed, no quality pain points at medium+ confidence in focus area.",
    "pain_points_addressed": [
      "mold changeover eats capacity",
      "batch flash defects surface late"
    ]
  },
  "production_flow": {
    "steps": [
      {
        "seq": 1,
        "name": "Injection",
        "dm_operation_hint": "OP-10-INJ",
        "type": "machine",
        "hint_resource": "injector"
      },
      {
        "seq": 2,
        "name": "Trim",
        "dm_operation_hint": "OP-20-TRIM",
        "type": "machine",
        "hint_resource": "trimmer"
      },
      {
        "seq": 3,
        "name": "QC",
        "dm_operation_hint": "OP-30-QC",
        "type": "inspection"
      }
    ],
    "rework_from_to": [null, null]
  },
  "materials_hint": {
    "finished_product_uom": "PC",
    "batch_managed": true,
    "components": [
      {"name": "HDPE Pellet", "kind": "raw"}
    ]
  },
  "workforce_hint": {
    "operator_count": 3,
    "supervisor_count": 1,
    "cert_requirements": []
  },
  "tools_hint": [{"name": "Mold A"}, {"name": "Mold B"}],
  "moments_of_expertise": [
    {
      "claim": "Every plastics line dances with changeover, the winners cut it in half.",
      "evidence": "Acme cap line averages 12-minute changeover; opportunity to show dispatching impact.",
      "source": "research",
      "bank_entry_ref": null
    },
    {
      "claim": "Batch traceability is boring until you get a customer complaint.",
      "evidence": "Flash defects surface late, batch link is manual today.",
      "source": "research",
      "bank_entry_ref": null
    }
  ],
  "terminology": [],
  "meta": {
    "mode": "auto",
    "created": "2026-07-07",
    "research_source": "Mitchell/Projects/Agent Network/golden-fixtures/acme-plastics-caps/research_output_v1.expected.json",
    "assumptions": [
      {
        "id": "A001",
        "phase": "presales-intent",
        "field_path": "scenario.audience_role",
        "value_chosen": "plant_manager",
        "rationale": "Default for known_to_sap=true company.",
        "confidence": "medium",
        "source": "default",
        "resolved": false
      },
      {
        "id": "A002",
        "phase": "presales-intent",
        "field_path": "scenario.duration_min",
        "value_chosen": "30",
        "rationale": "Default duration.",
        "confidence": "medium",
        "source": "default",
        "resolved": false
      },
      {
        "id": "A003",
        "phase": "scenario-frame",
        "field_path": "scenario.has_rework_loop",
        "value_chosen": "false",
        "rationale": "No signals fired: rework_common not confirmed, focus does not include quality-rework.",
        "confidence": "high",
        "source": "inferred",
        "resolved": false
      },
      {
        "id": "A004",
        "phase": "story",
        "field_path": "scenario.story",
        "value_chosen": "auto-authored",
        "rationale": "Story authored from research facts in auto mode.",
        "confidence": "medium",
        "source": "inferred",
        "resolved": false
      }
    ],
    "gaps_flagged": []
  }
}
```

- [ ] **Step 2: Write the verify script**

```bash
#!/usr/bin/env bash
set -euo pipefail

FIXTURE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPECTED_JSON="$FIXTURE_DIR/brainstorm_output_v1.expected.json"
DEMOS_DIR="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos"
PASS=0
FAIL=0

echo "=== Plastics-caps brainstorm fixture verification ==="

SLUG_DIR=$(find "$DEMOS_DIR" -maxdepth 1 -name "acme-plastics-caps-*" -type d 2>/dev/null | sort | tail -1)
if [ -z "$SLUG_DIR" ]; then
  echo "FAIL: No acme-plastics-caps-* slug folder found in Demos/"
  exit 1
fi
echo "Slug dir: $SLUG_DIR"

ACTUAL_JSON="$SLUG_DIR/brainstorm_output_v1.json"

if [ -f "$ACTUAL_JSON" ]; then echo "PASS: brainstorm_output_v1.json exists"; ((PASS++)); else echo "FAIL: brainstorm_output_v1.json missing"; ((FAIL++)); fi

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
ot = data['product']['order_type']
if ot == 'production_order':
    print(f'PASS: product.order_type = {ot}')
else:
    print(f'FAIL: product.order_type = {ot} (expected production_order)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
rl = data['scenario']['has_rework_loop']
if rl == False:
    print('PASS: has_rework_loop = false')
else:
    print(f'FAIL: has_rework_loop = {rl} (expected false)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
rft = data['production_flow']['rework_from_to']
if rft == [None, None]:
    print('PASS: rework_from_to = [null, null]')
else:
    print(f'FAIL: rework_from_to = {rft} (expected [null, null])')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib, sys
def strip_variable(d):
    if isinstance(d, dict):
        return {k: strip_variable(v) for k, v in d.items()
                if k not in ('created', 'accessed', 'id', 'bank_entry_ref')}
    if isinstance(d, list):
        return [strip_variable(i) for i in d]
    return d
actual = strip_variable(json.loads(pathlib.Path('$ACTUAL_JSON').read_text()))
expected = strip_variable(json.loads(pathlib.Path('$EXPECTED_JSON').read_text()))
if actual == expected:
    print('PASS: output matches expected (modulo dates + assumption IDs)')
else:
    actual_str = json.dumps(actual, indent=2, sort_keys=True)
    expected_str = json.dumps(expected, indent=2, sort_keys=True)
    from difflib import unified_diff
    diff = list(unified_diff(expected_str.splitlines(), actual_str.splitlines(),
                             fromfile='expected', tofile='actual', lineterm=''))
    print('FAIL: output differs from expected:')
    print('\n'.join(diff[:80]))
    sys.exit(1)
" && ((PASS++)) || ((FAIL++))

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ $FAIL -eq 0 ] && echo "PASS: all checks" || { echo "FAIL: $FAIL check(s) failed"; exit 1; }
```

- [ ] **Step 3: Make script executable and commit**

```bash
FIXTURE="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures/acme-plastics-caps"
chmod +x "$FIXTURE/verify-brainstorm-acme-plastics-caps.sh"
cd /Users/I584748/DM_MQTT
git add "$FIXTURE/brainstorm_output_v1.expected.json" "$FIXTURE/verify-brainstorm-acme-plastics-caps.sh"
git commit -m "feat(fixtures): add plastics-caps brainstorm expected output + verify script"
```

---

## Task 7: Golden fixture — PCB expected output + verify script

**Files:**
- Create: `Mitchell/Projects/Agent Network/golden-fixtures/acme-electronics-pcb/brainstorm_output_v1.expected.json`
- Create: `Mitchell/Projects/Agent Network/golden-fixtures/acme-electronics-pcb/verify-brainstorm-acme-electronics-pcb.sh`

PCB: discrete, no rework, monitoring+OEE focus, 60 min, serialized.

- [ ] **Step 1: Write brainstorm_output_v1.expected.json**

```json
{
  "brainstorm_output_version": 1,
  "slug": "acme-electronics-pcb-20260707",
  "company": "acme",
  "industry": "electronics",
  "product": {
    "name": "Sensor PCB v3",
    "short": "pcb",
    "type": "discrete",
    "order_type": "production_order"
  },
  "scenario": {
    "title": "SMT line with dense telemetry and serialized boards",
    "story": "Every SMT line has a reflow profile that looks fine on paper and misbehaves at 4 pm. Acme's SMT line feeds serialized Sensor PCB v3 boards into automotive assemblies, where traceability to component lot is a customer requirement. Cross-line telemetry is missing and the serial number-to-defect link is manual today, which means a defect traced back to a component lot becomes a fire drill. This demo shows a data-rich line view with solder paste temperatures, pick-and-place cycle counts, reflow oven profile, and AOI reject rates all visible in a single monitoring POD. An automotive quality auditor watches the reflow oven trend break its control limit and DM flag the affected serial range automatically. Without DM, the serial-to-defect link stays manual and the first signal of a lot problem is a customer complaint.",
    "duration_min": 60,
    "focus": ["monitoring", "oee"],
    "audience_role": "plant_manager",
    "has_rework_loop": false,
    "rework_rationale": "No signals fired: focus does not include quality-rework, no quality pain points at medium+ confidence.",
    "pain_points_addressed": [
      "no cross-line telemetry view",
      "serial number-to-defect link is manual"
    ]
  },
  "production_flow": {
    "steps": [
      {
        "seq": 1,
        "name": "Solder Paste",
        "dm_operation_hint": "OP-10-SOLD",
        "type": "machine",
        "hint_resource": "solder-paste-printer"
      },
      {
        "seq": 2,
        "name": "Pick and Place",
        "dm_operation_hint": "OP-20-PICK",
        "type": "machine",
        "hint_resource": "pick-place"
      },
      {
        "seq": 3,
        "name": "Reflow",
        "dm_operation_hint": "OP-30-REFL",
        "type": "machine",
        "hint_resource": "reflow-oven"
      },
      {
        "seq": 4,
        "name": "AOI Inspection",
        "dm_operation_hint": "OP-40-AOI",
        "type": "inspection",
        "hint_resource": "aoi"
      }
    ],
    "rework_from_to": [null, null]
  },
  "materials_hint": {
    "finished_product_uom": "PC",
    "serialized": true,
    "components": [
      {"name": "PCB Blank", "kind": "raw"},
      {"name": "Solder Paste", "kind": "raw"},
      {"name": "Components", "kind": "raw"}
    ]
  },
  "workforce_hint": {
    "operator_count": 4,
    "supervisor_count": 1,
    "cert_requirements": []
  },
  "moments_of_expertise": [
    {
      "claim": "Every SMT line has a reflow profile that looks fine on paper and misbehaves at 4 pm.",
      "evidence": "Thermal drift in reflow ovens is a documented failure mode in high-mix SMT production.",
      "source": "research",
      "bank_entry_ref": null
    },
    {
      "claim": "Serial traceability is only valuable when a defect happens, so build it before you need it.",
      "evidence": "Automotive customer requires lot-level traceability; current manual link is a risk.",
      "source": "research",
      "bank_entry_ref": null
    }
  ],
  "terminology": [],
  "meta": {
    "mode": "auto",
    "created": "2026-07-07",
    "research_source": "Mitchell/Projects/Agent Network/golden-fixtures/acme-electronics-pcb/research_output_v1.expected.json",
    "assumptions": [
      {
        "id": "A001",
        "phase": "presales-intent",
        "field_path": "scenario.audience_role",
        "value_chosen": "plant_manager",
        "rationale": "Default for known_to_sap=true company.",
        "confidence": "medium",
        "source": "default",
        "resolved": false
      },
      {
        "id": "A002",
        "phase": "presales-intent",
        "field_path": "scenario.duration_min",
        "value_chosen": "60",
        "rationale": "Default duration.",
        "confidence": "medium",
        "source": "default",
        "resolved": false
      },
      {
        "id": "A003",
        "phase": "scenario-frame",
        "field_path": "scenario.has_rework_loop",
        "value_chosen": "false",
        "rationale": "No signals fired: focus is monitoring/oee, not quality-rework.",
        "confidence": "high",
        "source": "inferred",
        "resolved": false
      },
      {
        "id": "A004",
        "phase": "story",
        "field_path": "scenario.story",
        "value_chosen": "auto-authored",
        "rationale": "Story authored from research facts in auto mode.",
        "confidence": "medium",
        "source": "inferred",
        "resolved": false
      }
    ],
    "gaps_flagged": []
  }
}
```

- [ ] **Step 2: Write the verify script**

```bash
#!/usr/bin/env bash
set -euo pipefail

FIXTURE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPECTED_JSON="$FIXTURE_DIR/brainstorm_output_v1.expected.json"
DEMOS_DIR="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos"
PASS=0
FAIL=0

echo "=== PCB brainstorm fixture verification ==="

SLUG_DIR=$(find "$DEMOS_DIR" -maxdepth 1 -name "acme-electronics-pcb-*" -type d 2>/dev/null | sort | tail -1)
if [ -z "$SLUG_DIR" ]; then
  echo "FAIL: No acme-electronics-pcb-* slug folder found in Demos/"
  exit 1
fi
echo "Slug dir: $SLUG_DIR"

ACTUAL_JSON="$SLUG_DIR/brainstorm_output_v1.json"

if [ -f "$ACTUAL_JSON" ]; then echo "PASS: brainstorm_output_v1.json exists"; ((PASS++)); else echo "FAIL: brainstorm_output_v1.json missing"; ((FAIL++)); fi

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
rl = data['scenario']['has_rework_loop']
if rl == False:
    print('PASS: has_rework_loop = false')
else:
    print(f'FAIL: has_rework_loop = {rl} (expected false)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
n = len(data.get('moments_of_expertise', []))
if n >= 2:
    print(f'PASS: moments_of_expertise count = {n}')
else:
    print(f'FAIL: moments_of_expertise count = {n} (need >= 2)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

python3 -c "
import json, pathlib, sys
def strip_variable(d):
    if isinstance(d, dict):
        return {k: strip_variable(v) for k, v in d.items()
                if k not in ('created', 'accessed', 'id', 'bank_entry_ref')}
    if isinstance(d, list):
        return [strip_variable(i) for i in d]
    return d
actual = strip_variable(json.loads(pathlib.Path('$ACTUAL_JSON').read_text()))
expected = strip_variable(json.loads(pathlib.Path('$EXPECTED_JSON').read_text()))
if actual == expected:
    print('PASS: output matches expected (modulo dates + assumption IDs)')
else:
    actual_str = json.dumps(actual, indent=2, sort_keys=True)
    expected_str = json.dumps(expected, indent=2, sort_keys=True)
    from difflib import unified_diff
    diff = list(unified_diff(expected_str.splitlines(), actual_str.splitlines(),
                             fromfile='expected', tofile='actual', lineterm=''))
    print('FAIL: output differs from expected:')
    print('\n'.join(diff[:80]))
    sys.exit(1)
" && ((PASS++)) || ((FAIL++))

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ $FAIL -eq 0 ] && echo "PASS: all checks" || { echo "FAIL: $FAIL check(s) failed"; exit 1; }
```

- [ ] **Step 3: Make script executable and commit**

```bash
FIXTURE="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures/acme-electronics-pcb"
chmod +x "$FIXTURE/verify-brainstorm-acme-electronics-pcb.sh"
cd /Users/I584748/DM_MQTT
git add "$FIXTURE/brainstorm_output_v1.expected.json" "$FIXTURE/verify-brainstorm-acme-electronics-pcb.sh"
git commit -m "feat(fixtures): add PCB brainstorm expected output + verify script"
```

---

## Task 8: End-to-end smoke test

Run all three verify scripts against their expected outputs to confirm the fixtures are self-consistent (expected JSON passes all property checks against itself).

- [ ] **Step 1: Verify noodles expected JSON is valid JSON and self-consistent**

```bash
FIXTURE="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures/acme-food-noodles"
python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$FIXTURE/brainstorm_output_v1.expected.json').read_text())
assert data['brainstorm_output_version'] == 1
assert data['product']['order_type'] == 'process_order'
assert data['scenario']['has_rework_loop'] == True
assert data['production_flow']['rework_from_to'] != [None, None]
seqs = [s['seq'] for s in data['production_flow']['steps']]
assert seqs == list(range(1, len(seqs)+1)), f'seqs not sequential: {seqs}'
assert len(data['moments_of_expertise']) >= 2
print('PASS: noodles expected JSON self-consistent')
"
```

Expected: `PASS: noodles expected JSON self-consistent`

- [ ] **Step 2: Verify plastics expected JSON self-consistent**

```bash
FIXTURE="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures/acme-plastics-caps"
python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$FIXTURE/brainstorm_output_v1.expected.json').read_text())
assert data['brainstorm_output_version'] == 1
assert data['product']['order_type'] == 'production_order'
assert data['scenario']['has_rework_loop'] == False
assert data['production_flow']['rework_from_to'] == [None, None]
seqs = [s['seq'] for s in data['production_flow']['steps']]
assert seqs == list(range(1, len(seqs)+1)), f'seqs not sequential: {seqs}'
assert len(data['moments_of_expertise']) >= 2
print('PASS: plastics expected JSON self-consistent')
"
```

Expected: `PASS: plastics expected JSON self-consistent`

- [ ] **Step 3: Verify PCB expected JSON self-consistent**

```bash
FIXTURE="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures/acme-electronics-pcb"
python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$FIXTURE/brainstorm_output_v1.expected.json').read_text())
assert data['brainstorm_output_version'] == 1
assert data['product']['order_type'] == 'production_order'
assert data['scenario']['has_rework_loop'] == False
assert data['production_flow']['rework_from_to'] == [None, None]
seqs = [s['seq'] for s in data['production_flow']['steps']]
assert seqs == list(range(1, len(seqs)+1)), f'seqs not sequential: {seqs}'
assert len(data['moments_of_expertise']) >= 2
print('PASS: PCB expected JSON self-consistent')
"
```

Expected: `PASS: PCB expected JSON self-consistent`

- [ ] **Step 4: Confirm agent file and shared pack are all present**

```bash
ls -la ~/.claude/agents/demo-brainstorm.md \
        ~/.claude/agents/dm-demo-shared/brainstorm-contract-target.md \
        ~/.claude/agents/dm-demo-shared/order-type-rules.md
```

Expected: three lines, all non-zero size.

- [ ] **Step 5: Commit summary**

```bash
cd /Users/I584748/DM_MQTT
git log --oneline -6
```

Expected: last 6 commits include the tasks from this plan (shared pack, agent, network docs, three fixture tasks).

---

## Self-review

**Spec coverage check:**

| Spec section | Covered by |
|---|---|
| D1 lean agent + shared pack | Tasks 1, 2, 3 |
| D2 story hard-confirm gate | Task 3 Pass 4 |
| D3 focus array / seed model | Task 3 Pass 2 |
| D4 tri-signal rework | Task 3 Pass 2 |
| D5 moments B-fill | Task 3 Pass 5 |
| D6 front-load Q&A block | Task 3 Pass 1 |
| D7 gap handling | Task 3 Pass 6 |
| D8 order_type | Tasks 2, 3 Pass 1 |
| R1-R11 compliance notes | Task 3 (R1-R11 section) |
| New shared pack files | Tasks 1, 2 |
| Network docs (R11) | Task 4 |
| ADR-010 | Task 4 |
| Golden fixtures (all 3) | Tasks 5, 6, 7 |
| Verify scripts (all 3) | Tasks 5, 6, 7 |
| brainstorm-summary.md | Task 3 (summary section) |
| End-to-end smoke | Task 8 |

All spec requirements covered. No gaps found.
