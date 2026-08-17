#!/usr/bin/env zsh
set -euo pipefail

# Canonical stable PlayTools build: native mouse/Y-fix plus one serialized
# producer for ordinary keys and F1-F12.
exec "${0:A:h}/build_playtools_serial_keyboard.sh" "$@"
