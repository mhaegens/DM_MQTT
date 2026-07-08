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
