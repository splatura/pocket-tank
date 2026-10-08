#!/bin/bash
# ci_build.sh - build every PUBLISHED board's firmware (tools/pt_boards.py), each
# in its own build dir under firmware/. Run from firmware/ (the ESP-IDF CI
# action's working dir) or the repo root. 2026-10-08: the workflows build what
# the list says, so publishing a board is one flag (docs/BOARDS.md).
set -euo pipefail
cd "$(dirname "$0")/../firmware"
# read the list first (a failure here stops the script) and feed the loop from a
# variable: idf.py must not be able to eat the loop's stdin
builds=$(python3 ../tools/pt_boards.py --published-builds)
while read -r dir frag; do
  if [ "$frag" = "-" ]; then idf.py -B "$dir" build
  else idf.py -B "$dir" -DSDKCONFIG="$dir/sdkconfig" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;$frag" build; fi
done <<< "$builds"
