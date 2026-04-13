#!/usr/bin/env bash
# update.sh — Pull latest from main and restart the SAP DM MQTT demo stack
# Run on the LXC as root from any directory.
# Usage: bash /opt/sapdm-demo/update.sh

set -euo pipefail

cd /opt/sapdm-demo

echo "Pulling latest from main..."
git pull origin main

echo "Pulling updated Docker images..."
docker compose pull

echo "Restarting stack..."
docker compose up -d --remove-orphans

echo ""
docker compose ps
