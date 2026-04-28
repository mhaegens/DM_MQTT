#!/usr/bin/env bash
# generate_fonts.sh — Convert IBM Plex fonts to LVGL C arrays.
# Run once from the firmware/ directory after installing lv_font_conv.
# Requires IBM Plex Sans and IBM Plex Mono OTF files in /tmp/fonts/.
#
# Download fonts:
#   IBM Plex Sans:  https://github.com/IBM/plex/releases (IBMPlexSans-*.otf)
#   IBM Plex Mono:  https://github.com/IBM/plex/releases (IBMPlexMono-*.otf)
#
# Usage: bash generate_fonts.sh /path/to/IBMPlexSans-Regular.otf \
#                                /path/to/IBMPlexSans-SemiBold.otf \
#                                /path/to/IBMPlexMono-Regular.otf

set -e

SANS=${1:-"IBMPlexSans-Regular.otf"}
SANS_BD=${2:-"IBMPlexSans-SemiBold.otf"}
MONO=${3:-"IBMPlexMono-Regular.otf"}
OUT="fonts"

mkdir -p "$OUT"

# Character range: Basic Latin + Latin-1 Supplement + degree symbol
CHARS="0x0020-0x00FF,0x00B0"

gen_sans() {
    local SIZE=$1
    local NAME="ibm_plex_sans_${SIZE}"
    echo "Generating $NAME..."
    lv_font_conv \
        --font "$SANS" -r "$CHARS" \
        --font "$SANS_BD" -r "$CHARS" \
        --size "$SIZE" --format lvgl --bpp 4 \
        --output "$OUT/${NAME}.c" \
        --lv-font-name "$NAME"
    # Header
    printf "#pragma once\n#include \"lvgl.h\"\nextern lv_font_t %s;\n" \
        "$NAME" > "$OUT/${NAME}.h"
}

gen_mono() {
    local SIZE=$1
    local NAME="ibm_plex_mono_${SIZE}"
    echo "Generating $NAME..."
    lv_font_conv \
        --font "$MONO" -r "$CHARS" \
        --size "$SIZE" --format lvgl --bpp 4 \
        --output "$OUT/${NAME}.c" \
        --lv-font-name "$NAME"
    printf "#pragma once\n#include \"lvgl.h\"\nextern lv_font_t %s;\n" \
        "$NAME" > "$OUT/${NAME}.h"
}

gen_sans 40
gen_sans 32
gen_sans 26
gen_sans 22
gen_sans 18
gen_sans 15

gen_mono 26
gen_mono 18
gen_mono 15

echo "Done. Font files written to $OUT/"
echo "Add fonts/ to your CMakeLists EXTRA_COMPONENT_DIRS or SRCS."
