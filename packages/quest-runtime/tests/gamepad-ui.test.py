import importlib.util
from pathlib import Path
import tempfile
import unittest

LINUX = Path(__file__).resolve().parents[1] / "linux"
spec = importlib.util.spec_from_file_location("bridge", LINUX / "prepare-gamepad-ui.py")
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class GamepadUITest(unittest.TestCase):
    def test_existing_os_bridge_refreshes_without_changing_valve_code(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            ui = root / "resources/webinterface/dashboard/systemui.js"
            ui.parent.mkdir(parents=True)
            marker = b"// Platform adapter: Valve owns the dock button and room-view state."
            ui.write_bytes(b"overlay adapter;\n" + marker + b"\n(() => { old();\n})();\nValveDashboard();")
            bridge.refresh(root, LINUX / "roomview-bridge.js")
            data = ui.read_bytes()
            self.assertIn(b"__stearlightGamepadBridge", data)
            self.assertEqual(data.count(marker), 1)
            self.assertTrue(data.endswith(b"\nValveDashboard();"))
            stamp = ui.stat().st_mtime_ns
            bridge.refresh(root, LINUX / "roomview-bridge.js")
            self.assertEqual(ui.stat().st_mtime_ns, stamp)

    def test_unknown_bridge_remains_unchanged(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            ui = root / "resources/webinterface/dashboard/systemui.js"
            ui.parent.mkdir(parents=True)
            ui.write_bytes(b"UnknownDashboard();")
            with self.assertRaises(ValueError):
                bridge.refresh(root, LINUX / "roomview-bridge.js")
            self.assertEqual(ui.read_bytes(), b"UnknownDashboard();")


if __name__ == "__main__":
    unittest.main()
