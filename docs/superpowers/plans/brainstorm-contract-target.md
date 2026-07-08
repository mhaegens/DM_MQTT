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
    "rework_rationale": "string: which tri-signals triggered the decision",
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
- `product.short`: kebab-case string, minimum 3 characters, maximum 20 characters (matches slug segment format).
- `production_flow.rework_from_to`: 2-element array `[from_step_name, to_step_name]` or `[null, null]` when `has_rework_loop = false`.
- `moments_of_expertise`: minimum 2, maximum 3 entries.
- `meta.assumptions[].id`: format `A\d{3}`, unique within document.
- `meta.gaps_flagged[].id`: format `G\d{3}`, unique within document.
- Auto-mode cap: max 30 combined entries across `meta.assumptions[]` + `meta.gaps_flagged[]` (R4 of ADR-008).
- All string values in `product`, `scenario`, `production_flow`: printable ASCII only (R5 of ADR-008).
