"""pt_boards.py - the boards a release ships for, and which one an image is.

0.3.0 ships one image per board (docs/BOARDS.md). The image says which board
it is for: a marker right after its app descriptor (firmware/main/
net_port_esp.c, common/version.h PT_BOARD_MARKER_OFFSET), so the release tools
read the board from the image itself - a mixed-up build dir can never publish
the 1.8's image under the bowl's name.
"""
import os, sys

# id (version.h PT_BOARD), the maker's name, the sdkconfig fragment on top of
# sdkconfig.defaults (None = the 1.8, the default build), the build dir under
# firmware/ that CI uses, and PUBLISHED: whether a release and the installer
# carry it (2026-10-08: a board is known - its images are recognised - before
# it ships; the FNK0104S ships once its bench acceptance passes,
# docs/board-fnk0104s.md)
BOARDS = [
    ("amoled18",  "Waveshare ESP32-S3-Touch-AMOLED-1.8",   None,              "build",       True),
    ("round175c", "Waveshare ESP32-S3-Touch-AMOLED-1.75C", "sdkconfig.round", "build-round", True),
    ("watch206",  "Waveshare ESP32-S3-Touch-AMOLED-2.06",  "sdkconfig.watch", "build-watch", True),
    ("fnk0104s",  "Freenove FNK0104S",                     "sdkconfig.lcd40", "build-lcd40", False),
]


def published_rows(rows):
    """the rows a release and the installer carry"""
    return [b for b in rows if b[4]]


def build_lines(rows):
    """("<build dir>", "<sdkconfig fragment or ->") per row, for tools/ci_build.sh"""
    return [(b[3], b[2] or "-") for b in rows]


IDS = [b[0] for b in BOARDS]
PUBLISHED = [b[0] for b in published_rows(BOARDS)]
MAGIC = b"PTBOARD\0"
MARKER_OFFSET = 0x120          # the image header (24) + the first segment's header (8) + esp_app_desc_t (256)


def board_of_image(path):
    """the board id an app image is for; exits when it carries no marker"""
    with open(path, "rb") as f:
        f.seek(MARKER_OFFSET)
        mk = f.read(32)
    if mk[:8] != MAGIC:
        sys.exit(f"{path}: no board marker at 0x{MARKER_OFFSET:x} - built before 0.3.0's per-board images?")
    board = mk[8:].split(b"\0")[0].decode()
    if board not in IDS:
        sys.exit(f"{path}: board marker '{board}' is none of {IDS}")
    return board


def build_of_image(path):
    """the build id an app image carries (esp_app_desc_t.version, 32 bytes at 0x30: the
    firmware's PROJECT_VER, what the tank itself reports as its build)"""
    with open(path, "rb") as f:
        f.seek(0x20)
        desc = f.read(48)
    if desc[:4] != b"\x32\x54\xcd\xab":
        sys.exit(f"{path}: no app descriptor at 0x20")
    return desc[16:48].split(b"\0")[0].decode()


def name_of(board):
    return next(b[1] for b in BOARDS if b[0] == board)


if __name__ == "__main__":
    if sys.argv[1:] == ["--published-dirs"]:
        for b in published_rows(BOARDS):
            print(b[3])
    elif sys.argv[1:] == ["--published-builds"]:   # "<dir> <fragment or ->" per line, for tools/ci_build.sh
        for d, frag in build_lines(published_rows(BOARDS)):
            print(d, frag)
    elif sys.argv[1:] == ["--known-builds"]:       # the same, for every known board: CI compiles them all (published or not)
        for d, frag in build_lines(BOARDS):
            print(d, frag)
    else:
        sys.exit("usage: pt_boards.py --published-dirs | --published-builds | --known-builds")
