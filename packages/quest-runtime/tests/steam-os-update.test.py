import importlib.util
import json
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

source = Path(__file__).resolve().parents[1] / 'linux/steam-os-update.py'
spec = importlib.util.spec_from_file_location('osupdate', source)
update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(update)


class OSUpdateTest(unittest.TestCase):
    def image(self, build='20261007.6125817', arch='aarch64'):
        return {'image': {'buildid': build, 'arch': arch, 'product': 'steamos', 'variant': 'vr'},
                'update_path': f'vr/{build}/frame.raucb', 'chunks_store_path': 'vr/chunks.castr'}

    def test_selects_newest_compatible_candidate(self):
        feed = {'minor': {'candidates': [self.image('20260922.1'), self.image(), self.image('20261008.1', 'amd64')]}}
        with patch.object(update, 'fetch', return_value=json.dumps(feed).encode()):
            self.assertEqual(update.candidate('beta')['image']['buildid'], '20261007.6125817')

    def test_rejects_image_paths_outside_valve_store(self):
        item = self.image()
        item['update_path'] = 'vr/../../other/image.raucb'
        with patch.object(update, 'fetch', return_value=json.dumps({'minor': {'candidates': [item]}}).encode()):
            with self.assertRaises(ValueError):
                update.candidate('stable')

    def index(self, end=64):
        return (struct.pack('<6Q', 48, 0x96824D9C7B129FF9, 0, 16, 32, 128)
                + struct.pack('<2Q', 96, 0xE75B9E112F17417D)
                + struct.pack('<Q', end) + bytes(32)
                + bytes(32) + struct.pack('<Q', 0x4B4F050E5549ECD1))

    def test_reconstruction_extent_must_match_signed_size(self):
        self.assertEqual(update.index_chunks(self.index(), 64)[bytes(32)], [(0, 64)])
        for size in (32, 128):
            with self.assertRaises(ValueError):
                update.index_chunks(self.index(), size)

    def test_rejects_truncated_or_invalid_chunk_table(self):
        for data in (self.index()[:-1], bytes(104), self.index(0), self.index(256)):
            with self.assertRaises(ValueError):
                update.index_chunks(data, 256)

    def test_native_duplicate_detection_does_not_start_download(self):
        with patch.object(update, 'fetch', side_effect=AssertionError('must not download')):
            self.assertEqual(update.main(['--supports-duplicate-detection']), 0)


if __name__ == '__main__':
    unittest.main()
