# The Freenove FNK0104S as a fourth board: design

2026-10-08. Status: design approved in conversation, awaiting review of this write-up.

## Goal

Run the tank on the Freenove FNK0104S (4.0-inch IPS, ESP32-S3) as a full fourth
board with the same standing as the 1.8, the pendant and the watch: simulator
world, `check-all`, its own signed image and update manifest, a place in the
browser installer, and a flash script with the preflight. One source tree, one
save layout, one model (docs/BOARDS.md).

The board is on its way. Everything that does not need the glass is built and
tested first. The board joins a public release only once the bench steps below
pass.

## The board

The FNK0104S is QDtech's ES3C40P under Freenove's name. Sources:
- Freenove's repository (github.com/Freenove/Freenove_ESP32_S3_Display): the
  4.0-inch schematic, the `FNK0104S_4.0_320x480_ST7796.h` TFT_eSPI setup, and
  the example sketches' pin defines.
- QDtech's manual (lcdwiki.com/res/ES3C40P/4.0inch_IPS_ESP32-S3_ES3C40P_User_Manual.pdf).

Freenove's README calls the panel "ST7789"; its setup file and the vendor
manual both say ST7796S. This design follows ST7796S.

| part | FNK0104S | against the 1.8 |
|---|---|---|
| chip | ESP32-S3, 16 MB QSPI flash, 8 MB octal PSRAM | the same: partitions.csv and the model partition unchanged |
| screen | ST7796S IPS LCD, 320 x 480, 4-wire SPI | new port |
| touch | FT6336 (FocalTech, I2C 0x38) | the FT3168's family: reused |
| audio | ES8311 (I2C 0x18) + FM8002E amp, enabled LOW | the same codec: reused, its pins |
| power | TP4054 charger; the cell's voltage on GPIO9 through a divider | no PMIC, no fuel gauge, no power-off |
| IMU | none | no auto-flip, no motion handling |
| RTC | none | the pendant's clockless path |
| keys | BOOT (GPIO0), RESET | BOOT is the sleep key |
| other | WS2812 on GPIO42, micro-SD | unused |

Pins (Freenove's FNK0104AB/S defines, which agree with the ES3C40P manual):

| signal | GPIO |
|---|---|
| LCD CS / MOSI / SCLK / DC | 10 / 11 / 12 / 46 |
| LCD reset | none (resets with the chip) |
| backlight (PWM, high = on) | 45 |
| I2C SDA / SCL | 16 / 15 |
| touch RST / INT | 18 / 17 |
| I2S MCLK / BCLK / WS / DOUT / DIN | 4 / 5 / 7 / 8 / 6 |
| amp enable (low = on) | 1 |
| battery voltage (ADC) | 9 |
| BOOT key | 0 |

Prior art: lmoiseichuk's port of the 2.8-inch sibling (ES3C28P, same codec,
touch, battery and audio pins; github.com/lmoiseichuk/pocket-tank-cyd,
`docs/CYD.md`). It predates 0.3 and rescales every page for 320 x 240. It is
used here as a reference for the amp polarity, the clockless clock, the
SCREEN setting and the SPI striping. It is not merged.

## 1. The world: `TANK_LCD40`

A compile-time world, beside `TANK_ROUND` and `TANK_WATCH`.

- **Geometry** (`common/tank.h`): `TANK_W 480`, `TANK_H 320`. The walls are the
  frame (`TANK_FX0 0`, `TANK_FX1 TANK_W`), and `TANK_BOT TANK_H`.
