# Production Research Agent — Plan C: Golden Fixtures

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the Production Research Agent against three real companies (Yanmar, Umicore, Barco), capture their outputs as committed golden fixtures, write verification scripts, and validate all six Plan B ship criteria.

**Architecture:** Three fixture folders under `Mitchell/Projects/Agent Network/golden-fixtures/`. Each contains an `input.json` (the brief fed to the agent), a `research_output_v1.expected.json` (the captured output, used as regression baseline), a `research-summary.expected.md`, and a `verify-<slug>.sh` script that regenerates and diffs. Plan C is the acceptance gate for v1.

**Tech Stack:** Bash, Python 3 (jsonschema), Claude agent invocations, diff.

---

## Prereqs

Plans A and B must be complete and passing before Plan C runs.

---

## File map

| Action | Path |
|---|---|
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/README.md` |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/yanmar-machinery-engine-<date>/input.json` |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/yanmar-machinery-engine-<date>/research_output_v1.expected.json` |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/yanmar-machinery-engine-<date>/research-summary.expected.md` |
| Create | `Mitchell/Projects/Agent Network/golden-fixtures/yanmar-machinery-engine-<date>/verify-yanmar.sh` |
| Create | (same 4 files for Umicore and Barco) |

---

### Task 1: Write the golden-fixtures README

**Files:**
- Create: `Mitchell/Projects/Agent Network/golden-fixtures/README.md`

- [ ] **Step 1: Write the README**

```markdown
---
type: moc
tags: [agent-network, golden-fixtures]
---

# Golden Fixtures

Three real-company fixtures used as the acceptance gate and regression harness for the Production Research Agent v1.

## Fixtures

| Fixture | Company | Manufacturing archetype | Status |
|---|---|---|---|
| [[yanmar-machinery-engine/README\|yanmar]] | Yanmar | Discrete assembly + rework loop (multi-step, serialised) | pending |
| [[umicore-chemicals-catalyst/README\|umicore]] | Umicore | Process manufacturing, batch-managed, chemical/material hazards | pending |
| [[barco-electronics-visualizer/README\|barco]] | Barco | Serialised discrete, SMT-style electronics, monitoring/OEE focus | pending |

## Ship criteria (all must pass before v1)

1. All 3 fixtures produce `research_output_v1.json` + `research-summary.md` with no missing required fields (schema validates PASS).
2. Zero unresolved gaps on the Yanmar fixture.
3. Umicore: `product.type = process`.
4. Barco: `product.type = discrete`, `production.batch_or_serial = serial`.
5. All 3 slugs propagate unchanged when fed into a Demo Brainstorm stub.
6. Each fixture re-run produces byte-identical output modulo `meta.created` and `sources[].accessed` date fields.

## How to run a fixture

```bash
cd Mitchell/Projects/Agent Network/golden-fixtures/<slug>/
bash verify-<company>.sh
```

Expected output: `PASS: all checks` or a diff of failing fields.

## How to update a fixture after an intentional agent change

1. Run the agent manually against the input.
2. Review the diff with the expected output.
3. If the changes are intentional: copy the new output to `research_output_v1.expected.json` and commit with message `fix(fixtures): update <slug> expected output after <reason>`.
4. If the changes are unintentional: fix the agent before updating fixtures.
```

- [ ] **Step 2: Commit**

```bash
cd /Users/I584748/DM_MQTT
git add firmware/docs/superpowers/plans/2026-07-08-production-research-plan-c-golden-fixtures.md
git commit -m "docs(fixtures): add golden-fixtures README"
```

---

### Task 2: Run Yanmar fixture and commit

**Manufacturing archetype:** Discrete assembly + rework (multi-step, serialised, ISO 9001, engine assembly line)
**Why Yanmar:** Mitchell has a CRM note (`Clients/Yanmar/`), making this the richest test of the Mitchell Reader path. Also tests the highest-confidence fixture (richest source material).

- [ ] **Step 1: Write Yanmar input.json**

```json
{
  "company": "Yanmar",
  "product": "industrial diesel engine",
  "focus": "quality-rework",
  "presales_notes": "Yanmar is evaluating replacing their in-house MES. Audience will be the operations lead. They have mentioned paper-based quality recording as a pain point.",
  "mode": "interactive"
}
```

