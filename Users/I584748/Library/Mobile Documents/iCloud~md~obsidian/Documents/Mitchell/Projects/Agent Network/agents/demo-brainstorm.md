---
type: agent
tags: [agent-network, agents, demo-brainstorm]
status: v1
date: 2026-07-08
---

# Demo Brainstorm

## Purpose

Creative step in the demo chain. Reads `research_output_v1.json` and presales intent, produces `brainstorm_output_v1.json` that `dm-demo-orchestrator` consumes directly.

## Chain position

Production Research → **Demo Brainstorm** → Build Demo → runnable demo

## Inputs

- [[../contracts/research-output-v1|Research Output v1]] — manufacturing facts
- Presales intent: interactive Q&A (four questions in one front-load block) or auto defaults

## Output

- [[../contracts/brainstorm-output-v1|Brainstorm Output v1]] — scenario spec

## Consumed by

- Build Demo ([[build-demo]]) via dm-demo-orchestrator
- Asset Sim (receives full research output forwarded by orchestrator, not the brainstorm output directly)

## Architecture

Single agent file at `~/.claude/agents/demo-brainstorm.md`. Backed by shared pack files in `~/.claude/agents/dm-demo-shared/`:
- `brainstorm-contract-target.md` — canonical output schema
- `order-type-rules.md` — product.type to order_type decision table

Six sequential passes:
1. Presales intent (front-load Q&A or auto defaults)
2. Scenario frame (focus array, tri-signal rework model)
3. Production flow mapping (research steps to DM operation hints)
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
| D8 | product.order_type decoupled from product.type (see [[../decisions/ADR-011-order-type]]) |

## Mode

`may_prompt: true` (see `~/.claude/agents/dm-demo-shared/mode-contract.md`).

## Spec

`~/.claude/plans/context-you-must-load-sparkling-brooks.md`

## Golden fixtures

| Fixture | product.type | order_type | has_rework_loop | Primary focus | Duration |
|---|---|---|---|---|---|
| acme-food-noodles | process | process_order | true | operator-ux | 45 min |
| acme-plastics-caps | discrete | production_order | false | dispatching | 30 min |
| acme-electronics-pcb | discrete | production_order | false | monitoring | 60 min |
