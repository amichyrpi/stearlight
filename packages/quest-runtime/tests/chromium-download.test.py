"""Chromium staging tolerates CDN failures without caching incomplete packages."""
import hashlib
from http.client import IncompleteRead
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

spec = importlib.util.spec_from_file_location('chromium', Path(__file__).resolve().parents[3] /
                                             'build_files/quest/stage-chromium.py')
chromium = importlib.util.module_from_spec(spec)
spec.loader.exec_module(chromium)


class DownloadTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.downloads = Path(temporary.name)
        self.data = b'complete pinned package'
        self.source = {'url': 'https://example.test/chromium.deb',
                       'sha256': hashlib.sha256(self.data).hexdigest()}
        self.archive = self.downloads / (self.source['sha256'][:16] + '-chromium.deb')

    def test_503_retries_then_commits_verified_package(self):
        failure = HTTPError(self.source['url'], 503, 'first byte timeout', {}, None)
        with patch.object(chromium.urllib.request, 'urlopen', side_effect=[failure, io.BytesIO(self.data)]) as request, \
                patch.object(chromium.time, 'sleep') as sleep:
            self.assertEqual(chromium.fetch(self.source, self.downloads), self.archive)
        self.assertEqual(request.call_count, 2)
        sleep.assert_called_once_with(1)
        self.assertEqual(self.archive.read_bytes(), self.data)
        self.assertEqual(list(self.downloads.iterdir()), [self.archive])

    def test_interrupted_body_restarts_from_empty_file(self):
        class Interrupted(io.BytesIO):
            def read(self, size=-1):
                if self.tell():
                    raise IncompleteRead(b'', 10)
                return super().read(4)
        with patch.object(chromium.urllib.request, 'urlopen',
                          side_effect=[Interrupted(self.data), io.BytesIO(self.data)]), \
                patch.object(chromium.time, 'sleep'):
            chromium.fetch(self.source, self.downloads)
        self.assertEqual(self.archive.read_bytes(), self.data)
        self.assertEqual(list(self.downloads.iterdir()), [self.archive])

    def test_verified_cache_needs_no_network(self):
        self.archive.write_bytes(self.data)
        with patch.object(chromium.urllib.request, 'urlopen') as request:
            chromium.fetch(self.source, self.downloads)
        request.assert_not_called()

    def test_corrupt_old_cache_is_replaced(self):
        self.archive.write_bytes(b'old partial download')
        with patch.object(chromium.urllib.request, 'urlopen', return_value=io.BytesIO(self.data)):
            chromium.fetch(self.source, self.downloads)
        self.assertEqual(self.archive.read_bytes(), self.data)

    def test_checksum_mismatch_is_never_committed(self):
        with patch.object(chromium.urllib.request, 'urlopen', return_value=io.BytesIO(b'wrong bytes')):
            with self.assertRaises(ValueError):
                chromium.fetch(self.source, self.downloads)
        self.assertEqual(list(self.downloads.iterdir()), [])

    def test_errors_are_bounded_and_leave_no_partial_files(self):
        for status, attempts in ((503, 5), (404, 1)):
            with self.subTest(status=status), \
                    patch.object(chromium.urllib.request, 'urlopen',
                                 side_effect=HTTPError(self.source['url'], status, 'failure', {}, None)) as request, \
                    patch.object(chromium.time, 'sleep'):
                with self.assertRaises(HTTPError):
                    chromium.fetch(self.source, self.downloads)
                self.assertEqual(request.call_count, attempts)
                self.assertEqual(list(self.downloads.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
