#!/bin/zsh
# Double-click in Finder to choose a digital twin.
cd -- "${0:A:h}/.." || exit 1
exec python3 dashboard/preview.py --open
