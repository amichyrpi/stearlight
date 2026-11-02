"""fetch() tries each listed mirror in order and never trusts a wrong checksum."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_payload', ROOT / 'build_files/quest/prepare-payload.py')
payload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(payload)

DATA = b'dependency bytes'
GOOD = hashlib.sha256(DATA).hexdigest()


class Response:
    def __init__(self, data):
        self.data = data

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def read(self, size=-1):
        data, self.data = self.data, b''
        return data


class FetchMirrorsTest(unittest.TestCase):
    def fetch(self, sources, sha256=GOOD):
        calls = []

        def urlopen(url, timeout):
            calls.append(url)
            result = sources[url]
            if isinstance(result, Exception):
                raise result
            return Response(result)

        with tempfile.TemporaryDirectory() as folder, mock.patch.object(payload.urllib.request, 'urlopen', urlopen), \
                mock.patch.object(payload.time, 'sleep'):
            try:
                result = payload.fetch({'urls': list(sources), 'sha256': sha256}, Path(folder), attempts=2)
                return calls, result.read_bytes()
            except Exception as error:
                error.calls = calls
                raise

    def test_second_url_succeeds_after_the_first_fails(self):
        calls, data = self.fetch({'https://a.test/x.tgz': TimeoutError('handshake timed out'), 'https://b.test/x.tgz': DATA})
        self.assertEqual(data, DATA)
        self.assertEqual(calls, ['https://a.test/x.tgz'] * 2 + ['https://b.test/x.tgz'])

    def test_every_url_failing_raises(self):
        with self.assertRaisesRegex(RuntimeError, 'Every download source failed') as raised:
            self.fetch({'https://a.test/x.tgz': TimeoutError('slow'), 'https://b.test/x.tgz': OSError('down')})
        self.assertEqual(len(raised.exception.calls), 4)

    def test_checksum_mismatch_raises_without_trying_later_urls(self):
        with self.assertRaisesRegex(RuntimeError, 'Checksum mismatch: https://a.test/x.tgz') as raised:
            self.fetch({'https://a.test/x.tgz': b'tampered', 'https://b.test/x.tgz': DATA})
        self.assertEqual(raised.exception.calls, ['https://a.test/x.tgz'])

    def test_single_url_spec_still_works(self):
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch.object(payload.urllib.request, 'urlopen', lambda url, timeout: Response(DATA)):
            path = payload.fetch({'url': 'https://a.test/x.tgz', 'sha256': GOOD}, Path(folder))
            self.assertEqual(path.read_bytes(), DATA)


if __name__ == '__main__':
    unittest.main()
