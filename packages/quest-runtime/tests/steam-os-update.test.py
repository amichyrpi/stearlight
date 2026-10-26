import importlib.util
import json
import hashlib
import os
from pathlib import Path
import struct
import unittest
import tempfile
from unittest.mock import patch, MagicMock

source = Path(__file__).resolve().parents[1] / 'linux/steam-os-update.py'
spec = importlib.util.spec_from_file_location('osupdate', source)
update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(update)


class OSUpdateTest(unittest.TestCase):
    def test_restores_tools_and_scripts_without_changing_assets_or_symlink_targets(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            runtime = root / 'runtime'
            tools = runtime / 'tools/vrlink/bin/linuxarm64'
            tools.mkdir(parents=True)
            contents = {'vrlink': b'\x7fELFtest', 'run_vrlink.sh': b'#!/bin/bash\nexit 0\n',
                        'helper.sh': b'exit 0\n', 'background.png': b'PNGdata'}
            for name, data in contents.items():
                path = tools / name
                path.write_bytes(data)
                path.chmod(0o600)
            outside = root / 'outside'
            outside.write_bytes(b'#!/bin/sh\n')
            outside.chmod(0o600)
            (tools / 'external.sh').symlink_to(outside)
            update.restore_executables(runtime)
            for name in ('vrlink', 'run_vrlink.sh', 'helper.sh'):
                self.assertEqual((tools / name).stat().st_mode & 0o777, 0o755)
            self.assertEqual((tools / 'background.png').stat().st_mode & 0o777, 0o600)
            self.assertEqual(outside.stat().st_mode & 0o777, 0o600)

    def test_chunk_downloads_reuse_tls_and_reconnect_after_failure(self):
        first, second = MagicMock(), MagicMock()
        first.getresponse.return_value.status = 200
        first.getresponse.return_value.read.side_effect = [b'one', b'two']
        second.getresponse.return_value.status = 200
        second.getresponse.return_value.read.return_value = b'three'
        update.HTTP = update.threading.local()
        url = update.IMAGES + 'vr/chunks.castr/abcd/chunk.cacnk'
        with patch.object(update.http.client, 'HTTPSConnection', side_effect=[first, second]) as connect, patch.object(update.time, 'sleep'):
            self.assertEqual(update.fetch(url), b'one')
            self.assertEqual(update.fetch(url), b'two')
            self.assertEqual(connect.call_count, 1)
            first.request.side_effect = OSError('connection closed')
            self.assertEqual(update.fetch(url), b'three')
            self.assertEqual(connect.call_count, 2)
            first.close.assert_called_once()
        update.HTTP = update.threading.local()

    def test_only_beta_trusts_the_pinned_development_key(self):
        with patch.object(update, 'QUEST', source.parent):
            stable = update.trusted_certificates('stable')
            beta = update.trusted_certificates('beta')
            self.assertEqual(stable.count(b'BEGIN CERTIFICATE'), 1)
            self.assertEqual(beta.count(b'BEGIN CERTIFICATE'), 2)
            self.assertIn((source.parent / 'steamos-dev-images.pem').read_bytes(), beta)
            with patch.object(update, 'DEV_CA_HASH', '0' * 64):
                with self.assertRaises(ValueError):
                    update.trusted_certificates('beta')

    def test_zero_chunks_are_sparse_and_old_bytes_are_removed_on_resume(self):
        zero = hashlib.new('sha512_256', bytes(4096)).digest()
        groups, holes = update.sparse_chunks({zero: [(0, 4096), (4096, 4096)], b'x': [(8192, 4096)]})
        self.assertEqual(groups, {b'x': [(8192, 4096)]})
        with tempfile.TemporaryFile() as image:
            image.write(b'x' * 12288)
            image.flush()
            update.clear_extents(image.fileno(), holes)
            self.assertEqual(os.pread(image.fileno(), 12288, 0), bytes(8192) + b'x' * 4096)
            self.assertEqual(os.fstat(image.fileno()).st_size, 12288)

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

    def test_verified_completed_download_skips_network_and_rewrite(self):
        with tempfile.TemporaryDirectory() as folder:
            cache = Path(folder)
            image = cache / 'rootfs.img'
            contents = b'completed signed image'
            image.write_bytes(contents)
            (cache / 'index').write_bytes(b'index')
            with patch.object(update, 'CACHE', cache), patch.object(update, 'progress'), \
                    patch.object(update, 'index_chunks', return_value={b'x': [(0, len(contents))]}), \
                    patch.object(update, 'sparse_chunks', side_effect=lambda groups: (groups, [])), \
                    patch.object(update, 'fetch', side_effect=AssertionError('must not download')):
                self.assertEqual(update.reconstruct(cache / 'index', len(contents), hashlib.sha256(contents).hexdigest(), 'vr/chunks.castr'), image)
                self.assertEqual(image.read_bytes(), contents)

    def test_failed_chunk_does_not_queue_the_entire_image(self):
        with tempfile.TemporaryDirectory() as folder:
            cache = Path(folder)
            (cache / 'index').write_bytes(b'index')
            groups = {number.to_bytes(32, 'little'): [(number * 64, 64)] for number in range(1000)}
            with patch.object(update, 'CACHE', cache), patch.object(update, 'progress'), \
                    patch.object(update, 'index_chunks', return_value=groups), \
                    patch.object(update, 'sparse_chunks', side_effect=lambda value: (value, [])), \
                    patch.object(update, 'fetch', side_effect=OSError('connection interrupted')) as fetch:
                with self.assertRaisesRegex(OSError, 'connection interrupted'):
                    update.reconstruct(cache / 'index', 64000, '0' * 64, 'vr/chunks.castr')
                self.assertLessEqual(fetch.call_count, 16)

    def test_native_duplicate_detection_does_not_start_download(self):
        with patch.object(update, 'fetch', side_effect=AssertionError('must not download')):
            self.assertEqual(update.main(['--supports-duplicate-detection']), 0)


if __name__ == '__main__':
    unittest.main()
