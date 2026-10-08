# The Freenove FNK0104S as a fourth board: design

2026-10-08. Status: revision 2, after an adversarial review (Codex,
gpt-6.1-sol, high effort). Awaiting review by the user.

Changes made for the review are tagged **(R#n)**, where n is the number of
the review finding. The review and the response to each finding are in the
appendix.

## Goal

Run the tank on the Freenove FNK0104S (4.0-inch IPS, ESP32-S3) as a full fourth
board with the same standing as the 1.8, the pendant and the watch. That
means a simulator world, `check-all`, its own signed image and update
manifest, a place in the browser installer, and a flash script. One source
tree, one save layout, one model (docs/BOARDS.md).

The board is on its way. Everything that does not need the glass is built and
tested first. The board joins a public release only once the bench
acceptance below passes.

## The board

The FNK0104S is QDtech's ES3C40P under Freenove's name. Sources:
- Freenove's repository (github.com/Freenove/Freenove_ESP32_S3_Display): the
  4.0-inch schematic, the `FNK0104S_4.0_320x480_ST7796.h` TFT_eSPI setup, the
  example sketches' pin defines, and the bundled datasheets.
- QDtech's manual (lcdwiki.com/res/ES3C40P/4.0inch_IPS_ESP32-S3_ES3C40P_User_Manual.pdf).

Freenove's README calls the panel "ST7789"; its setup file and the vendor
manual both say ST7796S. This design follows ST7796S.

| part | FNK0104S | against the 1.8 |
|---|---|---|
| chip | ESP32-S3, 16 MB QSPI flash, 8 MB octal PSRAM | the same: partitions.csv and the model partition unchanged |
| screen | ST7796S IPS LCD, 320 x 480, 4-wire SPI | new port |
| touch | FT6336 (FocalTech, I2C 0x38) | the FT3168's family: reused |
| audio | ES8311 (I2C 0x18) + FM8002E amp, enabled LOW | the same codec: reused, its pins |
| power | TP4054 charger; the cell through R14 = R15 = 100 k to GPIO9 (ADC1_CH8) | no PMIC, no fuel gauge, no charge signal to the chip, no power-off |
| IMU | none | no live flip, no motion handling |
| RTC | none | the chip's RTC timer runs through deep sleep; a power cut loses the time |
| keys | BOOT (GPIO0), RESET (CHIP_PU, which also resets the LCD) | BOOT is the sleep key |
| other | WS2812 on GPIO42, micro-SD, two LDOs | unused, but powered (they set the sleep floor) |

Pins (Freenove's FNK0104AB/S defines, which agree with the 4.0-inch schematic;
none fall in GPIO26-37, the flash/PSRAM lines):

| signal | GPIO | note |
|---|---|---|
| LCD CS / MOSI / SCLK / DC | 10 / 11 / 12 / 46 | 46 is a boot strap (R#15) |
| LCD reset | none | shared with CHIP_PU: never pulsed alone |
| backlight (PWM, high = on) | 45 | VDD_SPI voltage strap (R#15) |
| I2C SDA / SCL | 16 / 15 | |
| touch RST / INT | 18 / 17 | |
| I2S MCLK / BCLK / WS / DOUT / DIN | 4 / 5 / 7 / 8 / 6 | |
| amp enable (low = on) | 1 | RTC-capable: holdable high in deep sleep |
| battery voltage | 9 | ADC1_CH8: no conflict with Wi-Fi |
| BOOT key | 0 | boot strap, ext0 wake source |

Prior art: lmoiseichuk's port of the 2.8-inch sibling (ES3C28P, same codec,
touch, battery and audio pins; github.com/lmoiseichuk/pocket-tank-cyd,
`docs/CYD.md`). It predates 0.3. It is used as a reference only, not merged,
and its stripe algorithm is not reused as is (R#9).

## 1. The world: `TANK_LCD40`

A compile-time world, beside `TANK_ROUND` and `TANK_WATCH`.

- **Geometry** (`common/tank.h`): `TANK_W 480`, `TANK_H 320`. The walls are the
  frame (`TANK_FX0 0`, `TANK_FX1 TANK_W`), and `TANK_BOT TANK_H`.
- **Algae grid:** `ALGAE_CELL 16`, 30 x 20 = 600 cells, inside the 644 the save
  reserves. Offsets do not move (progression.c's SAVE LAYOUT LOCK). A save
  lives on its own board's flash and never moves between boards, so the
  requirement is a fixed layout, not spatial portability. A save written on
  this board reads back on this board (R#3).
- **Fronds:** the segment height is scaled so a full frond's tip sits just
  under the surface of a 320 px tank, as the watch's is for its height.
- **Model:** unchanged. The encoder's distance bands (70 / 180 / 380 px of a
  448-wide tank) stay: 480 is close to 448.
- **Ids:** `PT_BOARD "fnk0104s"`, `PT_BOARD_NAME "Freenove FNK0104S"`
  (`common/version.h`).
- **Propagation (R#11):** `firmware/components/tankcore/CMakeLists.txt` adds a
  PUBLIC `TANK_LCD40` for `CONFIG_POCKET_TANK_LCD40`, as it does for the round
  board and the watch. Every component sees the same world and the same
  `PT_BOARD`. The pre-hardware checks read the built image's marker back with
  `tools/pt_boards.py` and grep the generated sdkconfig.
- **Simulator:** `make FNK=1` builds `fishsim-lcd40` in `build-lcd40/`. The
  `clean` target and `check-all` gain it (R#16), and `check-all` runs every
  selftest in four worlds. `tools/board_sheet.py` adds the fourth glass.

### Layout on a 320 px glass (R#4)

The 448 x 368 PAGE space sits centred: `PAGE_X 16`, `PAGE_Y -24`. The glass
shows page y 24..344. The box every glass shows whole (x 56..392,
y 60..300) fits. That is not enough on its own. These elements fall in the
lost strips or past the bottom, and each gets a layout block for
`TANK_LCD40`, kept beside the watch's blocks:

| element | where it is now | the fix |
|---|---|---|
| fish card + TOOLBOX (TANK coords) | card y 8..266, toolbox y 280..350, icons ~291..338 | the toolbox moves beside the card (to its right, same top) on this board; `RENDER_TOOLS_X/Y` and `render_tools_hit` follow the constants |
| SETTINGS title | PAGE y 14 (frame y -10) | moves down into the visible band |
| version line on settings | PAGE y 354 (frame y 330, off the glass) | moves up beside the foot buttons |
| setup page dots | `PAGE_H - 14` (frame y 330) | raised into view |
| setup / update panel (`UY 16`, `UH 326`) | frame y -8..318 | its own `UY` / `UH` for a 320 px glass |
| milestones page edge rows (`MSP_*`) | top and foot rows | a block of its own, as the watch has |

The **audit method** is part of the work, not a one-off: the snapshot set
(`--snapshot`, every page) is rendered in the LCD40 world, and a new selftest
(`--selftest-bounds`) asks each page for its interactive rectangles (buttons,
hit boxes, the toolbox) and fails any that are not wholly inside the glass.
Centres are not enough. Every page's tap targets are already reachable by
name (BOARDS.md rule 3), so the test uses the existing accessors plus the
few the table above adds.

## 2. Firmware ports

The build is `sdkconfig.defaults` plus `sdkconfig.lcd40`:
`CONFIG_POCKET_TANK_LCD40=y` and `CONFIG_POCKET_TANK_DISPLAY_SH8601=n` with the
new `CONFIG_POCKET_TANK_DISPLAY_ST7796=y`. Kconfig makes the combinations
exclusive (R#11): LCD40 depends on !ROUND and !WATCH, ST7796 depends on LCD40,
and LCD40 forbids SH8601. `firmware/main/CMakeLists.txt` picks the display and
battery sources and adds `esp_adc` and `esp_driver_ledc` to `REQUIRES` (R#6).
Pins go in a `/* ---- the FNK0104S ---- */` block of `board_pins.h` with an `F_`
prefix.

### The display: `display_port_st7796.c`

**The contract (R#11).** It implements every function in `display_port.h`,
plus the symbols other ports take from the display port today:
`board_i2c_bus()`, `board_is_v2()` (false), `board_has_expander()` (false),
`board_is_round()` / `board_is_watch()` (false), `board_pwr_sense_pin()` (-1),
and the new `board_is_lcd40()` (true). The implementation plan lists them
from a link of the LCD40 build, so nothing is missed by reading.

**Bus.** SPI2 through `esp_lcd` panel IO at `CONFIG_POCKET_TANK_LCD40_SPI_MHZ`,
default 40. The panel scans landscape through MADCTL, and the frame is
byte-swapped into the stripes.

**Stripes (R#8, R#9).** Two DMA-capable stripes of **20 rows** each:
480 x 20 x 2 = 19,200 bytes, 38,400 in all, against the 1.8's 47,104 today.
320 rows give 16 stripes, an even count. Each stripe has its own completion
flag set by the `on_color_trans_done` callback. The writer waits for the
specific buffer it is about to fill, and the next-buffer index carries across
frames, so ownership never depends on the stripe count. `display_port_sleep`,
`set_inverted` and error paths drain both buffers first. The heap allocation
is checked at boot, and the log records internal and DMA free heap, the
largest DMA block, and whether the tank task started. This is recorded at
boot, in update mode, during time sync, and with inference and audio running.

**Speed (R#7).** A frame is 307,200 bytes: 61.4 ms on the wire at 40 MHz
(16.3 fps at most), 30.7 ms at 80 MHz (32.6 fps at most), less render and
gaps. The main loop caps at 25 fps. The ST7796S datasheet (p.54) gives a
66 ns minimum write cycle, about 15 MHz. Freenove ships 80 MHz. So:
- **The target** is 40 MHz as the release default, about 15 fps, which is
  acceptable for a desk tank. 40 MHz is already past the datasheet's figure,
  as is common for these panels, and Freenove's own examples run faster.
- **80 MHz** becomes the default only if it passes a soak: an hour with
  Wi-Fi, audio and inference active, twenty sleep/wake cycles, and a cold
  board, with no corrupted frames. Until then it is a bench option, recorded
  as empirical operation outside the datasheet.

**Brightness and sleep (R#15).** LEDC PWM on GPIO45, kept across sleep/wake.
Sleep is: backlight off, `DISPOFF`, `SLPIN`, then at least 5 ms before
anything else. Wake is: `SLPOUT`, at least 120 ms (and never `SLPIN` within
120 ms of `SLPOUT`), `DISPON`, one valid frame sent, then the backlight up,
so no garbage flashes. The LCD shares the chip's reset, so light sleep never
resets it.

**Flip.** `display_port_set_inverted` toggles both MADCTL mirrors between
frames (after a drain).

**Deep-sleep pins (R#2).** `display_port_deep_sleep_pins` holds CS (10) high,
the backlight (45) low and the touch reset (18) low. The pin-holding step is
called on every deep sleep (section 3), not only on boards with a power-key
sense line.

### Touch, audio, clock

**Touch** (`touch_port_ft3168.c`): a `board_is_lcd40()` branch resets on GPIO18
and reads the FT6336 at 0x38 through the FT5x06 path. It gets its own
calibration and orientation (the sibling's panel read turned 180 degrees),
and follows `touch_port_set_inverted`. After a light-sleep resume it is
re-initialised explicitly (R#2).

**Audio** (`audio_port_es8311.c`): the pin macros gain the board's values, and
the amp's level becomes `AMP_ON_LEVEL` (0 here, 1 elsewhere). The pin is
driven to off before it becomes an output, and `audio_port_deep_sleep_pins`
holds it HIGH (off) here. The codec's analog-rail call finds no PMIC and does
nothing.

**Clock (R#1).** See "Capabilities" in section 3: network time is tied to
"no RTC chip", not to "has a PMIC".

**No IMU:** `imu_port_init` finds nothing. Handling, for the idle light,
comes from touch alone.

## 3. Power, battery, sleep and the SCREEN setting

### Capabilities instead of `s_pmic` (R#1)

Today `s_pmic` (an AXP2101 answered) answers four different questions in
`main.c`: whether a PWR key exists, whether a power-off is possible, whether
the BOOT short press is the sleep key, and whether network time sync runs.
The battery port gains a small capability call, and main.c asks the
question it means:

| question | 1.8 / pendant / watch | FNK0104S |
|---|---|---|
| `battery_port_has_meter()` | true (fuel gauge) | true (ADC estimate) |
| `battery_port_can_power_off()` | true | false |
| `battery_port_has_pwr_key()` | true | false: BOOT is the sleep key |
| network time at boot | `!s_rtc && net_saved()` (today also required `s_pmic`) | the same expression: runs |

- `night_powers_off()` asks `can_power_off()`, so the FNK0104S's night ends in
  deep sleep.
- `sleep_button_poll`'s "BOOT short press sleeps" branch asks
  `!has_pwr_key()`. It is the one owner of BOOT here, the same handler that
  runs today on a board with no PMIC, including the BOOT + tap RESET chord.
  `pwr_key_poll` and the long press do not exist on this board (R#13).
- `net_clock_boot` drops its `!s_pmic` condition. On the other three boards
  the result is unchanged, because each has a PMIC. The pre-hardware checks
  include the other three boards' boot logs for that.

### The meter: `battery_port_adc.c` (R#5, R#6)

- **Never blocks.** A 1 s `esp_timer` callback takes one ADC sample per tick
  into a ring of 8. `battery_port_read` returns the cached median at once.
  Until four samples exist, and on any calibration failure, it returns false,
  and the pill hides as it does today with no gauge.
- **The reading.** ADC1_CH8 at 12 dB attenuation (the pin sees about 2.1 V at
  full), the curve-fitting calibration, and the divider ratio
  `F_BAT_DIVIDER 2.0` (R14 = R15 = 100 k per the schematic, checked against a
  meter on the bench). A one-shot handle is used under a mutex, because
  update mode reads it too.
- **The estimate.** A single-cell LiPo table (4.20 V = 1.0 ... 3.30 V = 0.0).
  It is valid as an open-circuit estimate only while discharging at the
  tank's steady draw. The percentage gets 2% hysteresis and is labelled an
  estimate on the battery page.
- **Three separate facts**, which the current single state blurs:
  1. **level:** the estimate above.
  2. **external power:** confirmed only when the USB-Serial-JTAG port sees a
     host (VBUS present). Then the state is `BAT_PLUGGED`, never
     `BAT_CHARGING` or `BAT_FULL`, because no charge signal reaches the chip.
  3. **charging:** unknown on this board. The pill's bolt shows only with
     confirmed external power, and the voltage trend is never used to claim
     power.
- **Updates stay safe.** `update_mode.c`'s `on_power` comes only from
  confirmed external power. A wall charger (no USB host) reads as on
  battery, so the update's low-battery rule applies. That is the
  conservative failure: an update that asks for a cable, never one that
  starts on a flat cell.
- **No battery fitted** (USB only): the voltage reads at the charger's
  float, about 4.2 V, with a USB host seen, so the tank shows PLUGGED. With no
  host and no cell, the readings are whatever the charger outputs. The
  bench checks this and records it.

### Sleep (R#2, R#13)

- **The grace is 60 minutes** on this board: the existing
  `SLEEP_GRACE_CLOCKLESS_US`, the rule for a board with no RTC chip. A press
  within the grace resumes in place. After it, the board deep-sleeps with
  BOOT as ext0.
- **Release before arming.** Every path that arms a wake level waits for BOOT
  to read high first. `enter_sleep_for` does this today. `enter_poweroff` does
  not, and is not reachable on this board, because there is no long press.
  The plan adds the same wait to it anyway.
- **After an ext0 wake,** the boot calls `rtc_gpio_deinit(GPIO0)` before BOOT
  is configured as a digital input.
- **Deep sleep preparation** in `deep_sleep_now` is split from the choice of
  wake source. Touch sleep or reset, panel sleep, the bus and
  `display_port_deep_sleep_pins` run first, for every board. Then ext1 (a
  PWR-sense board) or ext0 (BOOT) is armed. The `sleepcfg` bits keep
  working as they do.

### The SCREEN setting (R#10)

The watch already has a saved SCREEN setting (`screen_turned`, save offset
1672) that turns the picture 180 degrees. The FNK0104S needs exactly that:
a 180-degree turn chosen by the keeper. So:
- `common/tank.h` splits `TANK_WORN` into two flags. `TANK_WORN` keeps the
  wrist meaning (the tall settings layout, the AUTO learning from taps).
  The new `TANK_SCREEN_MANUAL` (watch or LCD40) means the way up is the
  keeper's setting. `tank_screen_turned` / `tank_screen_set` test
  `TANK_SCREEN_MANUAL`.
- On the FNK0104S settings page, the SCREEN row (NORMAL / TURNED) replaces
  the ROTATION lock row, which means nothing without an IMU. It sits in the
  same row slot, so no layout moves.
- Storage stays where the watch's is: the tank save, through
  `progression_settings_changed`, like ROTATION and AUTO FEED. This revises
  revision 1, which put it in NVS. The save already holds the keeper's
  settings, and a tank reset returning SCREEN to NORMAL is the same as the
  watch.
- `main.c` uses `tank_screen_turned` when `TANK_SCREEN_MANUAL`, and the IMU
  flip otherwise. The simulator's `T` key (the watch's pretend wearer) works
  in the LCD40 world too.
- Update mode runs before the save is loaded (main.c calls
  `update_mode_run` ahead of `progression_boot` / `progression_wake`, which load it), and so do the boot's
  time-sync and provisioning pages. A new `progression_peek_screen()` reads
  the save's `screen_turned` byte (offset 1672) without loading the tank, so
  those pages are drawn the right way up too. Setup runs after the load and
  needs nothing extra.

## 4. Release, installer, flashing, docs

### Two board lists (R#12)

`tools/pt_boards.py` gains a published flag:
`("fnk0104s", "Freenove FNK0104S", "sdkconfig.lcd40", False)`.
- `IDS` stays **every known** board, so the tools recognise an unreleased
  image. `PUBLISHED` is the published subset.
- `release.yml` and `installer.yml` stop hard-coding builds. A small step
  prints the published boards' build commands and expected id **set** from
  `pt_boards.py`, and the workflows build exactly those. The release's
  manifest check compares the produced `latest-*.json` names with that set
  instead of counting to 3. So turning the flag on is the one change that
  publishes the board, and leaving it off never breaks the other three.
- `make_installer.py` takes boards from `PUBLISHED`, with
  `--include-unpublished` for a local test page.
- `installer/index.html` gains the FNK0104S's `LABEL` and `NOTE` ("4.0 inch
  (Freenove)": an LCD, battery estimated, BOOT to sleep). It shows only
  boards present in the manifests it is given, so an unpublished board never
  appears.
- Nothing else in the update path changes. The board marker, the per-board
  manifest and the image check already derive from `PT_BOARD`.

### Flashing (R#16)

`tools/flash_lcd40.sh` is modelled on `flash_watch.sh`: its own build dir
`fw-build-lcd40`, the board found by `LCD40_SERIAL` (`tools/boards.local.sh`),
`--build-only`, and `--full` for a blank or factory board (it clears NVS and
writes the model, bootloader, table and app). Like the watch's, it runs no
preflight while this board holds no tank anyone would miss. A factory board
runs Freenove's firmware, which has no director, and `preflight.py` would
refuse it. `docs/DEVICE.md` records when that changes, and the script gains
the preflight at the same time the 1.8's has it.

### Docs

`docs/board-fnk0104s.md` covers the pins and sources, the traps (straps,
active-low amp, no charge signal, the SPI speed and the datasheet, shared
LCD reset) and the bench results. `BOARDS.md`, the README (board table,
install, layout), `CLAUDE.md` and `docs/OTA.md` gain the fourth board. Each
of these is checked and either changed or recorded as needing no change:
`checks.yml` (picks up `check-all`), `common/update.c` / `update.h` (panel
layout), `net_port_esp.c` (marker), `make_ota_manifest.py` (derives from the
marker) and `docs/AUDIO.md` (amp polarity).

## Testing

### Before the board arrives

1. `make -C sim check-all` passes in four worlds, including the new
   `--selftest-bounds` (R#4).
2. `--selftest-saves` in the LCD40 world: a save written and read back with
   all 600 cells non-zero, and the 1.8's save offsets unchanged (R#3).
3. `board_sheet.py --selftest-card` and the `--snapshot` set on four glasses,
   checked by eye, with the fish card and toolbox open.
4. Four firmware builds compile. The LCD40 image's marker reads back as
   `fnk0104s`, and its sdkconfig has LCD40 + ST7796 and not SH8601 (R#11).
5. The 1.8, pendant and watch builds produce the same snapshots as before,
   and their boot logs show the capability change made no difference: time
   sync, sleep key and night as before (R#1).
6. The release workflows dry-run, from a script, with the flag off (three
   manifests, release passes) and on (four).

### Bench acceptance, once the board arrives

Results go into `docs/board-fnk0104s.md`. The board is published only when
all of these pass:

1. **First light and boot (R#15):** colours, inversion and orientation. Cold
   boot, RESET, BOOT+RESET (download mode) and a software restart all come
   up clean.
2. **Display soak (R#7):** 40 MHz fps recorded. 80 MHz is adopted only after
   the soak in section 2.
3. **Touch:** orientation in both SCREEN settings. Calibration with
   `ota calib`. The four corners and edges land within the calibration's
   tolerance.
4. **Sound:** cues play, and no pop when the amp switches.
5. **Battery (R#5):** the divider against a meter (3 points from full to
   about 3.5 V). USB-host detection true/false. Wall-charger behaviour.
   No-cell behaviour.
6. **Sleep current (R#14):** measured at the battery terminal with a meter,
   unplugged, backlight off, no SD card, the RGB LED off. Recorded for
   awake, the light-sleep grace and deep sleep, plus 20 repeated
   sleep/wake cycles with a held-key wake each time. **The floor the parts
   set:** two ME6217 LDOs at about 100 uA each, the divider at about 21 uA,
   and the WS2812's quiescent draw (0.6 mA in its datasheet at 5 V, less at
   3.3 V), so about 0.5 to 0.9 mA before the ESP32. **Acceptance:** deep
   sleep within 0.5 mA of the floor measured with the ESP32 held in reset,
   which proves the firmware adds nothing. The result and the battery life
   it implies are published in the board doc and the installer note.
7. **Updates:** the installer end to end on a factory board. One signed
   Wi-Fi update. A wrong-board image refused. An interrupted update rolls
   back.

## Out of scope

The 2.8-inch and 3.5-inch FNK0104 variants (different panels and pins), the
RGB LED (left off), the SD card, the microphone, rescaling the model's
distance bands, merging the CYD fork, and spatial remapping of saves between
boards (saves do not travel; R#3).

## Appendix: the review and the response

Adversarial review of revision 1, 2026-10-08, by Codex (gpt-6.1-sol, high
reasoning effort, read-only). 16 findings. All were checked against the
code before acting.

| # | severity | finding | response |
|---|---|---|---|
| 1 | blocker | `s_pmic` gates time sync, sleep key and power-off | accepted: capabilities (section 3) |
| 2 | major | deep-sleep pin hook only on PWR-sense boards | accepted: prep split from wake source |
| 3 | major | cross-board save meaning / 644 to 600 to 644 loss | partly: saves never travel between boards, so the test is reworded to same-board round trips; no remapping |
| 4 | major | toolbox (TANK coords) and other elements cut off | accepted: per-element table + `--selftest-bounds` |
| 5 | major | USB / voltage trend cannot prove charging; OTA guard | accepted: three facts, `on_power` from USB host only |
| 6 | major | ADC sampling could block; calibration failure | accepted: timer-cached, failure hides the pill |
| 7 | major | SPI speed vs ST7796S datasheet timing | accepted: 40 MHz default, 80 MHz only after soak |
| 8 | major | stripes are heap, not `.bss`; budget unmeasured | accepted: 20-row stripes, 38,400 B, logged at four points |
| 9 | major | sibling's stripe ownership assumption | accepted: per-buffer completion, index across frames |
| 10 | major | SCREEN exists only for `TANK_WORN` | accepted, with a change: reuse the watch's saved setting via `TANK_SCREEN_MANUAL`, in the save (revises rev 1's NVS) |
| 11 | major | world propagation and display-port symbols | accepted: tankcore PUBLIC define, symbol list, exclusive Kconfig |
| 12 | major | `installer.yml` missed; count of 4 would break releases | accepted: known vs published lists drive both workflows |
| 13 | major | grace is 60 min clockless; key semantics; GPIO0 deinit | accepted |
| 14 | major | no sleep-current criterion | accepted: meter at the terminal, within 0.5 mA of the parts' floor |
| 15 | minor | straps and LCD sleep timing | accepted: boot matrix + command timing |
| 16 | minor | incomplete file list, preflight on a factory board | accepted: file audit list, `--full`, no preflight yet |
