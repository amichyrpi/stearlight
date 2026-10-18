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

source = Path(__file__).resolve().parents[1] / 'linux/prepare-fex.py'
spec = importlib.util.spec_from_file_location('fex_signal', source)
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class FexSignalTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.binary = Path(self.directory.name) / 'FEX'
        self.original = b'unchanged prefix' + b'original' + b'unchanged suffix'
        self.modified = b'unchanged prefix' + b'modified' + b'unchanged suffix'
        self.binary.write_bytes(self.original)
        self.binary.chmod(0o640)
        self.pins = patch.multiple(adapter, PATCHES=[(len(b'unchanged prefix'), b'original'.hex(), b'modified'.hex())],
                                  ORIGINAL=hashlib.sha256(self.original).hexdigest(),
                                  PATCHED=hashlib.sha256(self.modified).hexdigest())
        self.pins.start()
        self.addCleanup(self.pins.stop)

    def test_apply_idempotence_and_restore(self):
        self.assertTrue(adapter.prepare(self.binary))
        self.assertEqual(self.binary.read_bytes(), self.modified)
        self.assertFalse(adapter.prepare(self.binary))
        self.assertTrue(adapter.prepare(self.binary, restore=True))
        self.assertEqual(self.binary.read_bytes(), self.original)
        self.assertFalse(adapter.prepare(self.binary, restore=True))
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
        with patch.object(adapter, 'PATCHED', '0' * 64):
            with self.assertRaises(ValueError):
                adapter.prepare(self.binary)
        self.assertEqual(self.binary.read_bytes(), self.original)

    @unittest.skipUnless(os.environ.get('TEST_STEAM_FEX'), 'optional licensed runtime fixture')
    def test_verified_runtime_round_trip(self):
        self.pins.stop()
        data = Path(os.environ['TEST_STEAM_FEX']).read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), adapter.ORIGINAL)
        self.binary.write_bytes(data)
        self.assertTrue(adapter.prepare(self.binary))
        self.assertEqual(hashlib.sha256(self.binary.read_bytes()).hexdigest(), adapter.PATCHED)
        self.assertTrue(adapter.prepare(self.binary, restore=True))
        self.assertEqual(self.binary.read_bytes(), data)


if __name__ == '__main__':
    unittest.main()
