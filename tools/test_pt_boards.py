"""tools/test_pt_boards.py - the board lists (2026-10-08): every known board is
recognised, only published ones ship, and the CI helpers agree with the list."""
import os, subprocess, sys, unittest
sys.path.insert(0, os.path.dirname(__file__))
import pt_boards

PT_BOARDS = os.path.join(os.path.dirname(__file__), "pt_boards.py")

def cli(flag):
    return subprocess.run([sys.executable, PT_BOARDS, flag], capture_output=True, text=True, check=True).stdout

def cli_builds(flag):
    return [tuple(line.split()) for line in cli(flag).splitlines()]

def fnk_published():
    """the board list as it reads the day the FNK0104S's flag is turned on (in this
    process only: the committed flag is never touched)"""
    rows = [b[:4] + (True,) if b[0] == "fnk0104s" else b for b in pt_boards.BOARDS]
    assert rows != pt_boards.BOARDS
    return rows

def manifest_names(published_ids):
    """the manifests release.yml expects: 'latest-<board>.json' per published board"""
    return {"latest-%s.json" % b for b in published_ids}

class Lists(unittest.TestCase):
    def test_known_has_all_four(self):
        self.assertEqual(pt_boards.IDS, ["amoled18", "round175c", "watch206", "fnk0104s"])
    def test_fnk_known_not_published(self):
        self.assertIn("fnk0104s", pt_boards.IDS)
        self.assertNotIn("fnk0104s", pt_boards.PUBLISHED)
    def test_published_is_the_shipped_three(self):
        self.assertEqual(pt_boards.PUBLISHED, ["amoled18", "round175c", "watch206"])
    def test_published_dirs_cli(self):
        self.assertEqual(cli("--published-dirs").split(), ["build", "build-round", "build-watch"])
    def test_published_builds_cli(self):
        self.assertEqual(cli_builds("--published-builds"),
                         [("build", "-"), ("build-round", "sdkconfig.round"), ("build-watch", "sdkconfig.watch")])
    def test_known_builds_cli_lists_all_four(self):
        self.assertEqual(cli_builds("--known-builds"),
                         [("build", "-"), ("build-round", "sdkconfig.round"), ("build-watch", "sdkconfig.watch"),
                          ("build-lcd40", "sdkconfig.lcd40")])
    def test_manifests_today(self):
        self.assertEqual(manifest_names(pt_boards.PUBLISHED),
                         {"latest-amoled18.json", "latest-round175c.json", "latest-watch206.json"})

class FlagOn(unittest.TestCase):
    """publishing the FNK0104S is one flag: with it on, every list grows by that board"""
    def test_published_builds_become_four(self):
        self.assertEqual(pt_boards.build_lines(pt_boards.published_rows(fnk_published())),
                         [("build", "-"), ("build-round", "sdkconfig.round"), ("build-watch", "sdkconfig.watch"),
                          ("build-lcd40", "sdkconfig.lcd40")])
    def test_published_dirs_become_four(self):
        self.assertEqual([b[3] for b in pt_boards.published_rows(fnk_published())],
                         ["build", "build-round", "build-watch", "build-lcd40"])
    def test_manifests_become_four(self):
        self.assertEqual(manifest_names(b[0] for b in pt_boards.published_rows(fnk_published())),
                         {"latest-amoled18.json", "latest-round175c.json", "latest-watch206.json", "latest-fnk0104s.json"})
    def test_known_builds_match_published_with_flag_on(self):
        self.assertEqual(pt_boards.build_lines(pt_boards.published_rows(fnk_published())), cli_builds("--known-builds"))
    def test_committed_flag_untouched(self):
        fnk_published()
        self.assertEqual(pt_boards.PUBLISHED, ["amoled18", "round175c", "watch206"])
        self.assertFalse(next(b for b in pt_boards.BOARDS if b[0] == "fnk0104s")[4])

if __name__ == "__main__":
    unittest.main()
