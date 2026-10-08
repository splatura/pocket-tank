#!/bin/bash
# flash_lcd40.sh - build the FNK0104S image (the 480 x 320 landscape tank, CONFIG_POCKET_TANK_LCD40)
# and flash it to the Freenove FNK0104S (Freenove 4.0in ESP32-S3 board).
# The board is found by its USB SERIAL, never by port name: the tank, the
# round board and the FNK0104S share a bench and the names swap.
#
#   tools/flash_lcd40.sh               # build, flash the app
#   tools/flash_lcd40.sh --build-only
#   tools/flash_lcd40.sh --full        # a blank board: clear nvs, the model, bootloader + table + app
#   LCD40_SERIAL=AA:BB:CC:DD:EE:FF tools/flash_lcd40.sh    # the board, by its USB serial (or set it once in tools/boards.local.sh)
#
# Its own build directory and sdkconfig (~/.cache/pocket-tank/fw-build-lcd40):
# the tank's fw-build is never touched. No preflight: a factory board runs
# Freenove's firmware, which has no director, and this board holds no tank
# anyone would miss yet (docs/DEVICE.md records when that changes).
# docs/board-fnk0104s.md has the rest.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f tools/boards.local.sh ] && . tools/boards.local.sh   # your boards' USB serials (not tracked)
SERIAL=${LCD40_SERIAL:-}
[ -n "$SERIAL" ] || { echo "flash_lcd40: set LCD40_SERIAL=<the board's USB serial> (in tools/boards.local.sh, or the environment)"; exit 1; }
B=~/.cache/pocket-tank/fw-build-lcd40
. ~/esp/esp-idf/export.sh > /dev/null 2>&1 || { echo "flash_lcd40: ESP-IDF export failed"; exit 1; }
[ -f firmware/keys/ota_signing_key.pem ] || { echo "flash_lcd40: no firmware/keys/ota_signing_key.pem - tools/ota_key.sh"; exit 1; }
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
( cd firmware && idf.py -B "$B" -DSDKCONFIG="$B/sdkconfig" -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.lcd40" build ) > "$LOG" 2>&1 \
  || { echo "flash_lcd40: build FAILED"; grep -Ei "error|fatal|failed" "$LOG" | head -30; exit 1; }
grep -E "pocket_tank.bin binary size" "$LOG" || true
grep -q "CONFIG_POCKET_TANK_LCD40=y" "$B/sdkconfig" || { echo "flash_lcd40: $B is not a LCD40 build"; exit 1; }
[[ " $* " == *" --build-only "* ]] && exit 0
PORT=$(python - "$SERIAL" <<'PY'
import sys
from serial.tools import list_ports
want = sys.argv[1].upper()
for p in list_ports.comports():
    if (p.serial_number or "").upper() == want:
        print(p.device.replace("/dev/tty.", "/dev/cu.")); sys.exit(0)
sys.exit(1)
PY
) || { echo "flash_lcd40: no board with USB serial $SERIAL on USB"; exit 1; }
echo "flash_lcd40: the FNK0104S ($SERIAL) is $PORT"
ESPTOOL="python -m esptool --chip esp32s3 -p $PORT -b 460800"
if [[ " $* " == *" --full "* ]]; then
  $ESPTOOL --after no_reset erase_region 0x9000 0x7000 | grep -E "Erase completed|rror"
  $ESPTOOL --after no_reset write_flash 0x290000 model/out/model_q4.bin | grep -E "Wrote|verified|rror"
  ( cd "$B" && $ESPTOOL write_flash "@flash_args" | grep -E "Wrote|verified|rror" )
else
  # otadata too: after an over-the-air update the board boots ota_1, and an app written to ota_0 alone would never run
  $ESPTOOL write_flash 0xA90000 "$B/ota_data_initial.bin" 0x10000 "$B/pocket_tank.bin" | grep -E "Wrote|verified|rror"
fi
echo "flash_lcd40: done"
