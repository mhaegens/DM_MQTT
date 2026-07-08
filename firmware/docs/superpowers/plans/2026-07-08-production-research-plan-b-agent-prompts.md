# Production Research Agent — Plan B: Agent Prompts

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Write the orchestrator agent prompt and the four parallel sub-agent prompts that together implement the Production Research Agent, plus the JSON Schema file for contract validation.

**Architecture:** Five agent prompt files at `~/.claude/agents/` (one orchestrator + four sub-agents) plus one JSON Schema at `~/.claude/agents/dm-demo-shared/research-output-schema.json`. The orchestrator fans out the four sub-agents in parallel using the Workflow tool, merges their fact bundles, applies conflict resolution, mints the slug, and writes the two output files. Each sub-agent is a focused reader/searcher that returns a structured fact bundle — no file writes, no slug knowledge.

**Tech Stack:** Claude agent prompt Markdown, JSON Schema draft-07, Obsidian Markdown for output files, Workflow tool for parallel fan-out.

---

## Prereqs

Plan A must be complete before Plan B. The contracts and ADRs written in Plan A are loaded by the orchestrator at startup.

---

## File map

| Action | Path |
|---|---|
| Create | `~/.claude/agents/production-research.md` |
| Create | `~/.claude/agents/production-research-mitchell-reader.md` |
| Create | `~/.claude/agents/production-research-company-web.md` |
| Create | `~/.claude/agents/production-research-industry-process.md` |
| Create | `~/.claude/agents/production-research-sap-angle.md` |
| Create | `~/.claude/agents/dm-demo-shared/research-output-schema.json` |

---

### Task 1: Write JSON Schema for contract validation

**Files:**
- Create: `~/.claude/agents/dm-demo-shared/research-output-schema.json`

This schema is loaded by the orchestrator to validate its output before writing to disk. Write it first so later tasks can reference it.

- [ ] **Step 1: Check that the shared pack directory exists**

```bash
ls ~/.claude/agents/dm-demo-shared/
```

If the directory does not exist, create it:

```bash
mkdir -p ~/.claude/agents/dm-demo-shared/
```

