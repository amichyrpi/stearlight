"""Check that leaving a local game does not leave its background children alive."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest


class ReaperTest(unittest.TestCase):
    def test_proton_supervised_child_reuses_pid_and_keeps_exit_status(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "gamescopereaper"
            source = Path(__file__).resolve().parents[1] / "linux/gamescopereaper.c"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)], check=True)
            script = 'import os,sys,json; print(json.dumps([os.getpid(),os.getpgrp(),sys.argv[1]])); sys.exit(7)'
            child = subprocess.Popen([str(executable), "--", sys.executable, "-c", script, "two words"],
                                     env=dict(os.environ, STEARLIGHT_PROTON_DIRECT="1"), stdout=subprocess.PIPE, text=True)
            output, _ = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 7)
            self.assertEqual(json.loads(output), [child.pid, child.pid, "two words"])

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
