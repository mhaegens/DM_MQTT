#!/usr/bin/env bash
# smoke-test.sh — Post-install health check for the sapdm-demo stack
#
# Run inside the sapdm-demo LXC after install.sh completes:
#   bash /opt/sapdm-demo/scripts/smoke-test.sh
#
# Checks: container health, HTTP endpoints, MQTT TCP pub/sub, WebSocket port,
#         and (optional) external tunnel URL reachability.

set -uo pipefail

INSTALL_DIR="${INSTALL_DIR:-/opt/sapdm-demo}"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[1;33m'; BOLD='\033[1m'; NC='\033[0m'
pass() { echo -e "  ${GREEN}PASS${NC}  $*"; PASSED=$((PASSED + 1)); }
fail() { echo -e "  ${RED}FAIL${NC}  $*"; FAILED=$((FAILED + 1)); }
warn() { echo -e "  ${YELLOW}SKIP${NC}  $*"; }
PASSED=0; FAILED=0

check_entrypoint() {
  local base="$1" result
  result=$(curl -sS --max-time 8 -o /dev/null -w '%{http_code} %{redirect_url}' "$base/") || result=''
  if [[ "$result" == "302 $base/dashboard/" ]]; then
    pass "$base/ redirects to the dashboard"
  else
    fail "$base/ dashboard redirect failed (${result:-no response})"
  fi
}

check_websocket() {
  local url="$1" headers
  headers=$(mktemp)
  # A successful upgrade leaves the connection open until curl times out.
  curl -s --http1.1 --max-time 2 -D "$headers" -o /dev/null \
    -H 'Connection: Upgrade' -H 'Upgrade: websocket' \
    -H 'Sec-WebSocket-Version: 13' -H 'Sec-WebSocket-Protocol: mqtt' \
    -H 'Sec-WebSocket-Key: ZG0tbXF0dC1hdWRpdC0wMQ==' "$url" || true
  if grep -q '^HTTP/1.1 101' "$headers"; then
    pass "$url MQTT WebSocket upgrade"
  else
    fail "$url MQTT WebSocket upgrade failed"
  fi
  rm -f "$headers"
}

echo ""
echo -e "${BOLD}SAP DM MQTT Demo — Smoke Test${NC}"
echo "────────────────────────────────"
echo ""

# ── Install test deps if missing ─────────────────────────────────────────────
for pkg in mosquitto-clients netcat-openbsd curl; do
  if ! dpkg -s "$pkg" &>/dev/null; then
    echo "Installing ${pkg}..."
    apt-get install -y -qq "$pkg"
  fi
done

# ── 1. All containers running ────────────────────────────────────────────────
echo "1. Docker containers"
cd "$INSTALL_DIR"

EXPECTED=("sapdm-mosquitto" "sapdm-dashboard")
for name in "${EXPECTED[@]}"; do
  state=$(docker inspect --format '{{.State.Status}}' "$name" 2>/dev/null || echo "missing")
  if [[ "$state" == "running" ]]; then
    pass "${name} is running"
  else
    fail "${name} state: ${state}"
  fi
done

# ── 2. Dashboard path HTTP 200 ───────────────────────────────────────────────
echo ""
echo "2. Dashboard path (/dashboard/)"
if curl -sf --max-time 5 http://localhost/dashboard/ -o /dev/null; then
  pass "HTTP 200 on /dashboard/"
else
  fail "No HTTP 200 response on /dashboard/"
fi

# ── 3. Editor path HTTP 200 ──────────────────────────────────────────────────
echo ""
echo "3. Editor path (/editor/)"
if curl -sf --max-time 5 http://localhost/editor/ -o /dev/null; then
  pass "HTTP 200 on /editor/"
else
  fail "No HTTP 200 response on /editor/"
fi

# All workbench pages and local runtime assets must be reachable.
for endpoint in /shopfloor/ /config-editor/ /assets/site-config.js /assets/mqtt.min.js /assets/sap-logo.svg /assets/monitor.js; do
  if curl -sf --max-time 5 "http://localhost${endpoint}" -o /dev/null; then
    pass "HTTP 200 on ${endpoint}"
  else
    fail "No HTTP 200 response on ${endpoint}"
  fi
done

check_entrypoint http://localhost
check_websocket http://localhost/
check_websocket http://localhost/mqtt

# ── 4. MQTT TCP publish/subscribe ────────────────────────────────────────────
echo ""
echo "4. MQTT TCP pub/sub (:1883)"
TEST_TOPIC="sapdm/smoketest/$$"
TEST_MSG="smoke-$(date +%s)"
RECV_FILE=$(mktemp)

# Subscribe in background, give it 4s to receive
mosquitto_sub -h localhost -p 1883 -t "$TEST_TOPIC" -C 1 --quiet > "$RECV_FILE" &
SUB_PID=$!
sleep 0.5

mosquitto_pub -h localhost -p 1883 -t "$TEST_TOPIC" -m "$TEST_MSG" 2>/dev/null

# Wait up to 3s for message
for i in $(seq 1 6); do
  sleep 0.5
  RECV=$(cat "$RECV_FILE")
  if [[ "$RECV" == "$TEST_MSG" ]]; then
    break
  fi
done

kill "$SUB_PID" 2>/dev/null || true
wait "$SUB_PID" 2>/dev/null || true
rm -f "$RECV_FILE"

if [[ "${RECV:-}" == "$TEST_MSG" ]]; then
  pass "Published and received message on ${TEST_TOPIC}"
else
  fail "Did not receive MQTT message within 3s (got: '${RECV:-<empty>}')"
fi

# ── 5. MQTT WebSocket port open ───────────────────────────────────────────────
echo ""
echo "5. MQTT WebSocket (:9001)"
if nc -z -w3 localhost 9001 2>/dev/null; then
  pass "Port 9001 is open"
else
  fail "Port 9001 is not reachable"
fi

# ── 6. External tunnel URL (optional) ────────────────────────────────────────
echo ""
echo "6. External tunnel URL (optional)"
TUNNEL_URL="${TUNNEL_URL:-}"
if [[ -z "$TUNNEL_URL" ]]; then
  warn "Set TUNNEL_URL to check your public dashboard URL"
elif curl -sf --max-time 8 "$TUNNEL_URL" -o /dev/null; then
  pass "Tunnel URL reachable: $TUNNEL_URL"
else
  fail "Tunnel URL not reachable: $TUNNEL_URL — check your Proxmox cloudflared config"
fi

if [[ "$TUNNEL_URL" =~ ^(https?://[^/]+) ]]; then
  TUNNEL_ORIGIN="${BASH_REMATCH[1]}"
  check_entrypoint "$TUNNEL_ORIGIN"
  check_websocket "$TUNNEL_ORIGIN/"
  check_websocket "$TUNNEL_ORIGIN/mqtt"
  for endpoint in /config-editor/ /assets/site-config.js /assets/mqtt.min.js /assets/sap-logo.svg; do
    if curl -sf --max-time 8 "$TUNNEL_ORIGIN$endpoint" -o /dev/null; then
      pass "Public HTTP 200 on $endpoint"
    else
      fail "Public endpoint unavailable: $endpoint"
    fi
  done
fi

# ── Summary ──────────────────────────────────────────────────────────────────
echo ""
echo "────────────────────────────────"
TOTAL=$((PASSED + FAILED))
echo -e "  ${BOLD}Result: ${PASSED}/${TOTAL} checks passed${NC}"
echo ""
if [[ $FAILED -eq 0 ]]; then
  echo -e "  ${GREEN}All checks passed.${NC}"
  echo ""
  exit 0
else
  echo -e "  ${RED}${FAILED} check(s) failed. Review output above.${NC}"
  echo ""
  exit 1
fi
