# Production Research Agent — Plan A: Contracts and Network Docs

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade every stub contract and network doc to reflect the full Production Research design spec, and write two new ADRs locking the load-bearing architecture decisions.

**Architecture:** All artifacts are Markdown files in the Mitchell vault at `Mitchell/Projects/Agent Network/`. No code. Every change follows the additive-only Mitchell rule (no content removed). The `contracts/research-output-v1.md` stub is replaced with the full schema; `brainstorm-output-v1.md` gains consumer notes; two new ADRs are written; `agents/production-research.md` is promoted from stub to full doc; `network-map.md` gains a Production Research row.

**Tech Stack:** Obsidian Markdown, Obsidian conventions (frontmatter, wikilinks `[[]]`, callout blocks `> [!type]`), no em-dashes, no numeric Fiori IDs.

---

## File map

| Action | Path |
|---|---|
| Rewrite | `Mitchell/Projects/Agent Network/contracts/research-output-v1.md` |
| Modify | `Mitchell/Projects/Agent Network/contracts/brainstorm-output-v1.md` |
| Create | `Mitchell/Projects/Agent Network/decisions/ADR-009-production-steps-struct.md` |
| Create | `Mitchell/Projects/Agent Network/decisions/ADR-010-orchestrator-parallel-subagents.md` |
| Modify | `Mitchell/Projects/Agent Network/network-map.md` |
| Rewrite | `Mitchell/Projects/Agent Network/agents/production-research.md` |

---

### Task 1: Rewrite `contracts/research-output-v1.md`

**Files:**
- Modify: `Mitchell/Projects/Agent Network/contracts/research-output-v1.md`

The current file is a stub. Replace it entirely with the full schema from the spec. This is an intentional full rewrite of a stub — not a violation of the additive-only rule (stubs are placeholders by definition).

- [ ] **Step 1: Read the current stub**

Read `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/contracts/research-output-v1.md` to confirm it is still the stub (status: not started).

- [ ] **Step 2: Write the full contract**

Write the following content to that path:

```markdown
---
type: contract
tags: [agent-network, contracts, research-output]
version: 1
status: defined
spec: "~/.claude/plans/i-want-to-build-toasty-manatee.md"
---

# Research Output v1

## Purpose

Emitted by the **Production Research Agent** as the first artifact in the demo chain. Contains all real-world manufacturing context an agent needs to produce a credible DM demo scenario for a specific company and product. Consumed by [[brainstorm-output-v1|Demo Brainstorm]].

## Change log from stub

- `processes[]` (provisional top-level flat array) replaced by `production.steps[]` (nested, structured). See [[decisions/ADR-009-production-steps-struct]].
- `company` and `industry` promoted from flat strings to rich objects.
- `competitive_context`, `pain_points`, `terminology`, `sources[]` added.
- `meta.assumptions[]` and `meta.gaps_flagged[]` now explicit per R4 of [[decisions/ADR-008-network-wide-rules]].

## Schema

```json
{
  "research_output_version": 1,
  "slug": "<company>-<industry>-<product>-<yyyymmdd>",

  "company": {
    "name": "string",
    "normalized": "kebab-ascii-3-20-chars",
    "mitchell_client_ref": "[[Clients/Company/Company]] or null",
    "size": "large|medium|small|unknown",
    "hq_region": "APAC|EMEA|NA|LATAM|unknown",
    "known_to_sap": true
  },

  "industry": {
    "name": "string",
    "normalized": "kebab-ascii-3-20-chars",
    "mitchell_moc_ref": "[[MOC/Industries/MOC-X]] or null",
    "regulatory_context": ["ISO 9001", "CE marking"]
  },

  "product": {
    "name": "string",
    "short": "kebab-ascii-3-20-chars",
    "type": "discrete|process|repetitive",
    "description": "string"
  },

  "production": {
    "process_type_confirmed": true,
    "steps": [
      {
        "seq": 1,
        "name": "string",
        "description": "string",
        "typical_equipment": ["string"],
        "quality_checkpoints": ["string"],
        "can_trigger_rework": true
      }
    ],
    "rework_common": true,
    "rework_description": "string or null",
    "batch_or_serial": "serial|batch|both"
  },

  "equipment_hints": [
    {
      "name": "string",
      "category": "subtractive|assembly|inspection|filling|mixing|forming|other",
      "step_ref": 1,
      "typical_metrics": ["spindle_rpm", "tool_wear_pct"],
      "source": "web:url or mitchell_crm or mitchell_moc"
    }
  ],

  "materials_hint": {
    "finished_good": "string",
    "key_components": ["string"],
    "key_raw_materials": ["string"],
    "uom_typical": "EA|KG|L|M|PC"
  },

  "workforce_hint": {
    "typical_roles": ["string"],
    "cert_requirements": ["string"],
    "shift_pattern": "2-shift 5-day|3-shift 5-day|continental|other"
  },

  "quality_issues_hints": [
    {
      "name": "string",
      "description": "string",
      "step_ref": 1,
      "dm_nc_category": "dimensional|cosmetic|functional|process"
    }
  ],

  "pain_points": [
    {
      "description": "string",
      "category": "paper|visibility|traceability|scheduling|workforce|quality|other",
      "source": "mitchell_crm|web|inferred",
      "confidence": "high|medium|low"
    }
  ],

  "moments_of_expertise": [
    {
      "claim": "string — a universal manufacturing truth",
      "evidence": "string — why this is credible",
      "source": "string"
    }
  ],

  "terminology": [
    {
      "term": "string",
      "definition": "string",
      "context": "string — how to use in DM demo"
    }
  ],

  "competitive_context": {
    "incumbent_mes": "custom|SAP ME|Siemens Opcenter|other|unknown",
    "known_competitors_mentioned": ["string"],
    "source": "mitchell_crm|web|none"
  },

  "sources": [
    {
      "type": "mitchell_crm|mitchell_meeting|mitchell_moc|mitchell_dm_scenario|web|inferred",
      "ref": "[[wikilink]] or https://url",
      "accessed": "YYYY-MM-DD",
      "reliability": "high|medium|low"
    }
  ],

  "meta": {
    "mode": "interactive|auto",
    "created": "YYYY-MM-DD",
    "demo_focus": "operator-ux|quality-rework|throughput|OEE|dispatching|monitoring",
    "presales_notes_provided": true,
    "assumptions": [
      {
        "id": "A001",
        "phase": "slug-mint|product-identification|production-steps|equipment-hints|workforce-hint|materials-hint|quality-issues|moments-of-expertise",
        "field_path": "product.type",
        "value_chosen": "discrete",
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
        "responsible_agent": "production-research"
      }
    ]
  }
}
```

## Schema rules

- `slug` segments each 3-20 chars, lowercase-kebab ASCII. Minted per [[decisions/ADR-005-slug-identity]].
- `product.type` must be `discrete|process|repetitive`. Load-bearing for Demo Brainstorm routing decisions.
- `production.steps[].seq` sequential integers starting at 1.
- `equipment_hints[].step_ref` resolves to a valid `production.steps[].seq`.
- `quality_issues_hints[].step_ref` resolves to a valid `production.steps[].seq`.
- `equipment_hints[].typical_metrics[]` values are sapdm_config metric key strings (snake_case). Reference: `DM_MQTT/firmware/docs/superpowers/specs/2026-05-10-json-config-driven-demo.md`.
- `meta.assumptions[].id` unique, format `A\d{3}`.
- `meta.gaps_flagged[].id` unique, format `G\d{3}`.
- Auto-mode cap: max 30 combined entries across `meta.assumptions[]` + `meta.gaps_flagged[]`.

## R8 caller envelope

Separate from the JSON file written to disk. Returned to caller:

```json
{
  "status": "ok|failed",
  "diff": [
    "Mitchell/SAP Solutions/1 - DM/Demos/<slug>/research_output_v1.json",
    "Mitchell/SAP Solutions/1 - DM/Demos/<slug>/research-summary.md"
  ],
  "assumptions_add": [],
  "gaps_add": []
}
```

## Example instance

See `Mitchell/Projects/Agent Network/golden-fixtures/yanmar-machinery-engine-*/research_output_v1.expected.json` once Plan C fixtures are committed.

## Consumed by

- [[brainstorm-output-v1|Demo Brainstorm]] — reads `production.steps[]`, `materials_hint`, `workforce_hint`, `moments_of_expertise`, `equipment_hints[]`, `pain_points[]`, `terminology[]`
- Asset Sim — reads `equipment_hints[].typical_metrics[]` to seed machine metric vocabulary

## Conflict resolution rules

1. `mitchell_crm` or `mitchell_meeting` outranks `web` for same-company facts.
2. `web` (company's own site) outranks `inferred` for same-company facts.
3. `mitchell_moc` or `mitchell_dm_scenario` outranks general web for industry-norm facts.
4. Conflicting production step counts or sequences: log both in `gaps_flagged[]`, use Mitchell version, set `confidence: low`.
5. Never silently discard a conflicting fact.
```