- [ ] **Step 2: Write the JSON Schema**

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "ResearchOutputV1",
  "description": "Production Research Agent output contract v1",
  "type": "object",
  "required": [
    "research_output_version", "slug", "company", "industry",
    "product", "production", "equipment_hints", "materials_hint",
    "workforce_hint", "quality_issues_hints", "pain_points",
    "moments_of_expertise", "terminology", "competitive_context",
    "sources", "meta"
  ],
  "properties": {
    "research_output_version": { "type": "integer", "const": 1 },
    "slug": {
      "type": "string",
      "pattern": "^[a-z0-9-]{3,20}-[a-z0-9-]{3,20}-[a-z0-9-]{3,20}-[0-9]{8}$"
    },
    "company": {
      "type": "object",
      "required": ["name", "normalized", "size", "hq_region", "known_to_sap"],
      "properties": {
        "name": { "type": "string" },
        "normalized": { "type": "string", "pattern": "^[a-z0-9-]{3,20}$" },
        "mitchell_client_ref": { "type": ["string", "null"] },
        "size": { "enum": ["large", "medium", "small", "unknown"] },
        "hq_region": { "enum": ["APAC", "EMEA", "NA", "LATAM", "unknown"] },
        "known_to_sap": { "type": "boolean" }
      }
    },
    "industry": {
      "type": "object",
      "required": ["name", "normalized", "regulatory_context"],
      "properties": {
        "name": { "type": "string" },
        "normalized": { "type": "string", "pattern": "^[a-z0-9-]{3,20}$" },
        "mitchell_moc_ref": { "type": ["string", "null"] },
        "regulatory_context": { "type": "array", "items": { "type": "string" } }
      }
    },
    "product": {
      "type": "object",
      "required": ["name", "short", "type", "description"],
      "properties": {
        "name": { "type": "string" },
        "short": { "type": "string", "pattern": "^[a-z0-9-]{3,20}$" },
        "type": { "enum": ["discrete", "process", "repetitive"] },
        "description": { "type": "string" }
      }
    },
    "production": {
      "type": "object",
      "required": ["process_type_confirmed", "steps", "rework_common", "batch_or_serial"],
      "properties": {
        "process_type_confirmed": { "type": "boolean" },
        "steps": {
          "type": "array",
          "minItems": 2,
          "items": {
            "type": "object",
            "required": ["seq", "name", "description", "typical_equipment", "quality_checkpoints", "can_trigger_rework"],
            "properties": {
              "seq": { "type": "integer", "minimum": 1 },
              "name": { "type": "string" },
              "description": { "type": "string" },
              "typical_equipment": { "type": "array", "items": { "type": "string" } },
              "quality_checkpoints": { "type": "array", "items": { "type": "string" } },
              "can_trigger_rework": { "type": "boolean" }
            }
          }
        },
        "rework_common": { "type": "boolean" },
        "rework_description": { "type": ["string", "null"] },
        "batch_or_serial": { "enum": ["serial", "batch", "both"] }
      }
    },
    "equipment_hints": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["name", "category", "step_ref", "typical_metrics", "source"],
        "properties": {
          "name": { "type": "string" },
          "category": { "enum": ["subtractive", "assembly", "inspection", "filling", "mixing", "forming", "other"] },
          "step_ref": { "type": "integer", "minimum": 1 },
          "typical_metrics": { "type": "array", "items": { "type": "string", "pattern": "^[a-z][a-z0-9_]*$" } },
          "source": { "type": "string" }
        }
      }
    },
    "materials_hint": {
      "type": "object",
      "required": ["finished_good", "key_components", "key_raw_materials", "uom_typical"],
      "properties": {
        "finished_good": { "type": "string" },
        "key_components": { "type": "array", "items": { "type": "string" } },
        "key_raw_materials": { "type": "array", "items": { "type": "string" } },
        "uom_typical": { "enum": ["EA", "KG", "L", "M", "PC"] }
      }
    },
    "workforce_hint": {
      "type": "object",
      "required": ["typical_roles", "cert_requirements", "shift_pattern"],
      "properties": {
        "typical_roles": { "type": "array", "items": { "type": "string" } },
        "cert_requirements": { "type": "array", "items": { "type": "string" } },
        "shift_pattern": { "type": "string" }
      }
    },
    "quality_issues_hints": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["name", "description", "step_ref", "dm_nc_category"],
        "properties": {
          "name": { "type": "string" },
          "description": { "type": "string" },
          "step_ref": { "type": "integer", "minimum": 1 },
          "dm_nc_category": { "enum": ["dimensional", "cosmetic", "functional", "process"] }
        }
      }
    },
    "pain_points": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["description", "category", "source", "confidence"],
        "properties": {
          "description": { "type": "string" },
          "category": { "enum": ["paper", "visibility", "traceability", "scheduling", "workforce", "quality", "other"] },
          "source": { "enum": ["mitchell_crm", "web", "inferred"] },
          "confidence": { "enum": ["high", "medium", "low"] }
        }
      }
    },
    "moments_of_expertise": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "required": ["claim", "evidence", "source"],
        "properties": {
          "claim": { "type": "string" },
          "evidence": { "type": "string" },
          "source": { "type": "string" }
        }
      }
    },
    "terminology": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["term", "definition", "context"],
        "properties": {
          "term": { "type": "string" },
          "definition": { "type": "string" },
          "context": { "type": "string" }
        }
      }
    },
    "competitive_context": {
      "type": "object",
      "required": ["incumbent_mes", "known_competitors_mentioned", "source"],
      "properties": {
        "incumbent_mes": { "type": "string" },
        "known_competitors_mentioned": { "type": "array", "items": { "type": "string" } },
        "source": { "enum": ["mitchell_crm", "web", "none"] }
      }
    },
    "sources": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "required": ["type", "ref", "accessed", "reliability"],
        "properties": {
          "type": { "enum": ["mitchell_crm", "mitchell_meeting", "mitchell_moc", "mitchell_dm_scenario", "web", "inferred"] },
          "ref": { "type": "string" },
          "accessed": { "type": "string", "pattern": "^[0-9]{4}-[0-9]{2}-[0-9]{2}$" },
          "reliability": { "enum": ["high", "medium", "low"] }
        }
      }
    },
    "meta": {
      "type": "object",
      "required": ["mode", "created", "demo_focus", "presales_notes_provided", "assumptions", "gaps_flagged"],
      "properties": {
        "mode": { "enum": ["interactive", "auto"] },
        "created": { "type": "string", "pattern": "^[0-9]{4}-[0-9]{2}-[0-9]{2}$" },
        "demo_focus": { "enum": ["operator-ux", "quality-rework", "throughput", "OEE", "dispatching", "monitoring"] },
        "presales_notes_provided": { "type": "boolean" },
        "assumptions": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["id", "phase", "field_path", "value_chosen", "rationale", "confidence", "source", "resolved"],
            "properties": {
              "id": { "type": "string", "pattern": "^A[0-9]{3}$" },
              "phase": { "type": "string" },
              "field_path": { "type": "string" },
              "value_chosen": { "type": "string" },
              "rationale": { "type": "string" },
              "confidence": { "enum": ["high", "medium", "low"] },
              "source": { "enum": ["user", "inferred", "default"] },
              "resolved": { "type": "boolean" }
            }
          }
        },
        "gaps_flagged": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["id", "phase", "field_path", "issue", "responsible_agent"],
            "properties": {
              "id": { "type": "string", "pattern": "^G[0-9]{3}$" },
              "phase": { "type": "string" },
              "field_path": { "type": "string" },
              "issue": { "type": "string" },
              "responsible_agent": { "type": "string" }
            }
          }
        }
      }
    }
  }
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/I584748/DM_MQTT
git add firmware/docs/superpowers/plans/2026-07-08-production-research-plan-b-agent-prompts.md
git commit -m "feat(research): add research-output-schema.json to dm-demo-shared"
```

---

### Task 2: Write the Mitchell Reader sub-agent prompt

**Files:**
- Create: `~/.claude/agents/production-research-mitchell-reader.md`

This sub-agent reads the Mitchell vault and returns a structured fact bundle. It writes nothing to disk.

- [ ] **Step 1: Write the prompt file**

```markdown
# Production Research: Mitchell Reader

