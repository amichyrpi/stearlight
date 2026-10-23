"""Recover damaged Steam code while retaining intentional adapters and user data."""
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import zlib

spec = importlib.util.spec_from_file_location('integrity', Path(__file__).resolve().parents[1] / 'linux/prepare-steam-integrity.py')
integrity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integrity)


class IntegrityTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.package = self.root / 'package'
        self.package.mkdir()
        self.target = self.root / integrity.MEMBER
        self.target.parent.mkdir()
        self.good = b'original Valve UI code'
        self.bad = b'damaged! Valve UI code'
        self.target.write_bytes(self.bad)
        self.target.chmod(0o755)
        self.archive = io.BytesIO()
        with zipfile.ZipFile(self.archive, 'w') as archive:
            archive.writestr(integrity.MEMBER, self.good)
            archive.writestr('steamrtarm64/libSDL3.so.0', b'original SDL, must not replace adapter')
        self.payload = self.archive.getvalue()
        self.name = 'bins_linuxarm64_linuxarm64.zip.' + 'a' * 40
        self.digest = hashlib.sha256(self.payload).hexdigest()
        (self.package / 'steam_client_publicbeta_linuxarm64.installed').write_text(
            f'{integrity.MEMBER},{len(self.good)};123;{zlib.crc32(self.good)}\n')
        self.manifest = self.package / 'steam_client_publicbeta_linuxarm64.manifest'
        self.manifest.write_text('"bins_linuxarm64_linuxarm64" { '
            f'"file" "{self.name}" "size" "{len(self.payload)}" "sha2" "{self.digest}" }}')
        (self.root / 'userdata').mkdir()
        (self.root / 'userdata/save').write_bytes(b'user save')
        (self.target.parent / 'libSDL3.so.0').write_bytes(b'our SDL adapter')

    def test_healthy_library_never_downloads_or_changes_files(self):
        self.target.write_bytes(self.good)
        before = self.target.stat().st_mtime_ns
        with patch.object(integrity.urllib.request, 'urlopen') as network:
            self.assertFalse(integrity.prepare(self.root))
            network.assert_not_called()
        self.assertEqual(self.target.stat().st_mtime_ns, before)

    def test_repair_uses_verified_archive_and_preserves_other_files(self):
        with patch.object(integrity.urllib.request, 'urlopen', return_value=io.BytesIO(self.payload)):
            self.assertTrue(integrity.prepare(self.root))
        self.assertEqual(self.target.read_bytes(), self.good)
        self.assertEqual(self.target.stat().st_mode & 0o777, 0o755)
        self.assertEqual((self.root / 'userdata/save').read_bytes(), b'user save')
        self.assertEqual((self.target.parent / 'libSDL3.so.0').read_bytes(), b'our SDL adapter')
        self.target.write_bytes(self.bad)
        with patch.object(integrity.urllib.request, 'urlopen') as network:
            self.assertTrue(integrity.prepare(self.root))
            network.assert_not_called()  # Repeat repairs work offline from the verified cache.

    def test_failed_download_never_replaces_library_or_leaves_partial_files(self):
        with patch.object(integrity.urllib.request, 'urlopen', return_value=io.BytesIO(b'bad package')):
            with self.assertRaises(ValueError):
                integrity.prepare(self.root)
        self.assertEqual(self.target.read_bytes(), self.bad)
        self.assertFalse(list(self.root.rglob('*.quest-repair')))

    def test_wrong_package_version_is_not_installed(self):
        (self.package / self.name).write_bytes(self.payload)
        record = self.package / 'steam_client_publicbeta_linuxarm64.installed'
        record.write_text(f'{integrity.MEMBER},{len(self.good)};123;1234\n')
        with self.assertRaises(ValueError):
            integrity.prepare(self.root)
        self.assertEqual(self.target.read_bytes(), self.bad)


if __name__ == '__main__':
    unittest.main()