- [ ] **Step 3: Verify the file was written correctly**

Read the file back and confirm the frontmatter `status` is `defined` (not `not started`) and the schema JSON block is present.

- [ ] **Step 4: Commit**

```bash
git add "Mitchell/Projects/Agent Network/contracts/research-output-v1.md"
git commit -m "docs: rewrite research-output-v1 contract from stub to full schema"
```

Run from `/Users/I584748/DM_MQTT`. Note: the Mitchell vault path is outside this repo, so commit the plan file here to record the change:

```bash
cd /Users/I584748/DM_MQTT
git add firmware/docs/superpowers/plans/2026-07-08-production-research-plan-a-contracts-and-docs.md
git commit -m "docs: add production research Plan A"
```

---

### Task 2: Update `contracts/brainstorm-output-v1.md` with consumer notes

**Files:**
- Modify: `Mitchell/Projects/Agent Network/contracts/brainstorm-output-v1.md`

The stub has a target shape but no notes on how it reads the research output. Add a consumer notes section. Do NOT alter the existing target shape — additive only.

- [ ] **Step 1: Read the current file**

Read `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/contracts/brainstorm-output-v1.md` to see current content.

- [ ] **Step 2: Append consumer notes section**

After the existing `## Consumed by` section, append:

```markdown
## How it reads Research Output v1

The Demo Brainstorm agent receives `research_output_v1.json` as its primary input. Key mappings:

| Research output field | Used for |
|---|---|
| `production.steps[]` | Seed candidates for `production_flow.steps[]`. Brainstorm selects which real-world steps become DM operations and assigns DM identities. Research emits facts; Brainstorm makes DM design decisions. |
| `materials_hint` | Populates `materials_hint` in brainstorm output. Company-specific finished good name and component vocabulary for realistic DM material IDs. |
| `workforce_hint` | Populates `workforce_hint`. Realistic role names and cert requirements for the workforce designer. |
| `moments_of_expertise` | Seed entries for `scenario.moments_of_expertise`. Brainstorm may add more when assigning story context. Field exists in both contracts intentionally. |
| `equipment_hints[]` | Used by Asset Sim (not directly by Brainstorm). Brainstorm passes the full research output to orchestrator which forwards to Asset Sim. |
| `pain_points[]` | Informs `scenario.pain_points_addressed`. High-confidence CRM pain points should appear as addressed pain points in the scenario story. |
| `terminology[]` | Provides customer-specific vocabulary to use in material names, order descriptions, and runbook talking points. |
| `competitive_context` | Informs `scenario.story` — the narrative can acknowledge the incumbent system being replaced. |
| `slug` | Propagates unchanged into brainstorm output `slug`. Brainstorm NEVER mints or modifies the slug. |
| `product.type` | Load-bearing for routing type selection (`discrete` → operation-per-step, `process` → phase-based, `repetitive` → rate-based). |
| `meta.demo_focus` | Directly maps to `scenario.focus` array. Research output's single focus value becomes the primary entry. |

> [!important] Flat fields vs rich objects
> Brainstorm output uses flat strings for `company` and `industry` (e.g. `"company": "yanmar"`). These are derived from the slug segments, NOT read from the research output's rich company/industry objects. The slug is the canonical identity carrier.
```