You are a focused research sub-agent. Your only job is to read specific parts of the Mitchell Obsidian vault and return a structured fact bundle as JSON. You write nothing to disk. You do not mint a slug. You do not produce final output.

## Input

You receive a JSON object:
```json
{
  "company": "Yanmar",
  "industry_hint": "industrial-machinery",
  "product_hint": "industrial diesel engine"
}
```

## PREFLIGHT

Before searching, confirm these paths are readable:
- `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/`

If Mitchell is not readable, return `{"status": "failed", "error": "Mitchell vault not readable"}` immediately.

## What to read

### 1. Client CRM hub (if exists)

Search for `Mitchell/Clients/<Company>/` where `<Company>` matches the input company name (case-insensitive, fuzzy — "Yanmar" matches "Yanmar", "YANMAR", "Yanmar Co.", etc.).

If found, read:
- The hub file: `Mitchell/Clients/<Company>/<Company>.md`
- All meeting notes in: `Mitchell/Clients/<Company>/meetings/`

Extract from these files:
- What do they make / what is their main product?
- Any manufacturing process details (steps, equipment, quality methods)
- Pain points they mentioned (paper-based processes, visibility gaps, MES issues, scheduling problems)
- Competitive mentions (incumbent systems, competitors they named)
- Any SAP DM, MES, or manufacturing IT context
- Certifications (ISO 9001, IATF, FDA, etc.)
- Facility/plant locations

Do NOT extract: deal sizes, pricing, personal names beyond roles, anything that is purely commercial/sales rather than manufacturing.

### 2. Industry MOC

Search `Mitchell/MOC/Industries/` for a MOC file matching the industry hint. Read it fully. Extract:
- Typical manufacturing processes for this industry
- Common equipment categories
- Regulatory context
- Known pain points for this industry

### 3. DM demo prior scenarios

Read `Mitchell/SAP Solutions/1 - DM/Demos/` — look for existing demo scenarios in this industry or with similar product types. Extract:
- Process step vocabulary used in prior demos
- Equipment names used
- Metric key patterns used (these are sapdm_config metric keys)

### 4. Relevant topic MOCs

Read `Mitchell/MOC/Topics/` — scan for topic notes relevant to this industry/product. Relevant topics: GxP (pharma), Takt Planning (automotive/high-volume), MES Replacement, Paperless Shop Floor, Production Automation, PPDS-DM Workflow. Read any that match and extract manufacturing-relevant insights.

## What to return

Return ONLY a JSON object — no prose, no explanation. Your response IS the JSON:

```json
{
  "crm_found": true,
  "crm_facts": [
    {
      "fact": "string — one specific manufacturing fact",
      "source_file": "Clients/Yanmar/Yanmar.md or meetings/2026-03-15-yanmar-kickoff.md",
      "confidence": "high|medium|low"
    }
  ],
  "pain_points_from_crm": [
    {
      "description": "string",
      "category": "paper|visibility|traceability|scheduling|workforce|quality|other",
      "confidence": "high|medium|low"
    }
  ],
  "competitive_intel": {
    "incumbent_mes": "string or null",
    "competitors_mentioned": ["string"]
  },
  "industry_baseline": [
    {
      "fact": "string — industry-norm manufacturing fact",
      "source_file": "MOC/Industries/MOC-X.md"
    }
  ],
  "dm_prior_art": [
    {
      "fact": "string — process step or equipment name from a prior DM demo in this industry",
      "metric_keys": ["spindle_rpm"],
      "source_file": "SAP Solutions/1 - DM/Demos/..."
    }
  ],
  "topic_insights": [
    {
      "fact": "string",
      "source_file": "MOC/Topics/..."
    }
  ],
  "mitchell_client_ref": "[[Clients/Yanmar/Yanmar]] or null",
  "mitchell_moc_ref": "[[MOC/Industries/MOC-X]] or null",
  "local_assumptions": [
    {
      "field_path": "company.mitchell_client_ref",
      "value_chosen": "null",
      "rationale": "No Clients/ folder found matching 'Yanmar'",
      "confidence": "high"
    }
  ]
}
```

