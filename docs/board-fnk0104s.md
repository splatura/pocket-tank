# Fourth board: Freenove FNK0104S (4.0" ST7796S LCD, 480 x 320) - port notes, 2026-10-08

The FNK0104S is QDtech's ES3C40P under Freenove's name. The board has not
arrived yet. Everything below is built from the vendor's files and tested
in the simulator and by compiling; the bench results at the end are empty
until the board is on the desk. **The board is known to the tools but not
published**: no release or installer carries it until its row in
`tools/pt_boards.py` is set to `True`, and that waits for the bench
acceptance (docs/superpowers/specs/2026-10-08-fnk0104s-board-design.md).

Sources:
- Freenove's repository, github.com/Freenove/Freenove_ESP32_S3_Display: the
  4.0-inch schematic, the `FNK0104S_4.0_320x480_ST7796.h` TFT_eSPI setup, the
  example sketches' pin defines and the bundled datasheets. The commit read
  was not recorded when the design was written; record it here on the first
  bench day.
- QDtech's manual, lcdwiki.com/res/ES3C40P/4.0inch_IPS_ESP32-S3_ES3C40P_User_Manual.pdf.
- lmoiseichuk's port of the 2.8-inch sibling (github.com/lmoiseichuk/pocket-tank-cyd,
  its CYD notes), as a reference only. Nothing is merged from it.

Freenove's README calls the panel "ST7789"; its setup file and the vendor
manual both say ST7796S. This port follows ST7796S.

## The parts, against the 1.8

| part | FNK0104S | against the 1.8 |
|---|---|---|
| chip | ESP32-S3, 16 MB QSPI flash, 8 MB octal PSRAM | the same: partitions.csv and the model partition unchanged |
| screen | ST7796S IPS LCD, 320 x 480 native, 4-wire SPI, driven landscape (480 x 320) | new port, `display_port_st7796.c` |
| touch | FT6336 (FocalTech, I2C 0x38) | the FT3168's family, read directly in `touch_port_ft3168.c` |
| audio | ES8311 (I2C 0x18) + FM8002E amp, enabled LOW | the same codec, other pins |
| power | TP4054 charger; the cell through R14 = R15 = 100 k to GPIO9 (ADC1_CH8) | no PMIC, no fuel gauge, no charge signal to the chip, no power-off |
| IMU | none | no live flip, no motion handling |
| RTC | none | no RTC chip; the chip's own RTC timer runs through deep sleep, a power cut loses the time |
| keys | BOOT (GPIO0), RESET (CHIP_PU, which also resets the LCD) | BOOT is the sleep key |
| other | WS2812 on GPIO42, micro-SD, two LDOs | unused but powered: they set the sleep floor |

## The pins

From Freenove's FNK0104AB/S defines, which agree with the 4.0-inch schematic.
None fall in GPIO26-37, the flash and PSRAM lines. They are the `F_` block of
`firmware/main/board_pins.h`; where the code and the design spec differ, the
code wins.

| signal | GPIO | note |
|---|---|---|
| LCD CS / MOSI / SCLK / DC | 10 / 11 / 12 / 46 | 46 is a boot strap |
| LCD reset | none | shared with CHIP_PU: never pulsed alone |
| backlight (PWM, high = on) | 45 | the VDD_SPI voltage strap |
| I2C SDA / SCL | 16 / 15 | |
| touch RST / INT | 18 / 17 | INT is unused: the touch is polled |
| I2S MCLK / BCLK / WS / DOUT / DIN | 4 / 5 / 7 / 8 / 6 | DIN (the mic) is unused |
| amp enable (low = on) | 1 | RTC-capable: held high (off) in deep sleep |
| battery voltage | 9 | ADC1_CH8: no conflict with Wi-Fi |
| BOOT key | 0 | boot strap, the deep-sleep wake (ext0) |

## The build

`tools/flash_lcd40.sh`, or by hand with `sdkconfig.defaults;sdkconfig.lcd40`
(`CONFIG_POCKET_TANK_LCD40=y`, `..._DISPLAY_ST7796=y`, `..._DISPLAY_SH8601=n`)
into its own build directory. The id is `fnk0104s`
(`common/version.h` `PT_BOARD`). The simulator is `make -C sim FNK=1` ->
`./fishsim-lcd40`.

**The world.** The tank is 480 x 320. The pages are laid out in the 448 x 368
PAGE space as on the other boards; the 320 px glass is 48 px shorter than the
page, so it shows page y 24..344 (`PAGE_Y` -24, `PAGE_X` 16). The toolbox
stands beside the fish card here, not below it. `--selftest-bounds` checks
that every named tap target lies on the glass.

**Display.**
- SPI2 at `CONFIG_POCKET_TANK_LCD40_SPI_MHZ`, default 40 MHz. 80 MHz is a
  bench option only, until the soak below passes.
- The frame goes out in two 20-row DMA stripes (480 x 20 x 2 = 19,200 bytes
  each, 38,400 in all), each with its own completion flag.
- Backlight: LEDC PWM on GPIO45.