- [ ] **Step 3: Commit**

```bash
cd /Users/I584748/DM_MQTT
git add firmware/docs/superpowers/plans/2026-07-08-production-research-plan-a-contracts-and-docs.md
git commit -m "docs(network): add consumer notes to brainstorm-output-v1 contract"
```

---

### Task 3: Write ADR-009 — `production.steps[]` struct upgrade

**Files:**
- Create: `Mitchell/Projects/Agent Network/decisions/ADR-009-production-steps-struct.md`

- [ ] **Step 1: Write the ADR**

```markdown
---
type: adr
tags: [agent-network, adr, decision]
status: accepted
date: 2026-07-08
---

# ADR-009: `production.steps[]` replaces `processes[]`

## Status

Accepted.

## Context

The provisional `contracts/research-output-v1.md` stub listed a flat `processes[]` top-level array with no defined structure. When the Production Research brainstorm session designed the full contract, a richer nested structure was needed: each step requires `seq` (for cross-referencing from `equipment_hints[]` and `quality_issues_hints[]`), `typical_equipment[]`, `quality_checkpoints[]`, and `can_trigger_rework` (to help Demo Brainstorm decide whether to add a rework routing).

## Decision

Replace `processes[]` with `production` object containing `steps[]` with the following structure:

```json
{
  "seq": 1,
  "name": "string",
  "description": "string",
  "typical_equipment": ["string"],
  "quality_checkpoints": ["string"],
  "can_trigger_rework": true
}
```

The `production` object also carries `rework_common`, `rework_description`, and `batch_or_serial` — fields that belong with the process description, not scattered across the top level.

## Consequences

- **Positive:** `equipment_hints[].step_ref` and `quality_issues_hints[].step_ref` can resolve to specific steps, giving Demo Brainstorm a clean mapping from equipment to operation.
- **Positive:** `can_trigger_rework` on each step lets Demo Brainstorm automatically identify rework insertion points without heuristics.
- **Positive:** `batch_or_serial` moves next to the process it describes.
- **Negative:** Any consumer reading the old `processes[]` field must be updated. At time of writing, no consumer existed (stub only) — no migration needed.

## Files updated

- [[../contracts/research-output-v1]] — full schema rewrite
- [[../contracts/brainstorm-output-v1]] — consumer notes added
- [[../agents/production-research]] — I/O section updated

## Links

- [[ADR-008-network-wide-rules]] R1 (versioning), R4 (assumptions trail uses `field_path` that references `production.steps[]`)
```

- [ ] **Step 2: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "docs(network): add ADR-009 production.steps[] struct decision"
```

---

### Task 4: Write ADR-010 — Orchestrator + parallel sub-agent architecture

**Files:**
- Create: `Mitchell/Projects/Agent Network/decisions/ADR-010-orchestrator-parallel-subagents.md`

- [ ] **Step 1: Write the ADR**

```markdown
---
type: adr
tags: [agent-network, adr, decision]
status: accepted
date: 2026-07-08
---

# ADR-010: Orchestrator + 4 parallel research sub-agents

## Status

Accepted.

## Context

The Production Research Agent must do a deep dive across independent knowledge sources: the Mitchell vault (CRM + industry MOC + DM prior scenarios), the company's own public web presence, industry process standards, and the SAP DM customer story landscape. These four angles are genuinely independent — no source depends on another's output to proceed.

Three approaches were considered:

1. **Single-agent sequential** — one agent queries all sources in order. Simple but slow; web searches block on each other.
2. **Orchestrator + parallel sub-agents** — one orchestrator fans out four sub-agents simultaneously, merges results.
3. **Two-phase tiered** — quick Mitchell scan first; escalate to full web fan-out only if Mitchell is sparse. Reduces token cost for known customers.