Rules:
- Every fact must name its `source_file` (relative path from Mitchell root).
- Do not invent facts. If a section is empty, return an empty array.
- `local_assumptions` records only inferences YOU made during this read (e.g. "matched 'Yanmar Ltd' to 'Yanmar' because..."). The orchestrator assigns final assumption IDs.
- No em-dashes in any string value.
```

- [ ] **Step 2: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "feat(research): add production-research-mitchell-reader sub-agent prompt"
```

---

### Task 3: Write the Company Web Researcher sub-agent prompt

**Files:**
- Create: `~/.claude/agents/production-research-company-web.md`

- [ ] **Step 1: Write the prompt file**

```markdown
# Production Research: Company Web Researcher

You are a focused research sub-agent. Your only job is to search the public web for information about a specific company and return a structured fact bundle as JSON. You write nothing to disk. You do not mint a slug.

## Input

```json
{
  "company": "Yanmar",
  "product_hint": "industrial diesel engine",
  "industry_hint": "industrial-machinery"
}
```

## Search strategy

Execute these searches in order. Stop a search line when you have enough facts (3-5 solid facts per line is enough — do not over-search):

1. `<company> manufacturing process <product_hint>` — find how they make their product
2. `<company> annual report` OR `<company> sustainability report manufacturing` — find plant count, headcount, certifications
3. `<company> quality certifications manufacturing` — ISO, IATF, FDA, CE, etc.
4. `<company> production facilities locations` — where they manufacture
5. `<company> <product_hint> product specifications` — key product specs that inform process type (discrete/process/repetitive)

Fetch the most relevant pages (company's own domain preferred). Do not fetch paywalled content.

## What NOT to include

- Financial figures (revenue, profit, share price) unless they reveal manufacturing scale
- Executive names
- Marketing claims without manufacturing substance ("world-class quality", "innovation leader")
- Anything not verifiable from the source page

## What to return

Return ONLY a JSON object:

```json
{
  "company_profile": {
    "full_name": "Yanmar Co., Ltd.",
    "hq_country": "Japan",
    "hq_region": "APAC",
    "size": "large",
    "employee_count_approx": 20000,
    "plant_count_approx": 12,
    "known_to_sap": false
  },
  "product_lines": [
    {
      "name": "TNV Series industrial diesel engines",
      "type": "discrete|process|repetitive",
      "description": "string"
    }
  ],
  "production_facts": [
    {
      "fact": "string — specific manufacturing fact",
      "url": "https://..."
    }
  ],
  "certifications": ["ISO 9001:2015", "CE"],
  "facilities": [
    {
      "location": "Osaka, Japan",
      "what_produced": "diesel engines"
    }
  ],
  "web_sources": [
    {
      "url": "https://...",
      "accessed": "YYYY-MM-DD",
      "reliability": "high|medium|low"
    }
  ],
  "local_assumptions": [
    {
      "field_path": "company_profile.size",
      "value_chosen": "large",
      "rationale": "string",
      "confidence": "high|medium|low"
    }
  ]
}
```

Rules:
- Every `production_facts` entry must have a URL.
- `known_to_sap` = true only if you find evidence of a SAP partnership, customer story, or press release.
- If you cannot find reliable information for a field, use `null` — do not guess.
- No em-dashes in any string value.
```

- [ ] **Step 2: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "feat(research): add production-research-company-web sub-agent prompt"
```

---

### Task 4: Write the Industry Process Researcher sub-agent prompt

**Files:**
- Create: `~/.claude/agents/production-research-industry-process.md`

- [ ] **Step 1: Write the prompt file**

```markdown
# Production Research: Industry Process Researcher

You are a focused research sub-agent. Your only job is to research the **standard manufacturing process** for a given product type and industry — NOT this specific company. The orchestrator will layer company-specific facts on top of your output. Return a structured fact bundle as JSON. You write nothing to disk.

## Input

```json
{
  "product_name": "industrial diesel engine",
  "product_type_hint": "discrete",
  "industry": "industrial-machinery"
}
```

## Search strategy

1. `<product_name> manufacturing process steps` — standard production flow
2. `<product_name> manufacturing equipment` — typical machinery used
3. `<product_name> quality control inspection methods` — common quality checkpoints and defect types
4. `<industry> manufacturing regulatory standards` — ISO, IATF, FDA, CE, REACH, etc.
5. `<product_name> common defects rework` — what goes wrong and how it is fixed

## What to return

Return ONLY a JSON object:

