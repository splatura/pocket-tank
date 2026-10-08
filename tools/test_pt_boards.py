"""tools/test_pt_boards.py - the board lists (2026-10-08): every known board is
recognised, only published ones ship, and the CI helpers agree with the list."""
import os, subprocess, sys, unittest
sys.path.insert(0, os.path.dirname(__file__))
import pt_boards

class Lists(unittest.TestCase):
    def test_known_has_all_four(self):
        self.assertEqual(pt_boards.IDS, ["amoled18", "round175c", "watch206", "fnk0104s"])
    def test_fnk_known_not_published(self):
        self.assertIn("fnk0104s", pt_boards.IDS)
        self.assertNotIn("fnk0104s", pt_boards.PUBLISHED)
    def test_published_is_the_shipped_three(self):
        self.assertEqual(pt_boards.PUBLISHED, ["amoled18", "round175c", "watch206"])
    def test_published_dirs_cli(self):
        out = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "pt_boards.py"), "--published-dirs"],
                             capture_output=True, text=True, check=True).stdout.split()
        self.assertEqual(out, ["build", "build-round", "build-watch"])

if __name__ == "__main__":
    unittest.main()