## Decision

Use Approach 2: orchestrator + 4 parallel sub-agents.

Sub-agents:
- **Mitchell Reader** — CRM hub, meeting notes, industry MOC, DM demo prior-art scenarios, topic MOCs
- **Company Web Researcher** — company website, annual reports, LinkedIn, press releases
- **Industry Process Researcher** — standard process flow for this product type, typical equipment, regulatory context, common quality issues
- **SAP Angle Researcher** — DM capabilities relevant to this industry, existing customer stories, moments-of-expertise prior art, competitive landscape

Each returns a structured fact bundle. The orchestrator merges, applies conflict resolution, mints the slug, writes `research_output_v1.json` and `research-summary.md`.

Approach 3 was rejected: the "enough from Mitchell" threshold is hard to define reliably, and since deep dive is the always-on posture (design decision D1), the two-phase complexity yields no benefit.

## Consequences

- **Positive:** Wall-clock time = slowest single sub-agent, not sum of all four. Significant speed gain on deep-dive runs.
- **Positive:** Each angle gets full attention; no source starved by token budget of a sequential agent.
- **Positive:** Conflict resolution is explicit (orchestrator owns merge) rather than implicit (single agent silently overwrites).
- **Negative:** Token cost is higher than sequential. Acceptable given deep-dive posture.
- **Negative:** Assumption ID assignment must happen in the orchestrator (after merge), not in sub-agents. Sub-agents return unnumbered fact bundles with local assumption notes; orchestrator assigns `A001..A0NN`.

## Conflict resolution rule (load-bearing)

Mitchell facts sourced from direct customer interaction (`mitchell_crm`, `mitchell_meeting`) outrank web facts for the same company. Web facts from the company's own site outrank `inferred`. When Mitchell and web give conflicting production step counts or sequences, log both in `gaps_flagged[]`, use the Mitchell version, and set `confidence: low` on affected steps.

## Links

- [[ADR-008-network-wide-rules]] R3 (modes), R4 (assumptions trail), R8 (I/O shape)
- [[../agents/production-research]]
```

- [ ] **Step 2: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "docs(network): add ADR-010 orchestrator + parallel sub-agents decision"
```

---

### Task 5: Update `network-map.md` with Production Research row

**Files:**
- Modify: `Mitchell/Projects/Agent Network/network-map.md`

- [ ] **Step 1: Read the current file**

Read `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/network-map.md`.

- [ ] **Step 2: Add Production Research row**

Find the agent status table (or create one if absent) and add/update the Production Research row:

```markdown
| Agent | Version | Status | Min input version | Output contract |
|---|---|---|---|---|
| Production Research | 1 | plan-a complete | — (first in chain) | [[contracts/research-output-v1]] v1 |
| Demo Brainstorm | — | stub | research-output v1 | [[contracts/brainstorm-output-v1]] |
| Asset Sim | — | stub | brainstorm-output v1 | — |
| Build Demo | 1 | spec ready | brainstorm-output v1 | [[contracts/demoplan-v1]] |
```

If a table already exists, update it additively. If `network-map.md` does not yet have a table, add the section `## Agent status` with the table above.

- [ ] **Step 3: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "docs(network): add Production Research row to network-map"
```

---

### Task 6: Promote `agents/production-research.md` from stub to full doc

**Files:**
- Modify: `Mitchell/Projects/Agent Network/agents/production-research.md`

The current file is a stub. Replace it with the full agent doc per R11.

- [ ] **Step 1: Write the full agent doc**

```markdown
---
type: agent
tags: [agent-network, agents, production-research]
status: plan-a complete
spec: "~/.claude/plans/i-want-to-build-toasty-manatee.md"
prompt: "~/.claude/agents/production-research.md"
---

# Production Research Agent

## Purpose

Gathers real-world manufacturing context on a company, industry, and product before demo design begins. Mints the canonical slug. This is the **first agent in the chain** — nothing upstream, everything downstream depends on its output.

## Position in chain

```
Production Research  →  Demo Brainstorm  →  Build Demo
       (this)
```