- **Algae grid:** `ALGAE_CELL 16`, 30 x 20 = 600 cells, within the 644 the save
  keeps. The save layout does not move (progression.c's SAVE LAYOUT LOCK).
- **Fronds:** the segment height is scaled so a full frond's tip sits just
  under the surface of a 320 px tank. The watch's segment does the same in
  the other direction.
- **Pages** (`common/render.h`): the 448 x 368 PAGE space sits centred, with
  `PAGE_X 16` and `PAGE_Y -24`. The glass shows page y 24..344, and the box every
  glass shows whole (x 56..392, y 60..300) fits. A page that draws in its top
  or bottom 24 rows gets a layout block for this board (the milestones page's
  `MSP_*`, as the watch does). No board conditions go into page logic.
- **Model:** unchanged. The encoder's distance bands (70 / 180 / 380 px of a
  448-wide tank) are left as they are: 480 is close to 448 and the bands
  still mean near, mid and far.
- **Ids:** `PT_BOARD "fnk0104s"`, `PT_BOARD_NAME "Freenove FNK0104S"`
  (`common/version.h`), and `board_is_lcd40()` beside `board_is_round()` and
  `board_is_watch()`.
- **Simulator:** `make FNK=1` builds `fishsim-lcd40` in `build-lcd40/`.
  `check-all` runs every selftest in four worlds. `tools/board_sheet.py` adds
  the fourth glass.

## 2. Firmware ports

The build is `sdkconfig.defaults` plus `sdkconfig.lcd40`:
`CONFIG_POCKET_TANK_LCD40=y` (depends on !ROUND and !WATCH),
`CONFIG_POCKET_TANK_DISPLAY_SH8601=n`, and the new
`CONFIG_POCKET_TANK_DISPLAY_ST7796=y`. `firmware/main/CMakeLists.txt` picks
`display_port_st7796.c` when it is set. Pins go in a `/* ---- the FNK0104S ---- */`
block of `board_pins.h` with an `F_` prefix.

**`display_port_st7796.c`** implements all of `display_port.h`:
- SPI2 at `CONFIG_POCKET_TANK_LCD40_SPI_MHZ`, default 40. The panel is driven
  landscape through MADCTL, and frames go out row-major from two ping-pong
  DMA stripes in internal RAM (`esp_lcd` panel IO). The stripe height is
  chosen against the `.bss` budget.
- The frame is byte-swapped for the panel. Colour order (RGB/BGR) and
  inversion follow Freenove's setup (`TFT_INVERSION_ON`) and are confirmed at
  first light.
- Brightness is LEDC PWM on GPIO45 and is kept across sleep/wake. Sleep sends
  `SLPIN` and turns the backlight off; wake sends `SLPOUT` and restores the
  level.
- `display_port_set_inverted` flips both MADCTL mirrors, so the flip costs no
  extra work per frame.
- The port owns the I2C bus (16/15) and provides `board_i2c_bus()`, as the
  AMOLED port does.
- Round/watch-only calls (`display_port_deep_standby`, `deep_sleep_bus`,
  `frame_origin`, views) are no-ops or identity. `display_port_deep_sleep_pins`
  holds CS high and the backlight low.

