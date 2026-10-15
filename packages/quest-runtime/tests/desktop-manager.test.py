import importlib.util
from pathlib import Path
import tempfile
import unittest

source = Path(__file__).resolve().parents[1] / 'linux/desktop-manager.py'
spec = importlib.util.spec_from_file_location('desktop', source)
desktop = importlib.util.module_from_spec(spec)
spec.loader.exec_module(desktop)


class PasswordTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name)
        desktop.STATE = root / 'state'
        desktop.CONFIG = desktop.STATE / 'settings.json'
        desktop.SHADOW = root / 'shadow'
        desktop.SHADOW.write_text('root:!:20000:0:99999:7:::\n')
        self.manager = desktop.Manager()
        self.manager.reconcile = lambda: None

    def tearDown(self):
        self.directory.cleanup()

    def test_password_change_requires_previous_password(self):
        self.manager.set_password('', 'test-password-first')
        before = desktop.SHADOW.read_text()
        with self.assertRaises(ValueError):
            self.manager.set_password('incorrect-password', 'test-password-next')
        self.assertEqual(desktop.SHADOW.read_text(), before)
        self.manager.set_password('test-password-first', 'test-password-next')
        self.assertNotEqual(desktop.SHADOW.read_text(), before)
        self.assertTrue(desktop.SHADOW.read_text().startswith('root:!:'))

    def test_invalid_password_does_not_enable_access(self):
        for password in ('', 'short', None, 'has\x00nul'):
            with self.assertRaises(ValueError):
                self.manager.set_password('', password)
        self.assertFalse(self.manager.status()['has_password'])
        self.assertFalse(self.manager.status()['ssh_running'])

    def test_status_does_not_disclose_password_hash(self):
        self.manager.set_password('', 'test-password-first')
        self.assertNotIn('password_hash', self.manager.status())
        self.assertEqual(desktop.CONFIG.stat().st_mode & 0o777, 0o600)
        self.assertEqual(desktop.SHADOW.stat().st_mode & 0o777, 0o600)


if __name__ == '__main__':
    unittest.main()