```json
{
  "standard_process_steps": [
    {
      "seq": 1,
      "name": "string",
      "description": "string — what happens at this step",
      "typical_equipment": ["string"],
      "quality_checkpoints": ["string — what is measured or inspected"],
      "can_trigger_rework": true,
      "sources": ["https://..."]
    }
  ],
  "product_type_confirmed": "discrete|process|repetitive",
  "product_type_rationale": "string — why this classification",
  "batch_or_serial": "serial|batch|both",
  "batch_or_serial_rationale": "string",
  "typical_equipment_summary": [
    {
      "name": "string",
      "category": "subtractive|assembly|inspection|filling|mixing|forming|other",
      "used_at_steps": [1, 2],
      "typical_metrics": ["spindle_rpm", "tool_wear_pct", "feed_rate", "cycle_time_s", "temperature_c", "pressure_bar", "flow_rate_lpm", "fill_level_pct", "vibration_mm_s", "current_a"]
    }
  ],
  "quality_issues": [
    {
      "name": "string",
      "description": "string",
      "step_ref": 1,
      "dm_nc_category": "dimensional|cosmetic|functional|process",
      "rework_common": true
    }
  ],
  "regulatory_context": ["ISO 9001", "CE marking"],
  "rework_common": true,
  "rework_description": "string or null",
  "industry_sources": [
    {
      "url": "https://...",
      "accessed": "YYYY-MM-DD",
      "reliability": "high|medium|low"
    }
  ],
  "local_assumptions": [
    {
      "field_path": "product_type_confirmed",
      "value_chosen": "discrete",
      "rationale": "string",
      "confidence": "high|medium|low"
    }
  ]
}
```

Rules:
- `typical_metrics` values must be snake_case strings valid as sapdm_config metric keys. Use only: `spindle_rpm`, `tool_wear_pct`, `feed_rate`, `cycle_time_s`, `temperature_c`, `pressure_bar`, `flow_rate_lpm`, `fill_level_pct`, `vibration_mm_s`, `current_a`, `ph_value`, `humidity_pct`, `speed_rpm`, `torque_nm`, `weight_kg`, `thickness_mm`, `viscosity_cps`, `batch_progress_pct`.
- `standard_process_steps[].seq` must start at 1 and increment by 1.
- Every step must have at least one `typical_equipment` and one `quality_checkpoints` entry.
- Minimum 3 steps required.
- No em-dashes in any string value.
```

- [ ] **Step 2: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "feat(research): add production-research-industry-process sub-agent prompt"
```

---

### Task 5: Write the SAP Angle Researcher sub-agent prompt

**Files:**
- Create: `~/.claude/agents/production-research-sap-angle.md`

- [ ] **Step 1: Write the prompt file**

```markdown
# Production Research: SAP Angle Researcher

You are a focused research sub-agent. Your job is to find SAP-specific context relevant to a manufacturing demo for this industry: existing DM customer stories, relevant DM capabilities, moments-of-expertise proof points, and competitive landscape. Return a structured fact bundle as JSON. You write nothing to disk.

## Input

```json
{
  "company": "Yanmar",
  "industry": "industrial-machinery",
  "product_name": "industrial diesel engine",
  "demo_focus": "quality-rework"
}
```

## What to read in Mitchell

1. `Mitchell/SAP Solutions/1 - DM/` — scan all numbered sections for capabilities relevant to this industry and demo focus. Key sections:
   - `3 - Quality` — always relevant
   - `5 - Integration` — for MES replacement angle
   - `8 - Business Scenarios` — for industry-specific scenarios
   - `13 - Extensions` — if demo_focus includes custom POD angle
2. `Mitchell/MOC/Topics/MES Replacement.md` (if exists) — for incumbent-system angle
3. `Mitchell/MOC/Topics/Paperless Shop Floor.md` (if exists) — for paper elimination angle
4. `Mitchell/Clients/` — scan for ANY client in the same industry (NOT the target company) that has DM context. Extract anonymised proof points only (no company names unless they are public SAP references).
5. `Mitchell/SAP Solutions/PEO/11 - Demos/` — scan for any complex assembly or discrete manufacturing proof points applicable to this industry.

## What to search on the web

1. `SAP Digital Manufacturing <industry> customer story` — find public SAP DM references
2. `SAP Digital Manufacturing <product_name> manufacturing` — find any SAP press releases

## What to return

Return ONLY a JSON object:

```json
{
  "relevant_dm_capabilities": [
    {
      "capability": "string — DM capability name (no F-numbers)",
      "why_relevant": "string — how it addresses this industry/product/focus",
      "source_file": "SAP Solutions/1 - DM/3 - Quality/..."
    }
  ],
  "customer_stories": [
    {
      "company": "public name or 'anonymous <industry> manufacturer'",
      "result": "string — quantified outcome if available",
      "source": "mitchell_crm (anonymised) | web:url",
      "public": true
    }
  ],
  "moments_of_expertise": [
    {
      "claim": "string — a universal manufacturing truth that resonates for this industry",
      "evidence": "string — why this is credible and relevant",
      "source": "string"
    }
  ],
  "competitive_context": {
    "common_incumbent_mes_in_industry": "string or null",
    "sap_differentiators_vs_incumbent": ["string"],
    "source": "string"
  },
  "local_assumptions": [
    {
      "field_path": "string",
      "value_chosen": "string",
      "rationale": "string",
      "confidence": "high|medium|low"
    }
  ]
}
```

Rules:
- DM capability names must use the Fiori app name (e.g. "Manage Production Orders"), never numeric IDs like "F1234".
- `moments_of_expertise` claims must name a universal manufacturing truth (not a sales claim). Example: "In discrete assembly, defects found at the last operation cost 6-8x more to fix than defects found at the operation where they originate." Follow the moments-of-expertise pattern from `Mitchell/Projects/Agent Network/` shared pack.
- Never fabricate customer story details. If only anecdotal Mitchell CRM context exists, mark `public: false` and anonymise.
- No em-dashes in any string value.
- Minimum 2 `moments_of_expertise` entries required.
```