## Invocation

```
Task(
  subagent_type: "production-research",
  prompt: "{\"company\": \"Yanmar\", \"mode\": \"interactive\"}"
)
```

Or with full brief (bypasses interactive questions):

```
Task(
  subagent_type: "production-research",
  prompt: "{\"company\": \"Yanmar\", \"product\": \"industrial diesel engine\", \"focus\": \"quality-rework\", \"presales_notes\": \"replacing in-house MES, audience is ops lead\", \"mode\": \"auto\"}"
)
```

## Input

Free-form JSON with:
- `company` (required): company name, any casing
- `product` (optional): target product line. If absent and mode=interactive, agent asks.
- `focus` (optional): `operator-ux|quality-rework|throughput|OEE|dispatching|monitoring`. If absent and mode=interactive, agent asks.
- `presales_notes` (optional): free text. If absent and mode=interactive, agent asks.
- `mode` (optional, default `interactive`): `interactive|auto`

## Output

Writes two files to `Mitchell/SAP Solutions/1 - DM/Demos/<slug>/`:
- `research_output_v1.json` — machine-readable contract (schema: [[../contracts/research-output-v1]])
- `research-summary.md` — human-readable summary with gap callouts

Returns R8 envelope: `{status, diff[], assumptions_add[], gaps_add[]}`.

## Sub-agents called

| Sub-agent | Prompt file | Purpose |
|---|---|---|
| Mitchell Reader | `~/.claude/agents/production-research-mitchell-reader.md` | CRM, meeting notes, industry MOC, DM prior scenarios |
| Company Web Researcher | `~/.claude/agents/production-research-company-web.md` | Company website, annual reports, press |
| Industry Process Researcher | `~/.claude/agents/production-research-industry-process.md` | Standard process flow, equipment, quality issues, regulatory |
| SAP Angle Researcher | `~/.claude/agents/production-research-sap-angle.md` | DM capabilities, customer stories, moments-of-expertise |

All four run in parallel via the Workflow tool.

## Prereqs

```
PREFLIGHT:
- [ ] Mitchell vault readable at /Users/.../Mitchell
- [ ] Web search available
- [ ] No slug collision in Mitchell/SAP Solutions/1 - DM/Demos/
```

## Output locations

| File | Location |
|---|---|
| `research_output_v1.json` | `Mitchell/SAP Solutions/1 - DM/Demos/<slug>/` |
| `research-summary.md` | `Mitchell/SAP Solutions/1 - DM/Demos/<slug>/` |

No writes outside these paths (R6).

## Key design decisions

- [[../decisions/ADR-005-slug-identity]] — slug minting rules
- [[../decisions/ADR-008-network-wide-rules]] — R1–R11 binding
- [[../decisions/ADR-009-production-steps-struct]] — why `production.steps[]` not `processes[]`
- [[../decisions/ADR-010-orchestrator-parallel-subagents]] — why parallel fan-out

## Idempotency

If `research_output_v1.json` already exists in the slug folder, the agent overwrites it only when invoked with `force=true`. Otherwise it returns `{status: ok, diff: []}` with a warning in `gaps_add`.

## Golden fixtures

See `[[../golden-fixtures/README]]` — Yanmar, Umicore, Barco fixtures (Plan C).
```

- [ ] **Step 2: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "docs(network): promote production-research agent doc from stub to full"
```

---

## Self-review

**Spec coverage check:**

| Spec section | Covered by task |
|---|---|
| Change propagation table | Task 1 (research-output-v1 rewrite), Task 2 (brainstorm consumer notes) |
| ADR-009 | Task 3 |
| ADR-010 | Task 4 |
| network-map.md update | Task 5 |
| agents/production-research.md promotion from stub | Task 6 |
| R11 documentation duty | All tasks collectively |

No gaps found.

**Placeholder scan:** No TBDs. All file content is complete. All commit messages are specific.

**Type consistency:** No code types — all Markdown. Wikilinks consistent across all files (`[[decisions/ADR-009-production-steps-struct]]` format used throughout).
