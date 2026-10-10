import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

source = Path(__file__).resolve().parents[1] / 'linux/activate-os-update.py'
spec = importlib.util.spec_from_file_location('activation', source)
activation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(activation)


class ActivationTest(unittest.TestCase):
    def prepare(self, root):
        cache, runtime, pending, previous = [root / name for name in ('cache', 'runtime', 'pending', 'previous')]
        for path in (cache, runtime, pending):
            path.mkdir()
        (cache / 'pending.json').write_text('{"buildid":"new"}')
        (runtime / 'version').write_text('old')
        (pending / 'version').write_text('new')
        return cache, runtime, pending, previous

    def test_confirmed_update_survives_next_start(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.prepare(Path(directory))
            cache, runtime, pending, previous = paths
            activation.activate(*paths)
            self.assertEqual((runtime / 'version').read_text(), 'new')
            self.assertEqual((previous / 'version').read_text(), 'old')
            (cache / 'activated.json').rename(cache / 'installed.json')
            activation.activate(*paths)
            self.assertEqual((runtime / 'version').read_text(), 'new')

    def test_incomplete_start_restores_old_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.prepare(Path(directory))
            activation.activate(*paths)
            activation.activate(*paths)
            self.assertEqual((paths[1] / 'version').read_text(), 'old')
            self.assertFalse((paths[0] / 'activated.json').exists())

    def test_interruptions_between_directory_moves_are_recoverable(self):
        for interrupted_move in ('runtime', 'pending'):
            with self.subTest(interrupted_move=interrupted_move), tempfile.TemporaryDirectory() as directory:
                paths = self.prepare(Path(directory))
                rename = Path.rename

                def interrupted(path, target):
                    if path.name == interrupted_move:
                        raise OSError('interrupted activation')
                    return rename(path, target)

                with patch.object(Path, 'rename', interrupted), self.assertRaises(OSError):
                    activation.activate(*paths)
                activation.activate(*paths)
                self.assertEqual((paths[1] / 'version').read_text(), 'old')
                self.assertFalse((paths[0] / 'activated.json').exists())


if __name__ == '__main__':
    unittest.main()
