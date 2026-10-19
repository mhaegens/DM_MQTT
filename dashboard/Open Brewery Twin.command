#!/bin/zsh
# Double-click in Finder to open the local twin against the configured rig broker.
cd -- "${0:A:h}/.." || exit 1
exec python3 dashboard/preview.py --open --live
