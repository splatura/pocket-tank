# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A virtual aquarium in which a 14.3M-parameter, 4-bit transformer (distilled from gemma4:26b) chooses every fish's goal. It runs on an ESP32-S3 and in a PC simulator. The same C code in `common/` compiles unchanged into both. Release number: `common/version.h` (currently 0.3.3 alpha).

## Commands

Simulator (LVGL v9 + SDL2; LVGL is cloned in-tree, not tracked):

```bash
git clone --depth 1 --branch v9.2.2 https://github.com/lvgl/lvgl.git sim/lvgl   # once
make -C sim                      # ./sim/fishsim (the 1.8 rectangle)
make -C sim ROUND=1              # ./sim/fishsim-round (1.75C pendant)
make -C sim WATCH=1              # ./sim/fishsim-watch (2.06 watch)
make -C sim FNK=1                # ./sim/fishsim-lcd40 (Freenove FNK0104S, 480x320)
make -C sim check                # every selftest, rectangle only
make -C sim check-all            # every selftest on all four boards: the gate for any change to common/
make -C sim NOMODEL=1 check-all  # skips --selftest-llm when model/out/model_q4.bin is missing
```

To run one selftest, use the binary directly from `sim/`, e.g. `./fishsim --selftest-shop` or `./fishsim-round --selftest-pop`. The full list is `SELFTESTS` in `sim/Makefile` (`--selftest`, `-hunger`, `-shop`, `-battery`, `-pop`, `-card`, `-sleep`, `-tend`, `-llm`, `-saves`, `-update`, `-bounds`). Useful flags: `--fresh`, `--fast N`, `--greedy`, `--narrate`, `--snapshot <prefix>` (writes PPM frames), `--bench`.

Other CI checks (`.github/workflows/checks.yml`):

```bash
cd firmware/host_test
cc -O2 -std=c11 -Wall -Wextra -Werror -I../../common -o audio_host audio_host.c ../../common/audio.c ../../common/sounds.c && ./audio_host
cc -O2 -std=c11 -Wall -Wextra -Werror -I../../common/llm -o q4_host q4_host.c ../../common/llm/q4_model.c ../../common/llm/word_tok.c -lm \
  && ./q4_host ../../model/out/model_q4.bin ../../model/out/tokenizer.bin "<state line> ->"
python3 -m compileall -q tools model
```

Firmware (ESP-IDF v5.4, `. ~/esp/esp-idf/export.sh` first). Builds are signed, so the first build needs `tools/ota_key.sh`, which writes `firmware/keys/`. That folder is never committed.

```bash
cd firmware && idf.py build      # the 1.8 (default)
idf.py -B build-round -DSDKCONFIG=build-round/sdkconfig "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.round" build
idf.py -B build-watch -DSDKCONFIG=build-watch/sdkconfig "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.watch" build
idf.py -B build-lcd40 -DSDKCONFIG=build-lcd40/sdkconfig "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.lcd40" build
cd firmware && ./run_qemu.sh     # boots in QEMU with stub display/touch; decisions go to the log
```

## Flashing a real device

Read `docs/DEVICE.md` before anything that resets a board. **Flash only through `tools/flash.sh`** (add `--model` to also write the model partition at 0x290000). It runs `tools/preflight.py` first, which archives the battery log and state to `docs/batlog/<date_time>.txt`, and it refuses to flash without that archive. Commit the archive together with the flash. The other boards use `tools/flash_round.sh`, `tools/flash_watch.sh` and `tools/flash_lcd40.sh` (`--build-only` just compiles; the FNK0104S has no preflight yet, `docs/DEVICE.md`). With several boards on USB, set `TANK_PORT=`.

## Architecture

Three layers, kept strictly apart, all in `common/`:

