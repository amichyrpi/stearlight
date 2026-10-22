"""Verify exact-byte adaptation, refusal of unknown builds and restoration."""
import contextlib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

source = Path(__file__).resolve().parents[1] / 'linux/prepare-steam-input.py'
spec = importlib.util.spec_from_file_location('steam_input', source)
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class SteamInputTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.binary = Path(self.directory.name) / 'libSDL3.so.0'
        self.original = b'unchanged prefix' + adapter.PROLOGUE + b'unchanged suffix'
        self.modified = b'unchanged prefix' + adapter.RETURN + b'unchanged suffix'
        self.binary.write_bytes(self.original)
        self.binary.chmod(0o640)
        self.build = (hashlib.sha256(self.original).hexdigest(),
                      hashlib.sha256(self.modified).hexdigest(), len(b'unchanged prefix'))
        self.pins = patch.object(adapter, 'BUILDS', (self.build,))
        self.pins.start()
        self.addCleanup(self.pins.stop)

    def test_apply_idempotence_and_restore(self):
        self.assertTrue(adapter.prepare(self.binary))
        self.assertEqual(self.binary.read_bytes(), self.modified)
        self.assertFalse(adapter.prepare(self.binary))
        self.assertTrue(adapter.prepare(self.binary, raw_hid=True))
        self.assertEqual(self.binary.read_bytes(), self.original)
        self.assertFalse(adapter.prepare(self.binary, raw_hid=True))
        if os.name != 'nt':
            self.assertEqual(self.binary.stat().st_mode & 0o777, 0o640)
        self.assertEqual(list(Path(self.directory.name).iterdir()), [self.binary])

    def test_unknown_build_is_untouched(self):
        unknown = self.original + b'new upstream build'
        self.binary.write_bytes(unknown)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertFalse(adapter.prepare(self.binary))
        self.assertEqual(self.binary.read_bytes(), unknown)

    def test_invalid_output_pin_never_replaces_library(self):
        with patch.object(adapter, 'BUILDS', ((self.build[0], '0' * 64, self.build[2]),)):
            with self.assertRaises(ValueError):
                adapter.prepare(self.binary)
        self.assertEqual(self.binary.read_bytes(), self.original)

    def test_selects_matching_build_offset(self):
        other = b'a different prefix' + adapter.PROLOGUE + b'other suffix'
        modified = b'a different prefix' + adapter.RETURN + b'other suffix'
        build = (hashlib.sha256(other).hexdigest(), hashlib.sha256(modified).hexdigest(), len(b'a different prefix'))
        self.binary.write_bytes(other)
        with patch.object(adapter, 'BUILDS', (self.build, build)):
            self.assertTrue(adapter.prepare(self.binary))
            self.assertEqual(self.binary.read_bytes(), modified)
            self.assertFalse(adapter.prepare(self.binary))
            self.assertTrue(adapter.prepare(self.binary, raw_hid=True))
            self.assertEqual(self.binary.read_bytes(), other)

    @unittest.skipUnless(os.environ.get('TEST_STEAM_SDL3'), 'optional licensed runtime fixture')
    def test_verified_runtime_round_trip(self):
        self.pins.stop()
        data = Path(os.environ['TEST_STEAM_SDL3']).read_bytes()
        build = next(item for item in adapter.BUILDS if item[0] == hashlib.sha256(data).hexdigest())
        self.binary.write_bytes(data)
        self.assertTrue(adapter.prepare(self.binary))
        self.assertEqual(hashlib.sha256(self.binary.read_bytes()).hexdigest(), build[1])
        self.assertTrue(adapter.prepare(self.binary, raw_hid=True))
        self.assertEqual(self.binary.read_bytes(), data)


if __name__ == '__main__':
    unittest.main()
