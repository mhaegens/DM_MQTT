#!/usr/bin/env bash
set -euo pipefail

FIXTURE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPECTED_JSON="$FIXTURE_DIR/brainstorm_output_v1.expected.json"
DEMOS_DIR="$HOME/Library/Mobile Documents/iCloud~md~obsidian/Documents/Mitchell/SAP Solutions/DM/Demos"
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
ACTUAL_SUMMARY="$SLUG_DIR/brainstorm-summary.md"

if [ -f "$ACTUAL_JSON" ]; then echo "PASS: brainstorm_output_v1.json exists"; ((PASS+=1)); else echo "FAIL: brainstorm_output_v1.json missing"; ((FAIL+=1)); fi
if [ -f "$ACTUAL_SUMMARY" ]; then echo "PASS: brainstorm-summary.md exists"; ((PASS+=1)); else echo "FAIL: brainstorm-summary.md missing"; ((FAIL+=1)); fi

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
ot = data['product']['order_type']
if ot == 'production_order':
    print(f'PASS: product.order_type = {ot}')
else:
    print(f'FAIL: product.order_type = {ot} (expected production_order)')
    exit(1)
" && ((PASS+=1)) || ((FAIL+=1))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
rl = data['scenario']['has_rework_loop']
if rl == False:
    print('PASS: has_rework_loop = false')
else:
    print(f'FAIL: has_rework_loop = {rl} (expected false)')
    exit(1)
" && ((PASS+=1)) || ((FAIL+=1))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
rft = data['production_flow']['rework_from_to']
if rft == [None, None]:
    print('PASS: rework_from_to = [null, null]')
else:
    print(f'FAIL: rework_from_to = {rft} (expected [null, null])')
    exit(1)
" && ((PASS+=1)) || ((FAIL+=1))

python3 -c "
import json, pathlib
data = json.loads(pathlib.Path('$ACTUAL_JSON').read_text())
n = len(data.get('moments_of_expertise', []))
if n >= 2:
    print(f'PASS: moments_of_expertise count = {n}')
else:
    print(f'FAIL: moments_of_expertise count = {n} (need >= 2)')
    exit(1)
" && ((PASS+=1)) || ((FAIL+=1))

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
" && ((PASS+=1)) || ((FAIL+=1))

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
" && ((PASS+=1)) || ((FAIL+=1))

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ $FAIL -eq 0 ] && echo "PASS: all checks" || { echo "FAIL: $FAIL check(s) failed"; exit 1; }
