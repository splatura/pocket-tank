#!/bin/bash
# ci_build.sh - build every PUBLISHED board's firmware (tools/pt_boards.py), each
# in its own build dir under firmware/. Run from firmware/ (the ESP-IDF CI
# action's working dir) or the repo root. 2026-10-08: the workflows build what
# the list says, so publishing a board is one flag (docs/BOARDS.md).
# `--known`: every KNOWN board instead, published or not - checks.yml's compile
# gate, so an unpublished board still builds under CI's ESP-IDF (never shipped).
set -euo pipefail
list=--published-builds
case "${1:-}" in
  "") ;;
  --known) list=--known-builds ;;
  *) echo "usage: ci_build.sh [--known]" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/../firmware"
# read the list first (a failure here stops the script) and feed the loop from a
# here-string; each idf.py reads /dev/null, so it can never eat the loop's stdin
builds=$(python3 ../tools/pt_boards.py "$list")
while read -r dir frag; do
  if [ "$frag" = "-" ]; then idf.py -B "$dir" build </dev/null
  else idf.py -B "$dir" -DSDKCONFIG="$dir/sdkconfig" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;$frag" build </dev/null; fi
done <<< "$builds"