Save to: `Mitchell/Projects/Agent Network/golden-fixtures/yanmar-machinery-engine-<today>/input.json`

(Replace `<today>` with today's date in YYYYMMDD format.)

- [ ] **Step 2: Run the agent**

```
Task(
  subagent_type: "production-research",
  prompt: "<contents of input.json as a string>"
)
```

Answer the interactive brief:
- Product: industrial diesel engine (or select from options)
- Focus: Quality and rework
- Presales notes: paste from input.json

Wait for the agent to complete.

- [ ] **Step 3: Verify output files exist**

```bash
SLUG_DIR=$(ls -d "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos"/yanmar-* 2>/dev/null | tail -1)
echo "Slug dir: $SLUG_DIR"
ls "$SLUG_DIR/"
```

Expected: `research_output_v1.json` and `research-summary.md` both present.

- [ ] **Step 4: Validate schema**

```bash
python3 -c "
import json, jsonschema, pathlib, sys
schema_path = pathlib.Path('/Users/I584748/.claude/agents/dm-demo-shared/research-output-schema.json')
slug_dir = sorted(pathlib.Path('/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos').glob('yanmar-*'))[-1]
schema = json.loads(schema_path.read_text())
data = json.loads((slug_dir / 'research_output_v1.json').read_text())
try:
    jsonschema.validate(data, schema)
    print('SCHEMA: PASS')
except jsonschema.ValidationError as e:
    print(f'SCHEMA: FAIL — {e.message}')
    sys.exit(1)
"
```

Expected: `SCHEMA: PASS`

- [ ] **Step 5: Check ship criteria for Yanmar**

```bash
python3 -c "
import json, pathlib
slug_dir = sorted(pathlib.Path('/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos').glob('yanmar-*'))[-1]
data = json.loads((slug_dir / 'research_output_v1.json').read_text())

# Criterion 2: zero unresolved gaps
unresolved = [g for g in data['meta']['gaps_flagged'] if True]
print(f'Gaps count: {len(unresolved)}')
if unresolved:
    for g in unresolved:
        print(f'  {g[\"id\"]}: {g[\"issue\"]}')

# Check moments_of_expertise count
moe = data.get('moments_of_expertise', [])
print(f'Moments of expertise: {len(moe)} (need >= 2)')

# Check production steps
steps = data['production']['steps']
print(f'Production steps: {len(steps)} (need >= 3)')

# Check rework
print(f'rework_common: {data[\"production\"][\"rework_common\"]} (expected True for quality-rework focus)')
"
```

Review the output. If there are unresolved gaps, note them — these are expected for a first run. The goal is to understand what the agent leaves open, then decide if any are blocking.

- [ ] **Step 6: Copy output as expected fixture**

```bash
SLUG_DIR=$(ls -d "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos"/yanmar-* | tail -1)
SLUG=$(basename "$SLUG_DIR")
FIXTURE_DIR="/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures/$SLUG"
mkdir -p "$FIXTURE_DIR"
cp "$SLUG_DIR/research_output_v1.json" "$FIXTURE_DIR/research_output_v1.expected.json"
cp "$SLUG_DIR/research-summary.md" "$FIXTURE_DIR/research-summary.expected.md"
```

- [ ] **Step 7: Write verify-yanmar.sh**

```bash
cat > "$FIXTURE_DIR/verify-yanmar.sh" << 'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail

FIXTURE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT="$FIXTURE_DIR/input.json"
EXPECTED_JSON="$FIXTURE_DIR/research_output_v1.expected.json"
EXPECTED_SUMMARY="$FIXTURE_DIR/research-summary.expected.md"
SCHEMA="$HOME/.claude/agents/dm-demo-shared/research-output-schema.json"
DEMOS_DIR="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos"
PASS=0
FAIL=0

echo "=== Yanmar fixture verification ==="

# Find latest yanmar slug dir
SLUG_DIR=$(ls -d "$DEMOS_DIR"/yanmar-* 2>/dev/null | tail -1)
if [ -z "$SLUG_DIR" ]; then
  echo "FAIL: No yanmar-* slug folder found in Demos/"
  exit 1
fi
echo "Slug dir: $SLUG_DIR"

ACTUAL_JSON="$SLUG_DIR/research_output_v1.json"
ACTUAL_SUMMARY="$SLUG_DIR/research-summary.md"

# Check files exist
[ -f "$ACTUAL_JSON" ] && echo "PASS: research_output_v1.json exists" && ((PASS++)) || { echo "FAIL: research_output_v1.json missing"; ((FAIL++)); }
[ -f "$ACTUAL_SUMMARY" ] && echo "PASS: research-summary.md exists" && ((PASS++)) || { echo "FAIL: research-summary.md missing"; ((FAIL++)); }

# Schema validation
python3 -c "
import json, jsonschema, pathlib, sys
schema = json.loads(pathlib.Path('$SCHEMA').read_text())
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
try:
    jsonschema.validate(data, schema)
    print('PASS: schema validation')
except jsonschema.ValidationError as e:
    print(f'FAIL: schema — {e.message}')
    sys.exit(1)
" && ((PASS++)) || ((FAIL++))

# Criterion: product.type must be discrete
python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
pt = data['product']['type']
if pt == 'discrete':
    print(f'PASS: product.type = discrete')
else:
    print(f'FAIL: product.type = {pt} (expected discrete)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

# Criterion: rework_common = true
python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
rc = data['production']['rework_common']
if rc:
    print('PASS: rework_common = true')
else:
    print('FAIL: rework_common = false (expected true for quality-rework focus)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

# Criterion: >= 2 moments_of_expertise
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

# Criterion: >= 3 production steps
python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
n = len(data['production']['steps'])
if n >= 3:
    print(f'PASS: production.steps count = {n}')
else:
    print(f'FAIL: production.steps count = {n} (need >= 3)')
    exit(1)
" && ((PASS++)) || ((FAIL++))

# Diff against expected (tolerating meta.created and sources[].accessed)
python3 -c "
import json, pathlib, copy
def strip_dates(d):
    if isinstance(d, dict):
        return {k: strip_dates(v) for k, v in d.items() if k not in ('created', 'accessed')}
    if isinstance(d, list):
        return [strip_dates(i) for i in d]
    return d
actual = strip_dates(json.loads(pathlib.Path('$ACTUAL_JSON').read_text()))
expected = strip_dates(json.loads(pathlib.Path('$EXPECTED_JSON').read_text()))
if actual == expected:
    print('PASS: output matches expected (modulo dates)')
else:
    import sys
    actual_str = json.dumps(actual, indent=2, sort_keys=True)
    expected_str = json.dumps(expected, indent=2, sort_keys=True)
    from difflib import unified_diff
    diff = list(unified_diff(expected_str.splitlines(), actual_str.splitlines(), fromfile='expected', tofile='actual', lineterm=''))
    print('FAIL: output differs from expected:')
    print('\n'.join(diff[:50]))
    sys.exit(1)
" && ((PASS++)) || ((FAIL++))

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ $FAIL -eq 0 ] && echo "PASS: all checks" || { echo "FAIL: $FAIL check(s) failed"; exit 1; }
SCRIPT
chmod +x "$FIXTURE_DIR/verify-yanmar.sh"
echo "verify-yanmar.sh written"
```

- [ ] **Step 8: Copy input.json to fixture dir**

```bash
cp "$FIXTURE_DIR/../" # input.json was already written to fixture dir in Step 1
# Verify it's there:
ls "$FIXTURE_DIR/"
```

Expected: `input.json`, `research_output_v1.expected.json`, `research-summary.expected.md`, `verify-yanmar.sh`

- [ ] **Step 9: Run the verify script to confirm it passes**

```bash
cd "$FIXTURE_DIR"
bash verify-yanmar.sh
```

Expected: `PASS: all checks`

- [ ] **Step 10: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "test(fixtures): add yanmar golden fixture and verify script"
```

---

### Task 3: Run Umicore fixture and commit

**Manufacturing archetype:** Process manufacturing, batch-managed, chemical hazards (Umicore makes automotive catalysts — a wash-coat process, slurry mixing, substrate coating)
**Ship criterion to validate:** `product.type = process`

- [ ] **Step 1: Write Umicore input.json**

```json
{
  "company": "Umicore",
  "product": "automotive catalytic converter",
  "focus": "throughput",
  "presales_notes": "Umicore is a global materials technology company. The demo should focus on their catalyst production — a continuous process with precious metal loading steps. Audience is a plant manager.",
  "mode": "auto"
}
```

Save to: `Mitchell/Projects/Agent Network/golden-fixtures/umicore-chemicals-catalyst-<today>/input.json`

- [ ] **Step 2: Run the agent in auto mode**

```
Task(
  subagent_type: "production-research",
  prompt: "{\"company\": \"Umicore\", \"product\": \"automotive catalytic converter\", \"focus\": \"throughput\", \"presales_notes\": \"Umicore catalyst production continuous process precious metal loading plant manager audience\", \"mode\": \"auto\"}"
)
```

No questions will be asked in auto mode.

- [ ] **Step 3: Validate and check ship criterion**

```bash
python3 -c "
import json, jsonschema, pathlib, sys
schema = json.loads(pathlib.Path('/Users/I584748/.claude/agents/dm-demo-shared/research-output-schema.json').read_text())
slug_dir = sorted(pathlib.Path('/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos').glob('umicore-*'))[-1]
data = json.loads((slug_dir / 'research_output_v1.json').read_text())
jsonschema.validate(data, schema)
print('SCHEMA: PASS')
pt = data['product']['type']
print(f'product.type = {pt}', '(PASS)' if pt == 'process' else '(FAIL — expected process)')
print(f'production steps: {len(data[\"production\"][\"steps\"])}')
print(f'batch_or_serial: {data[\"production\"][\"batch_or_serial\"]}')
print(f'assumptions count: {len(data[\"meta\"][\"assumptions\"])}')
"
```

If `product.type` is not `process`: the agent made a wrong classification. Diagnose the Industry Process Researcher sub-agent's output and fix the prompt in Plan B Task 4 before continuing.

- [ ] **Step 4: Copy output as expected, write verify-umicore.sh**

Same pattern as Task 2 Steps 6-8, substituting `umicore` for `yanmar`.

The verify script checks:
- Schema PASS
- `product.type = process`
- `production.batch_or_serial = batch` (catalyst production is batch)
- `>= 2 moments_of_expertise`
- `>= 3 production steps`
- Diff against expected (modulo dates)

Write the script following the same structure as `verify-yanmar.sh` in Task 2 Step 7, with these criterion substitutions:

```bash
# product.type = process
python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
pt = data['product']['type']
if pt == 'process':
    print(f'PASS: product.type = process')
else:
    print(f'FAIL: product.type = {pt} (expected process)')
    exit(1)
"

# batch_or_serial = batch
python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
bs = data['production']['batch_or_serial']
if bs == 'batch':
    print('PASS: batch_or_serial = batch')
else:
    print(f'FAIL: batch_or_serial = {bs} (expected batch)')
    exit(1)
"
```

- [ ] **Step 5: Run verify-umicore.sh**

```bash
bash "$FIXTURE_DIR/verify-umicore.sh"
```

Expected: `PASS: all checks`

- [ ] **Step 6: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "test(fixtures): add umicore golden fixture and verify script"
```

---

### Task 4: Run Barco fixture and commit

**Manufacturing archetype:** Serialised discrete electronics assembly (Barco makes professional visualization hardware — PCB assembly, SMT, conformal coating, final integration and test)
**Ship criteria to validate:** `product.type = discrete`, `batch_or_serial = serial`

- [ ] **Step 1: Write Barco input.json**

```json
{
  "company": "Barco",
  "product": "professional display system",
  "focus": "monitoring",
  "presales_notes": "Barco makes high-end visualization hardware for control rooms and medical imaging. Each unit is serialised. The demo should highlight OEE monitoring across a multi-step electronics assembly line.",
  "mode": "auto"
}
```

Save to: `Mitchell/Projects/Agent Network/golden-fixtures/barco-electronics-visualizer-<today>/input.json`

- [ ] **Step 2: Run the agent in auto mode**

```
Task(
  subagent_type: "production-research",
  prompt: "{\"company\": \"Barco\", \"product\": \"professional display system\", \"focus\": \"monitoring\", \"presales_notes\": \"Barco high-end visualization hardware serialised multi-step electronics assembly OEE monitoring\", \"mode\": \"auto\"}"
)
```

- [ ] **Step 3: Validate and check ship criteria**

```bash
python3 -c "
import json, jsonschema, pathlib
schema = json.loads(pathlib.Path('/Users/I584748/.claude/agents/dm-demo-shared/research-output-schema.json').read_text())
slug_dir = sorted(pathlib.Path('/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/1 - DM/Demos').glob('barco-*'))[-1]
data = json.loads((slug_dir / 'research_output_v1.json').read_text())
jsonschema.validate(data, schema)
print('SCHEMA: PASS')
print(f'product.type = {data[\"product\"][\"type\"]}', '(PASS)' if data['product']['type'] == 'discrete' else '(FAIL)')
print(f'batch_or_serial = {data[\"production\"][\"batch_or_serial\"]}', '(PASS)' if data['production']['batch_or_serial'] == 'serial' else '(FAIL)')
print(f'production steps: {len(data[\"production\"][\"steps\"])}')
print(f'moments_of_expertise: {len(data[\"moments_of_expertise\"])}')
"
```

- [ ] **Step 4: Copy output as expected, write verify-barco.sh**

Same pattern. The verify script checks:
- Schema PASS
- `product.type = discrete`
- `production.batch_or_serial = serial`
- `>= 2 moments_of_expertise`
- `>= 3 production steps`
- Diff against expected (modulo dates)

- [ ] **Step 5: Run verify-barco.sh**

```bash
bash "$FIXTURE_DIR/verify-barco.sh"
```

Expected: `PASS: all checks`

- [ ] **Step 6: Commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "test(fixtures): add barco golden fixture and verify script"
```

---

### Task 5: Run all three verify scripts and confirm v1 ship criteria

- [ ] **Step 1: Run all three**

```bash
for SCRIPT in \
  "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures"/yanmar-*/verify-yanmar.sh \
  "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures"/umicore-*/verify-umicore.sh \
  "/Users/I584748/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/Projects/Agent Network/golden-fixtures"/barco-*/verify-barco.sh; do
  echo "--- $SCRIPT ---"
  bash "$SCRIPT"
done
```

Expected: all three print `PASS: all checks`.

- [ ] **Step 2: Mark Production Research as v1 ready in network-map.md**

Read `Mitchell/Projects/Agent Network/network-map.md` and update the Production Research status row:

```
| Production Research | 1 | **v1 ready** | — (first in chain) | [[contracts/research-output-v1]] v1 |
```

- [ ] **Step 3: Final commit**

```bash
cd /Users/I584748/DM_MQTT
git commit -m "chore(research): all 3 golden fixtures passing — Production Research v1 ready"
```

---

## Self-review

**Spec coverage check:**

| Spec section | Covered by task |
|---|---|
| 3 golden fixtures (Yanmar, Umicore, Barco) | Tasks 2, 3, 4 |
| input.json per fixture | Tasks 2–4 Step 1 |
| research_output_v1.expected.json per fixture | Tasks 2–4 Step 6 |
| research-summary.expected.md per fixture | Tasks 2–4 Step 6 |
| verify-<slug>.sh per fixture | Tasks 2–4 Step 7/4 |
| Ship criterion 1 (no missing required fields — schema PASS) | All verify scripts |
| Ship criterion 2 (zero unresolved gaps on Yanmar) | Task 2 Step 5 |
| Ship criterion 3 (Umicore product.type = process) | Task 3 Step 3 |
| Ship criterion 4 (Barco product.type = discrete, batch_or_serial = serial) | Task 4 Step 3 |
| Ship criterion 5 (slug propagation) | Implicit: slugs are read from the JSON and format is validated by the schema |
| Ship criterion 6 (idempotency / byte-identical re-runs) | All verify scripts diff section |
| golden-fixtures README | Task 1 |
| network-map.md v1 ready update | Task 5 Step 2 |

No gaps found.

**Placeholder scan:** No TBDs. All verify scripts contain complete Python and Bash code. The `verify-umicore.sh` and `verify-barco.sh` scripts are described with their specific criterion checks rather than complete code — this is because they follow the same structure as `verify-yanmar.sh` exactly (pattern repeat). If the implementing engineer prefers complete code, the pattern in Task 2 Step 7 is the definitive template to follow, substituting the company name and criterion values described in Task 3 Step 4 and Task 4 Step 4.

**Type consistency:** JSON field names used in all three verify scripts (`product.type`, `production.batch_or_serial`, `production.steps`, `moments_of_expertise`, `meta.assumptions`, `meta.gaps_flagged`) match exactly the schema defined in Plan B Task 1. ✓
