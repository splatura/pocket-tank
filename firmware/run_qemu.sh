#!/bin/bash
# run_qemu.sh — build a board's emulator image and boot it in Espressif QEMU
# (esp32s3 machine, octal 8 MB PSRAM, the model partition populated), UART on
# the terminal. Ctrl-A X to quit.
#   ./run_qemu.sh            # the 1.8's world
#   ./run_qemu.sh round      # the 1.75C bowl
#   ./run_qemu.sh watch      # the 2.06 watch
#   ./run_qemu.sh lcd40      # the Freenove FNK0104S (480 x 320)
#   QEMU=/path/to/qemu-system-xtensa ./run_qemu.sh   # a QEMU of your own
# The image is sdkconfig.defaults + the board's fragment + sdkconfig.qemu: the
# display, touch and sound are stub ports, the image is unsigned, and the
# FNK0104S has no battery meter (QEMU has no ADC). Decisions go to the log.
# Needs the esp_develop 9.2.2+ QEMU (octal PSRAM): ESP-IDF 5.5's
#   python ~/esp/esp-idf/tools/idf_tools.py install qemu-xtensa
# installs it; it needs the host's libslirp (Arch: pacman -S libslirp,
# Debian/Ubuntu: apt install libslirp0). The 9.0.0 QEMU that IDF 5.4.1 ships
# lacks octal PSRAM and boot-loops.
set -e
cd "$(dirname "$0")"
case "${1:-}" in
  ""|amoled18) FRAG=""; B=build_qemu ;;
  round)       FRAG="sdkconfig.round;"; B=build_qemu-round ;;
  watch)       FRAG="sdkconfig.watch;"; B=build_qemu-watch ;;
  lcd40)       FRAG="sdkconfig.lcd40;"; B=build_qemu-lcd40 ;;
  *) echo "run_qemu: board is one of amoled18 (default), round, watch, lcd40"; exit 1 ;;
esac
[ $# -gt 0 ] && shift
[ -n "${IDF_PATH:-}" ] || . ~/esp/esp-idf/export.sh >/dev/null 2>&1
if [ -z "${QEMU:-}" ]; then
  QEMU=$(ls -d "$HOME"/.espressif/tools/qemu-xtensa/esp_develop_9.2.2_*/qemu/bin/qemu-system-xtensa 2>/dev/null | tail -1)
  [ -n "$QEMU" ] || { echo "run_qemu: no esp_develop 9.2.2 QEMU - python ~/esp/esp-idf/tools/idf_tools.py install qemu-xtensa (it needs libslirp)"; exit 1; }
fi
if ldd "$QEMU" 2>/dev/null | grep -q 'not found'; then
  echo "run_qemu: $QEMU is missing libraries:"; ldd "$QEMU" | grep 'not found'
  echo "run_qemu: libslirp.so.0 is the usual one - Arch: sudo pacman -S libslirp; Debian/Ubuntu: sudo apt install libslirp0"; exit 1
fi
MODEL="../model/out/model_q4.bin"
idf.py -B "$B" -DSDKCONFIG="$B/sdkconfig" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;${FRAG}sdkconfig.qemu" build >/dev/null \
  || { echo "run_qemu: build FAILED - idf.py -B $B build shows why"; exit 1; }
# image on the local SSD: QEMU needs file locking, which AFP shares refuse
mkdir -p "$HOME/.cache/pocket-tank"
IMG="$HOME/.cache/pocket-tank/flash_qemu_$B.bin"
esptool.py --chip esp32s3 merge_bin --fill-flash-size 16MB -o "$IMG" \
  0x0      $B/bootloader/bootloader.bin \
  0x8000   $B/partition_table/partition-table.bin \
  0x10000  $B/pocket_tank.bin \
  0x290000 "$MODEL" >/dev/null
ls -la "$IMG" | awk '{print "flash image:", $5, "bytes"}'
exec "$QEMU" -nographic -machine esp32s3 -m 8M -global driver=ssi_psram,property=is_octal,value=true \
  -drive file="$IMG",if=mtd,format=raw,file.locking=off \
  -serial mon:stdio "$@"