- **Reflex layer** (`tank.c`, `tank.h`): physics, steering, touch gestures, feeding, hunger, grass and algae, and the shop creatures. It runs every frame and never blocks on the model.
- **LLM advisor** (`advisor.c`, `llm/`): a llama2.c-style 4-bit engine (`q4_model.c`), a 54-token word tokenizer (`word_tok.c`) and the single shared state encoder (`advisor_core.c`). A fish is re-asked only when its `state_signature` changes. On the device, inference runs on the second core (`firmware/main/advisor_llm_esp.c`); the sim's equivalent is `sim/advisor_llm.c`.
- **Progression** (`progression.c`): growth, arrivals, trust, sand dollars, milestones and persistence through a platform port (`sim/persist_port_sim.c` writes to a file; `firmware/components/tankcore/persist_port_esp.c` writes to NVS).
- `render.c` is a software RGB565 renderer for the tank and every page (stats card, milestones, shop, placement, settings). `update.c` handles the OTA pages, `setup.c` the first-run flow, and `audio.c`/`sounds.c`/`notice.c` the cues and announcements.

The firmware pulls `common/` in through `firmware/components/tankcore` and `firmware/components/llm` (these CMake files point at `../../../common`). Hardware access is in `firmware/main/*_port_*.c`, with a `_stub.c` for each port used in QEMU.

**The model owns its decisions.** Never add fallback heuristics that override the model's chosen goal. Uncertainty shows up as visible hesitation. The reflex layer only carries out the goal, or stages presentations the model's state has earned.

The state/goal text format is frozen in `model/schema.md`. Changing it means retraining (`docs/pipeline.md`, `docs/retrain-v4.md`). The shipped model's tag, length and sha256 are pinned in `common/version.h`, and `tools/model_trailer.py --check` enforces them.

## Rules for changes (`docs/BOARDS.md`)

- A board is a compile-time world (`TANK_ROUND`, `TANK_WATCH`, `TANK_LCD40`). Board `#ifdef`s belong only in the geometry in `tank.h`, in layout-constant blocks in `render.h`/`render.c`/`update.c`, and in the ports. Feature logic that needs one is written against a pixel instead of a name; fix that instead.
- Pages draw in a 448×368 PAGE space centered at `PAGE_X`/`PAGE_Y`, while fish live in TANK coordinates. Anything that crosses between them subtracts `PAGE_X`/`PAGE_Y`. Put new page elements in PAGE x 56..392, y 60..300, the area every board shows in full.
- Write features once in `common/`. The platforms only route taps: pages return `MS_TAP_*`/`SHOP_TAP_*`-style codes, and the route is written twice, once in `sim/main.c` and once in `firmware/main/touch_port_ft3168.c` + `main.c`. Sounds go through `tank_emit`.
- Tests and tools find elements by asking the page (`render_milestones_card`, etc.) or through named layout constants. A literal pixel in a test only works on one board.
- **Save layout lock** (`progression.c`): new fields only ever go at the END of `save_t`, each with its own offset `_Static_assert`. Never edit an existing assert line. Saves must load on every board and every earlier release.
- `TANK_SCREEN_MANUAL` (watch and FNK0104S) means the way up is the keeper's SCREEN setting, saved in the tank save; `TANK_WORN` (the watch alone) is the wrist meaning. Feature code tests the flag, never the board.
- A change is done only when it passes `check-all`, `tools/board_sheet.py --selftest-card` gives a side-by-side picture of every board, and every firmware build compiles (`flash_lcd40.sh --build-only` for the FNK0104S). Compare `.bss` with the last build, since internal RAM headroom is about 23 KB.

## Releases

Bump `common/version.h` in the release commit. Pushing a `v*` tag runs `.github/workflows/release.yml`, which builds the published boards signed with the `OTA_SIGNING_KEY` secret, runs `tools/make_ota_manifest.py`, and attaches `latest-<board>.json` plus the images to the GitHub release. A tag with a suffix (e.g. `v0.3.0-rc1`) becomes a pre-release that tanks do not pick up as "latest". `tools/make_installer.py` builds the browser installer into `installer/dist/`. Commit messages follow the form `vX.Y.Z (alpha): <plain-English summary>`.

The boards a release and the installer carry are the rows of `tools/pt_boards.py` whose published flag is `True`; `tools/ci_build.sh` and both workflows read that list. The FNK0104S is known there (its images are recognised) but `False` until its bench acceptance passes (`docs/board-fnk0104s.md`); flipping the flag publishes it.
