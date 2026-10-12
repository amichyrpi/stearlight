import json
from pathlib import Path
import subprocess
import tempfile
import unittest


class PlaySpaceTest(unittest.TestCase):
    def test_openxr_stage_becomes_valid_seated_and_floor_origins(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "play-space"
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(root / "app/src/main/cpp"),
                            str(root / "tests/play-space.test.cpp"), "-o", str(binary)], check=True)
            results = [json.loads(line) for line in subprocess.check_output([str(binary)], text=True).splitlines()]
        self.assertEqual(results[0]["jsonid"], "chaperone_info")
        calibrated = results[0]["universes"][0]
        self.assertEqual(calibrated["universeID"], 2)
        self.assertEqual(calibrated["standing"], {"translation": [0, 0, 0], "yaw": 0})
        seated = calibrated["seated"]
        self.assertEqual(seated["translation"][::2], [-1, 2])
        self.assertAlmostEqual(seated["translation"][1], -1.6)
        self.assertAlmostEqual(seated["yaw"], 1.57079632679, places=6)
        self.assertEqual(calibrated["play_area"], [2, 3])
        # SteamVR negates the file offset when constructing seated-to-raw.
        # The sampled headset must therefore start at the seated origin.
        for raw, offset in zip([1, 1.6, -2], seated["translation"]):
            self.assertAlmostEqual(raw + offset, 0)
        # OpenXR does not expose Guardian's wall polygons through STAGE.
        self.assertEqual(calibrated["collision_bounds"], [])
        self.assertNotIn("play_area", results[1]["universes"][0])


if __name__ == "__main__":
    unittest.main()
