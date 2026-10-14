#!/usr/bin/env bash
# Compile a snapshot in a path without spaces; leave outputs for inspection.
set -euo pipefail
FIRMWARE_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_SNAPSHOT=$(mktemp -d "${TMPDIR:-/tmp}/dm-mqtt-build.XXXXXX")
cp -R "$FIRMWARE_DIR/main" "$FIRMWARE_DIR/patches" "$BUILD_SNAPSHOT/"
cp "$FIRMWARE_DIR/CMakeLists.txt" "$FIRMWARE_DIR/sdkconfig.defaults" "$FIRMWARE_DIR/partitions.csv" "$BUILD_SNAPSHOT/"
if [[ -f "$FIRMWARE_DIR/dependencies.lock" ]]; then cp "$FIRMWARE_DIR/dependencies.lock" "$BUILD_SNAPSHOT/"; fi
if [[ -d "$FIRMWARE_DIR/managed_components" ]]; then cp -R "$FIRMWARE_DIR/managed_components" "$BUILD_SNAPSHOT/"; fi
if [[ -f "$FIRMWARE_DIR/sdkconfig.local.defaults" ]]; then
  cp "$FIRMWARE_DIR/sdkconfig.local.defaults" "$BUILD_SNAPSHOT/"
  idf.py -C "$BUILD_SNAPSHOT" -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local.defaults' build
else
  idf.py -C "$BUILD_SNAPSHOT" build
fi
printf '\nVerified build snapshot: %s\nNo device was flashed.\n' "$BUILD_SNAPSHOT"
