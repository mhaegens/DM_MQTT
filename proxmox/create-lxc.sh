#!/usr/bin/env bash
# create-lxc.sh — Provision the sapdm-demo LXC on a Proxmox host
#
# Run on the Proxmox host as root from the Proxmox shell or via SSH:
#   bash <(curl -fsSL https://raw.githubusercontent.com/mhaegens/DM_MQTT/main/proxmox/create-lxc.sh)
#
# What it does:
#   1. Prompts for LXC settings (all have sensible defaults)
#   2. Downloads the Debian 13 (Trixie) CT template if not already cached
#   3. Creates the LXC with nesting enabled (required for Docker)
#   4. Starts it and runs install.sh inside to bring up the full demo stack

set -euo pipefail

RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'; BOLD='\033[1m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${BOLD}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ OK ]${NC}  $*"; }
die()   { echo -e "${RED}[FAIL]${NC}  $*" >&2; exit 1; }
ask()   { echo -e "${CYAN}[CONF]${NC}  $*"; }

# ── Preflight ────────────────────────────────────────────────────────────────
[[ "$(id -u)" -eq 0 ]] || die "Run as root."
command -v pct &>/dev/null    || die "pct not found — this script must run on a Proxmox host."
command -v pveam &>/dev/null  || die "pveam not found — this script must run on a Proxmox host."

echo ""
echo -e "${BOLD}SAP DM MQTT Demo — Proxmox LXC Setup${NC}"
echo "──────────────────────────────────────"
echo ""

# ── Prompt helpers ───────────────────────────────────────────────────────────
prompt() {
  local var="$1" prompt_text="$2" default="$3"
  ask "${prompt_text} [${default}]: "
  read -r input
  printf -v "$var" '%s' "${input:-$default}"
}

prompt_required() {
  local var="$1" prompt_text="$2"
  while true; do
    ask "${prompt_text}: "
    read -rs input
    echo ""
    [[ -n "$input" ]] && break
    warn "This field is required."
  done
  printf -v "$var" '%s' "$input"
}

# ── Collect settings ─────────────────────────────────────────────────────────
prompt     LXC_ID    "LXC container ID"          "200"
prompt     HOSTNAME  "Container hostname"         "sapdm-demo"
prompt     IP_CIDR   "IP address (with prefix)"  "192.168.0.60/24"
prompt     GW        "Gateway"                   "192.168.0.1"
prompt     DNS       "DNS server"                "192.168.0.10"
prompt     BRIDGE    "Network bridge"            "vmbr0"
prompt     STORAGE   "Storage target"            "local-lvm"
prompt_required ROOT_PASS "Root password for the LXC (hidden)"

echo ""
info "Summary:"
echo "  LXC ID   : ${LXC_ID}"
echo "  Hostname : ${HOSTNAME}"
echo "  IP/CIDR  : ${IP_CIDR}"
echo "  Gateway  : ${GW}"
echo "  DNS      : ${DNS}"
echo "  Bridge   : ${BRIDGE}"
echo "  Storage  : ${STORAGE}"
echo ""
ask "Proceed? [y/N]: "
read -r confirm
[[ "${confirm,,}" == "y" ]] || { info "Aborted."; exit 0; }
echo ""

# ── Validate LXC ID not in use ───────────────────────────────────────────────
if pct status "$LXC_ID" &>/dev/null; then
  die "LXC ${LXC_ID} already exists. Choose a different ID."
fi

# ── Find or download Debian 13 (Trixie) template ────────────────────────────
info "Looking for Debian 13 (Trixie) CT template..."

TEMPLATE_FILE=$(find /var/lib/vz/template/cache/ -maxdepth 1 -name "debian-13-standard_*.tar.*" 2>/dev/null | sort -V | tail -1)

