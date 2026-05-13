#!/usr/bin/env bash
# install.sh — One-shot installer for the SAP DM MQTT demo stack
# Run on a fresh Ubuntu 24.04 LXC as root (or with sudo).
# Usage: bash install.sh

set -euo pipefail

REPO="https://github.com/mhaegens/DM_MQTT.git"
INSTALL_DIR="/opt/sapdm-demo"

RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'; BOLD='\033[1m'; NC='\033[0m'
info()  { echo -e "${BOLD}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ OK ]${NC}  $*"; }
die()   { echo -e "${RED}[FAIL]${NC}  $*" >&2; exit 1; }

# ── 1. OS check ─────────────────────────────────────────────────────────────
info "Checking OS..."
[[ "$(id -u)" -eq 0 ]] || die "Run as root or with sudo."
grep -qi "ubuntu" /etc/os-release || warn "Not Ubuntu — proceeding anyway."

# ── 2. Base packages ────────────────────────────────────────────────────────
info "Installing base packages (curl, git, ca-certificates, gnupg)..."
apt-get update -qq
apt-get install -y -qq curl git ca-certificates gnupg lsb-release
ok "Base packages ready."

# ── 3. Docker Engine ────────────────────────────────────────────────────────
if command -v docker &>/dev/null; then
  ok "Docker already installed: $(docker --version)"
else
  info "Installing Docker Engine..."
  curl -fsSL https://get.docker.com | sh
  ok "Docker installed: $(docker --version)"
fi

# Ensure Docker Compose plugin is available
docker compose version &>/dev/null || die "Docker Compose plugin not found. Install docker-compose-plugin and retry."
ok "Docker Compose: $(docker compose version --short)"

# ── 4. Clone or update repo ─────────────────────────────────────────────────
if [[ -d "${INSTALL_DIR}/.git" ]]; then
  info "Repo already cloned — pulling latest main..."
  git -C "${INSTALL_DIR}" pull origin main
else
  info "Cloning repo to ${INSTALL_DIR}..."
  git clone "${REPO}" "${INSTALL_DIR}"
fi
ok "Repo at ${INSTALL_DIR} is up to date."

cd "${INSTALL_DIR}"

# ── 5. Runtime directories ──────────────────────────────────────────────────
info "Creating runtime directories..."
mkdir -p mosquitto/data mosquitto/log
ok "Runtime directories ready."

# ── 6. Cloudflare Tunnel config check ───────────────────────────────────────
if [[ ! -f cloudflared/config.yml ]]; then
  warn "cloudflared/config.yml not found."
  warn "The cloudflared container will start but fail until you configure the tunnel."
  warn "After setup:"
  warn "  1. Copy your tunnel credentials JSON to ${INSTALL_DIR}/cloudflared/"
  warn "  2. cp cloudflared/config.yml.example cloudflared/config.yml"
  warn "  3. Edit cloudflared/config.yml — fill in tunnel ID and hostname"
  warn "  4. docker compose restart cloudflared"
fi

# ── 7. Pull images & start stack ────────────────────────────────────────────
info "Pulling Docker images (this may take a minute)..."
docker compose pull
info "Starting stack..."
docker compose up -d

# ── 8. Status ───────────────────────────────────────────────────────────────
echo ""
docker compose ps
echo ""
ok "Stack is up."
echo ""
echo -e "${BOLD}Access URLs:${NC}"
LAN_IP=$(hostname -I | awk '{print $1}')
echo "  Dashboard : http://${LAN_IP}"
echo "  MQTT TCP  : ${LAN_IP}:1883  (internal only)"
echo "  MQTT WS   : ${LAN_IP}:9001  (LAN + Cloudflare Tunnel)"
echo ""
if [[ ! -f cloudflared/config.yml ]]; then
  warn "Cloudflare Tunnel not yet configured — SAP DM cannot connect until tunnel is set up."
fi