**Touch** (`touch_port_ft3168.c`): a `board_is_lcd40()` branch resets on GPIO18
and reads the FT6336 at 0x38 through the FT5x06 path. The board gets its own
calibration and orientation (the sibling's panel read turned 180 degrees).
The touch follows `display_port_set_inverted`.

**Audio** (`audio_port_es8311.c`): the pin macros gain the board's values. The
amp's level becomes `AMP_ON_LEVEL` (0 here, 1 elsewhere). The pin is driven
to off before it becomes an output and is held off through deep sleep. The
codec's analog-rail call finds no PMIC and does nothing.

**Clock:** no RTC. The pendant's clockless path covers it: Wi-Fi time sync,
and `rtc_port_seed` from the save after a power cut.

**Absent IMU:** `imu_port_init` finds nothing. Handling, for the idle light,
comes from touch alone.

## 3. Battery, sleep and the SCREEN setting

**`battery_port_adc.c`**, selected by `CONFIG_POCKET_TANK_LCD40` in place of
`battery_port_axp2101.c` at build time (firmware/main/CMakeLists.txt), so
the other boards' images do not change:
- It reads GPIO9 with ADC1 and the curve-fitting calibration, times the
  divider ratio (`F_BAT_DIVIDER`, 2.0 until measured), and takes a median of
  samples spread over about 5 s.
- Voltage maps to fraction through a single-cell LiPo open-circuit table
  (4.20 V = 1.0 ... 3.30 V = 0.0), clamped.
- State:
  - `BAT_CHARGING` while the USB-Serial-JTAG port sees a host, or while the
    voltage has risen steadily over the last few minutes;
  - `BAT_FULL` on power at 4.15 V or more;
  - `BAT_ON_BATTERY` otherwise.
- `battery_port_poweroff` returns false. The rail calls are no-ops.
  `battery_port_vbat_mv` and `battery_port_dump` report the ADC.
- The battery page marks the figure as an estimate on this board.

**The key:** `battery_port_key_init` / `key_poll` read BOOT (GPIO0, active low)
with the same short/long press semantics. The existing sleep path stays as it
is: a short press darkens and light-sleeps the tank, a press within 20 min
resumes it, and past the grace it deep-sleeps with BOOT as ext0. Where the
1.8 would power off, this board deep-sleeps. Before deep sleep the
backlight is low and held, the amp is held off, the I2S pins are low, and
the touch is held in reset.

**SCREEN row** (settings page, this board only): UPRIGHT / FLIPPED, stored in
NVS beside the brightness (not in the tank save). It is restored at boot and
re-saved after a tank reset, as the brightness is.

## 4. Release, installer, docs

- `tools/pt_boards.py`: `("fnk0104s", "Freenove FNK0104S", "sdkconfig.lcd40")`,
  with a released flag that `release.yml` and `make_installer.py` honour.
- `release.yml`: a fourth signed build (`build-lcd40`), and the manifest count
  becomes 4.
- `make_installer.py` writes `manifest-fnk0104s.json`. `installer/index.html` adds
  the board to `LABEL` and `NOTE` ("4.0 inch (Freenove)": an LCD, battery
  estimated, BOOT to sleep).
- `tools/flash_lcd40.sh` is modelled on `flash_watch.sh`: preflight, build
  dir `fw-build-lcd40`, `LCD40_SERIAL` in `tools/boards.local.sh`, and
  `--build-only`.
- The release gate: the board's released flag stays off until the bench steps
  pass, which keeps it out of the release build and the installer. Everything
  else lands on `main`.
- Docs: `docs/board-fnk0104s.md` (pins and sources, traps, bench results);
  `BOARDS.md`, the README (board table, install, layout) and `CLAUDE.md` gain
  the fourth board.

## Testing

Before the board:
1. `make -C sim check-all` passes in four worlds.
2. A cross-board save test (`--selftest-saves`) loads a 1.8, a pendant and a
   watch save into the 480 x 320 world and writes them back, with the
   offsets unchanged.
3. `tools/board_sheet.py --selftest-card` (and `--snapshot`) on four glasses,
   checked by eye for the cut-off strips.
4. All four firmware builds compile. `.bss` is compared with the last 1.8
   build (about 23 KB of internal RAM headroom).
5. The 1.8, pendant and watch builds and snapshots are unchanged by the work.

On the bench, in order, each result going into `docs/board-fnk0104s.md`:
1. First light: colours, inversion, orientation.
2. Frame rate at 40 MHz, then 80 MHz. Keep 80 if the glass is clean.
3. Touch: orientation, then calibration with `ota calib`.
4. Sound: cues play, and no pop when the amp switches.
5. Battery: the divider ratio against a meter, and the charge state against
   the charge LED.
6. Sleep: BOOT light-sleep and resume, then deep sleep and wake, then one
   overnight drain recorded in `DEVICE.md`.
7. The installer end to end, then one signed Wi-Fi update.

## Out of scope

The 2.8-inch and 3.5-inch FNK0104 variants (different panels and pins), the
RGB LED, the SD card, the microphone, rescaling the model's distance bands,
and merging the CYD fork.