A frame is 307,200 bytes: 61.4 ms on the wire at 40 MHz (16.3 fps at most),
30.7 ms at 80 MHz. The main loop caps at 25 fps.

**Touch.** The FT6336 is read directly, as the watch's FT3168 is. Its turn
is `F_TOUCH_TURN` (touch_port_ft3168.c), set on the bench, not yet: it
defaults to 0 until the board shows which way the panel reads.

**Battery.** An ADC estimate on GPIO9 times `F_BAT_DIVIDER` 2.0 (R14 = R15 =
100 k from the schematic; to be measured on the bench). A 1 s timer fills a
ring of 8 samples, so a read never blocks. The state is PLUGGED only while a
USB host is seen; it is never CHARGING or FULL, because no charge signal
reaches the chip. A wall charger has no host, so it reads as battery, and the
update's low-battery rule applies: below 20% it asks for a cable. That is the
safe failure.

**Sound.** The ES8311 on the pins above. The FM8002E amp is on when its pin
is LOW (`AMP_ON_LEVEL` 0 in audio_port_es8311.c); docs/AUDIO.md has the rule.

**Sleep.** There is no power-off. A short press of BOOT sleeps. The grace is
60 minutes (`SLEEP_GRACE_CLOCKLESS_US`, the rule for a board with no RTC
chip), and a press inside it resumes in place. After it the board deep-sleeps,
and BOOT wakes it. BOOT is released before it is armed as the wake.

**SCREEN.** Settings has SCREEN: NORMAL / TURNED in the ROTATION row's place,
because there is no IMU to lock. It turns the picture 180 degrees, is
`TANK_SCREEN_MANUAL` in the code, and is saved in the tank save (the same
byte as the watch's). A tank reset returns it to NORMAL.

**Flashing.** `tools/flash_lcd40.sh` finds the board by its USB serial
(`LCD40_SERIAL` in `tools/boards.local.sh`). `--build-only` compiles only;
`--full` is for a blank or factory board (clears NVS, writes the model,
bootloader, table and app). No preflight yet: a factory board runs Freenove's
firmware, which has no director, and no tank on this board is one anybody
would miss. docs/DEVICE.md says when that changes.

## Traps

1. **GPIO45 is the VDD_SPI strap, and GPIO46 and GPIO0 are boot straps.**
   45 is the backlight, 46 is the LCD's DC, 0 is BOOT. The board's own pulls
   set their reset levels. Never add an external pull to any of them.
2. **The amp is active LOW.** The pin is driven off before it becomes an
   output, and held HIGH (off) through deep sleep. The 1.8's amp is the
   other way round.
3. **There is no charge signal.** The pill never shows CHARGING, only PLUGGED,
   and only while a USB host is seen. A wall charger reads as battery.
4. **40 MHz SPI is past the ST7796S datasheet.** Its write cycle is 66 ns at
   best (about 15 MHz, p.54). Freenove ships 80 MHz and these panels commonly
   take more than the sheet says, but it is empirical. 40 MHz is the default;
   80 MHz is adopted only after a soak: an hour with Wi-Fi, audio and
   inference running, twenty sleep/wake cycles and a cold board, with no
   corrupted frame.
5. **The LCD shares CHIP_PU.** RESET resets the panel too, so the LCD is never
   reset alone; light sleep never resets it. Sleep is backlight off, DISPOFF,
   SLPIN; wake is SLPOUT, 120 ms, DISPON, one frame, then the backlight.
6. **The WS2812 and the two LDOs draw in deep sleep.** That is the floor the
   parts set: two ME6217 LDOs at about 100 uA each, the divider at about
   21 uA and the LED's quiescent draw, so about 0.5 to 0.9 mA before the
   ESP32. Deep sleep is accepted within 0.5 mA of the floor measured with the
   ESP32 held in reset.

## Bench results

Nothing has been run on the board. One row per bench step of the design spec;
the board is published only when every row passes.

| step | what | date | result | by |
|---|---|---|---|---|
| 1 | First light and boot: colors, inversion, orientation; cold boot, RESET, BOOT+RESET (download mode), software restart | | not yet | |
| 2 | Display soak: fps at 40 MHz recorded; 80 MHz only after the soak in trap 4 | | not yet | |
| 3 | Touch: orientation in both SCREEN settings (`F_TOUCH_TURN`), calibration with `ota calib`, four corners and edges | | not yet | |
| 4 | Sound: cues play, no pop when the amp switches | | not yet | |
| 5 | Battery: the divider against a meter (3 points from full to about 3.5 V), USB-host detection true / false, wall-charger behavior, no-cell behavior | | not yet | |
| 6 | Sleep current at the battery terminal (unplugged, backlight off, no SD, LED off): awake, the light-sleep grace, deep sleep, 20 sleep/wake cycles; deep sleep within 0.5 mA of the floor | | not yet | |
| 7 | Updates: the installer end to end on a factory board, one signed Wi-Fi update, a wrong-board image refused, an interrupted update rolls back | | not yet | |
