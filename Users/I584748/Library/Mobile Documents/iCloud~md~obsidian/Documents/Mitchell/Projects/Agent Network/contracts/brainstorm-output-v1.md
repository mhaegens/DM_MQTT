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
- Asset Sim receives the full research output forwarded by orchestrator, not the brainstorm output