if [[ -z "$TEMPLATE_FILE" ]]; then
  info "Template not cached — fetching available templates list..."
  pveam update

  TEMPLATE_NAME=$(pveam available --section system 2>/dev/null | awk '{print $2}' | grep "^debian-13-standard_" | sort -V | tail -1)
  [[ -n "$TEMPLATE_NAME" ]] || die "Could not find a debian-13-standard template via pveam. Check your Proxmox template repository."

  info "Downloading ${TEMPLATE_NAME}..."
  pveam download local "$TEMPLATE_NAME" || die "Template download failed."

  TEMPLATE_FILE=$(find /var/lib/vz/template/cache/ -maxdepth 1 -name "debian-13-standard_*.tar.*" | sort -V | tail -1)
  [[ -n "$TEMPLATE_FILE" ]] || die "Template file not found after download."
fi

TEMPLATE_NAME=$(basename "$TEMPLATE_FILE")
ok "Using template: ${TEMPLATE_NAME}"

# ── Create LXC ───────────────────────────────────────────────────────────────
info "Creating LXC ${LXC_ID}..."
pct create "$LXC_ID" "local:vztmpl/${TEMPLATE_NAME}" \
  --hostname "$HOSTNAME" \
  --password "$ROOT_PASS" \
  --ostype debian \
  --memory 2048 \
  --swap 512 \
  --cores 2 \
  --rootfs "${STORAGE}:16" \
  --net0 "name=eth0,bridge=${BRIDGE},ip=${IP_CIDR},gw=${GW}" \
  --nameserver "${DNS}" \
  --unprivileged 1 \
  --start 0
ok "LXC created."

# ── Enable nesting (required for Docker) ─────────────────────────────────────
info "Enabling nesting (required for Docker inside LXC)..."
pct set "$LXC_ID" --features nesting=1
ok "Nesting enabled."

# ── Start LXC ────────────────────────────────────────────────────────────────
info "Starting LXC ${LXC_ID}..."
pct start "$LXC_ID"

info "Waiting for LXC to become ready..."
TIMEOUT=60
ELAPSED=0
until pct exec "$LXC_ID" -- hostname &>/dev/null 2>&1; do
  sleep 2
  ELAPSED=$((ELAPSED + 2))
  [[ $ELAPSED -ge $TIMEOUT ]] && die "LXC did not become ready within ${TIMEOUT}s."
done
ok "LXC is up."

# ── Run install.sh inside the LXC ────────────────────────────────────────────
info "Ensuring DNS is available inside the LXC..."
pct exec "$LXC_ID" -- bash -c "echo 'nameserver ${DNS}' > /etc/resolv.conf"
ok "DNS set to ${DNS}."

info "Running install.sh inside the LXC..."
echo ""
pct exec "$LXC_ID" -- bash -c \
  "apt-get update -qq && \
   apt-get install -y -qq curl && \
   bash <(curl -fsSL https://raw.githubusercontent.com/mhaegens/DM_MQTT/main/install.sh)"

# ── Done ─────────────────────────────────────────────────────────────────────
IP_ONLY="${IP_CIDR%%/*}"
echo ""
ok "LXC ${LXC_ID} (${HOSTNAME}) is running."
echo ""
echo -e "${BOLD}Access URLs:${NC}"
echo "  Dashboard : http://${IP_ONLY}"
echo "  Node-RED  : http://${IP_ONLY}:1880"
echo "  MQTT TCP  : ${IP_ONLY}:1883  (internal / Node-RED only)"
echo "  MQTT WS   : ${IP_ONLY}:9001  (LAN + Cloudflare Tunnel)"
echo ""
echo -e "${BOLD}Next step — configure Cloudflare Tunnel:${NC}"
echo "  pct enter ${LXC_ID}"
echo "  cd /opt/sapdm-demo"
echo "  cp cloudflared/config.yml.example cloudflared/config.yml"
echo "  # copy your tunnel credentials JSON, edit config.yml, then:"
echo "  docker compose restart cloudflared"
echo ""