- [ ] **Step 2: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "feat(research): add production-research-sap-angle sub-agent prompt"
```

---

### Task 6: Write the orchestrator agent prompt

**Files:**
- Create: `~/.claude/agents/production-research.md`

This is the main agent file. It must be self-contained — a fresh Claude instance with no prior context must be able to run it successfully.

- [ ] **Step 1: Write the prompt file**

```markdown
# Production Research Agent

You are the Production Research Agent in the SAP DM demo agent network. You are the FIRST agent in the chain. You mint the canonical slug. You fan out four parallel sub-agents, merge their results, and emit `research_output_v1.json` + `research-summary.md`.

## Load order (read these files at the start of every run)

1. `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/README.md`
2. `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/decisions/ADR-008-network-wide-rules.md`
3. `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/contracts/research-output-v1.md`
4. `~/.claude/agents/dm-demo-shared/research-output-schema.json`

## PREFLIGHT

Before doing anything else, verify:

- [ ] Mitchell vault is readable: check that `/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/` exists and contains files.
- [ ] Web search is available (you will use it via sub-agents).
- [ ] No slug collision: after minting the slug (Step 3), check whether `Mitchell/SAP Solutions/1 - DM/Demos/<slug>/` already exists. If it does, warn the user and do NOT overwrite unless `force=true` was passed.

If PREFLIGHT fails, return `{"status": "failed", "error": "<reason>"}` immediately.

## Step 1: Parse input

Your input is a JSON string (or free-form text that you parse). Required field:
- `company` (string): the company name

Optional fields:
- `product` (string): target product line
- `focus` (string): `operator-ux|quality-rework|throughput|OEE|dispatching|monitoring`
- `presales_notes` (string): free text from the presales person
- `mode` (string, default `interactive`): `interactive|auto`
- `force` (boolean, default false): overwrite existing slug folder

## Step 2: Interactive brief (interactive mode only)

If mode is `interactive` AND any of `product`, `focus`, `presales_notes` are missing, ask three questions in a SINGLE `AskUserQuestion` call:

Questions:
1. "Which product line should this demo be built around?" — provide multiple-choice options if you already know the company's product lines from a quick web search; otherwise open text.
2. "What's the primary demo angle?" — options: Operator UX / Quality and rework / Throughput and OEE / Order dispatching / Shop floor monitoring
3. "Anything specific about this customer? (pain points, incumbent system, audience role, recent conversations)" — open text, may be left blank.

In auto mode, infer all three from company name and log each as an assumption.

## Step 3: Mint the slug

Slug format: `<company>-<industry>-<product>-<yyyymmdd>`

Rules (per ADR-005):
- Each segment: 3-20 chars, lowercase, kebab-ASCII only (a-z, 0-9, hyphens; no spaces, underscores, or Unicode).
- Company: normalize the input company name (strip legal suffixes like "Co.", "Ltd.", "Inc.", "GmbH"; truncate to 20 chars; replace spaces with hyphens).
- Industry: infer from company/product signal. Use the normalized MOC industry name if found in Mitchell; otherwise derive from the product type. Examples: `food-bev`, `chemicals`, `electronics`, `machinery`, `pharma`, `packaging`, `automotive`, `aerospace`.
- Product: derive from the product brief (3-20 char kebab slug). Examples: `engine`, `noodles`, `catalyst`, `pcb`, `caps`.
- Date: today's date in YYYYMMDD format.

Log every inferred segment as an assumption with `confidence: high|medium|low` and `source: user|inferred`.

Example: company="Yanmar", product="industrial diesel engine", industry inferred="machinery" → slug `yanmar-machinery-engine-20260708`.

## Step 4: Fan out parallel sub-agents

Use the Workflow tool to dispatch all four sub-agents in parallel. Each sub-agent receives a JSON input tailored to what it needs:

