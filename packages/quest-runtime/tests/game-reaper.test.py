"""Check that leaving a local game does not leave its background children alive."""
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


class ReaperTest(unittest.TestCase):
    def test_exit_status_and_orphan_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            executable = folder / "gamescopereaper"
            source = Path(__file__).resolve().parents[1] / "linux/gamescopereaper.c"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)], check=True)
            pidfile = folder / "child.pid"
            script = folder / "game.sh"
            script.write_text(f'#!/bin/sh\nsleep 120 &\necho $! > "{pidfile}"\nsleep .1\nexit 7\n')
            result = subprocess.run([str(executable), "--", "/bin/sh", str(script)], timeout=8)
            self.assertEqual(result.returncode, 7)
            child = Path("/proc") / pidfile.read_text().strip()
            for _ in range(30):
                if not child.exists():
                    break
                time.sleep(.05)
            self.assertFalse(child.exists(), "Game background process survived cleanup")


if __name__ == "__main__":
    unittest.main()