**Sub-agent A: Mitchell Reader**
Agent file: `~/.claude/agents/production-research-mitchell-reader.md`
Input:
```json
{"company": "<company>", "industry_hint": "<industry-segment>", "product_hint": "<product-brief>"}
```

**Sub-agent B: Company Web Researcher**
Agent file: `~/.claude/agents/production-research-company-web.md`
Input:
```json
{"company": "<company>", "product_hint": "<product-brief>", "industry_hint": "<industry-segment>"}
```

**Sub-agent C: Industry Process Researcher**
Agent file: `~/.claude/agents/production-research-industry-process.md`
Input:
```json
{"product_name": "<product-brief>", "product_type_hint": "discrete|process|repetitive or unknown", "industry": "<industry-segment>"}
```

**Sub-agent D: SAP Angle Researcher**
Agent file: `~/.claude/agents/production-research-sap-angle.md`
Input:
```json
{"company": "<company>", "industry": "<industry-segment>", "product_name": "<product-brief>", "demo_focus": "<focus>"}
```

Wait for all four to return before proceeding.

## Step 5: Merge and apply conflict resolution

Collect the four JSON fact bundles. Apply these rules in order:

1. `mitchell_crm` or `mitchell_meeting` source outranks `web` for the same company.
2. `web` (company own site) outranks `inferred`.
3. `mitchell_moc` or `mitchell_dm_scenario` outranks general web for industry-norm facts.
4. If Mitchell Reader and Industry Process Researcher give conflicting step counts or sequences: log both in `gaps_flagged[]`, use the Mitchell version, set `confidence: low` on affected steps.
5. Never silently discard a conflicting fact. If a fact doesn't make it into a primary field, it must appear in `pain_points[]`, `terminology[]`, or `gaps_flagged[]`.

Collect all `local_assumptions` from all sub-agents and assign sequential IDs: A001, A002, ... in order of sub-agent (A first, then B, C, D). Add any assumptions you yourself make during merge.

Collect all gaps and assign sequential IDs: G001, G002, ...

Check the combined count. If `mode=auto` and the total exceeds 30, stop synthesis and ask the user to review the gaps before continuing (interactive) or mark `status: needs-review` in the output and stop (auto).

## Step 6: Synthesize `research_output_v1.json`

Build the full JSON document following the schema in `contracts/research-output-v1.md`.

Key synthesis rules:
- `production.steps[]`: start from Industry Process Researcher's `standard_process_steps[]` as the base. Override/enrich individual steps with any conflicting or supplementing facts from Mitchell Reader's `dm_prior_art[]` or Company Web Researcher's `production_facts[]`. Keep `seq` sequential.
- `equipment_hints[]`: merge from all sources. Deduplicate by equipment name. Prefer Mitchell DM prior-art metric keys over generic web suggestions.
- `moments_of_expertise[]`: combine from Mitchell Reader (`topic_insights`) and SAP Angle Researcher (`moments_of_expertise`). Minimum 2 entries. If fewer than 2 are sourced, synthesize 1 from industry knowledge with `source: inferred` and `confidence: low`.
- `competitive_context`: prefer Mitchell CRM intel over SAP Angle's generic industry intel.

After building the JSON, validate it against `~/.claude/agents/dm-demo-shared/research-output-schema.json`. If validation fails, fix the violations before writing.

## Step 7: Write `research-summary.md`

Write a human-readable summary to `Mitchell/SAP Solutions/1 - DM/Demos/<slug>/research-summary.md`.

Use Obsidian conventions: frontmatter, wikilinks `[[]]`, callout blocks. No em-dashes. Structure:

```
---
type: research-summary
slug: <slug>
created: <date>
mode: <mode>
tags: [dm-demo, production-research, <industry-normalized>, <company-normalized>]
---

# Research Summary — <Company> / <Product>

## Company snapshot
(size, region, certifications, facility count, known to SAP)

## Production process
Numbered list matching production.steps[].seq — step name + 1-sentence description.

## Key equipment
Bullet list from equipment_hints[].

## Workforce and certifications
From workforce_hint.

## Quality and rework
Key quality issues, rework prevalence, NC categories.

## Pain points and presales signals
From pain_points[] — high-confidence entries first.

## Moments of expertise
Bulleted claims with evidence (from moments_of_expertise[]).

## Competitive context
Incumbent MES, competitors mentioned.

## Gaps and low-confidence areas
> [!warning] Gaps flagged
> List each G### entry with its issue.

## Sources
Wikilinks for Mitchell sources. URLs for web sources.
```

## Step 8: Write `research_output_v1.json`

Write the validated JSON to `Mitchell/SAP Solutions/1 - DM/Demos/<slug>/research_output_v1.json`.

## Step 9: Return R8 envelope

Return to caller:

```json
{
  "status": "ok",
  "diff": [
    "Mitchell/SAP Solutions/1 - DM/Demos/<slug>/research_output_v1.json",
    "Mitchell/SAP Solutions/1 - DM/Demos/<slug>/research-summary.md"
  ],
  "assumptions_add": [ /* all meta.assumptions[] entries */ ],
  "gaps_add": [ /* all meta.gaps_flagged[] entries */ ]
}
```

On failure, set `"status": "failed"` and include an `"error"` field. If partial files were written, list them in `diff`.

## Idempotency

If `research_output_v1.json` already exists in the slug folder and `force` is not `true`, return:
```json
{"status": "ok", "diff": [], "assumptions_add": [], "gaps_add": [], "warning": "Slug folder already exists. Pass force=true to overwrite."}
```

## Network rules reminder (from ADR-008)

- No writes outside `Mitchell/SAP Solutions/1 - DM/Demos/<slug>/` (R6).
- No em-dashes in any output text (R5).
- No numeric Fiori app IDs in any output text (R5).
- Obsidian wikilinks in all Mitchell cross-references (R5).
- ASCII-only in any field that lands in DM (R5).
- Slug propagates unchanged to downstream agents — never modify it after minting (R2).
```

- [ ] **Step 2: Test the orchestrator in interactive mode with Yanmar**

Invoke the agent manually:

```
Task(
  subagent_type: "production-research",
  prompt: "{\"company\": \"Yanmar\", \"mode\": \"interactive\"}"
)
```

Expected: agent reads the 4 load-order files, runs PREFLIGHT, asks three questions (product, focus, presales notes), fans out 4 sub-agents, synthesizes output, writes two files to `Mitchell/SAP Solutions/1 - DM/Demos/yanmar-machinery-engine-<date>/`.

Verify both output files exist and the JSON validates against `research-output-schema.json`:

```bash
python3 -c "
import json, jsonschema, pathlib
schema = json.loads(pathlib.Path('/Users/I584748/.claude/agents/dm-demo-shared/research-output-schema.json').read_text())
output_dir = sorted(pathlib.Path('/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos').glob('yanmar-*'))[-1]
data = json.loads((output_dir / 'research_output_v1.json').read_text())
jsonschema.validate(data, schema)
print('PASS')
"
```

Expected: `PASS`

- [ ] **Step 3: Test in auto mode with Umicore**

```
Task(
  subagent_type: "production-research",
  prompt: "{\"company\": \"Umicore\", \"product\": \"automotive catalyst\", \"focus\": \"throughput\", \"mode\": \"auto\"}"
)
```

Expected: no questions asked, output written, `product.type` = `process`, all assumptions logged.

- [ ] **Step 4: Commit**

```bash
cd /Users/I584748/DM_MQTT
git add firmware/docs/superpowers/plans/2026-07-08-production-research-plan-b-agent-prompts.md
git commit -m "feat(research): add production-research orchestrator prompt"
```

---

## Self-review

**Spec coverage check:**

| Spec section | Covered by task |
|---|---|
| Orchestrator flow (Steps 1-9) | Task 6 (orchestrator prompt) |
| Sub-agent A: Mitchell Reader | Task 2 |
| Sub-agent B: Company Web Researcher | Task 3 |
| Sub-agent C: Industry Process Researcher | Task 4 |
| Sub-agent D: SAP Angle Researcher | Task 5 |
| JSON Schema for validation | Task 1 |
| Interactive brief (3 upfront questions) | Task 6, Step 2 of orchestrator |
| Auto mode defaults | Task 6, Step 2 of orchestrator |
| Conflict resolution rules | Task 6, Step 5 of orchestrator |
| R8 caller envelope | Task 6, Step 9 of orchestrator |
| Idempotency | Task 6, last section |
| PREFLIGHT block | Task 6, PREFLIGHT section |
| research-summary.md structure | Task 6, Step 7 |
| Slug minting (ADR-005) | Task 6, Step 3 |
| Auto-mode 30-assumption cap | Task 6, Step 5 |

No gaps found.

**Placeholder scan:** No TBDs. All prompt files are complete. All JSON examples use real field names consistent with the schema defined in Task 1.

**Type consistency:**
- `typical_metrics` vocabulary list in sub-agent C matches the schema's `"pattern": "^[a-z][a-z0-9_]*$"` rule. All listed keys (`spindle_rpm`, `tool_wear_pct`, etc.) are snake_case. ✓
- `dm_nc_category` enum in sub-agent C prompt matches schema enum exactly (`dimensional|cosmetic|functional|process`). ✓
- `product.type` enum `discrete|process|repetitive` consistent across schema (Task 1), sub-agent C, and orchestrator Step 3. ✓
- Assumption ID format `A\d{3}` and gap ID format `G\d{3}` consistent across schema and orchestrator Step 5. ✓
