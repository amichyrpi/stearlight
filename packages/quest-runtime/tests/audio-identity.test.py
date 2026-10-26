"""The native audio adapter may only acknowledge existing process identities."""
import ctypes
import errno
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class AudioIdentityTest(unittest.TestCase):
    def test_unchanged_ids_succeed_and_changes_are_denied(self):
        source = Path(__file__).resolve().parents[1] / 'linux/audio-identity.c'
        before = (os.getuid(), os.geteuid(), os.getgid(), os.getegid())
        with tempfile.TemporaryDirectory() as folder:
            library = Path(folder) / 'identity.so'
            subprocess.run(['cc', '-shared', '-fPIC', '-nostdlib', str(source), '-o', str(library)], check=True)
            adapter = ctypes.CDLL(str(library), use_errno=True)
            for name, real, effective in [('setresuid', os.getuid(), os.geteuid()),
                                          ('setresgid', os.getgid(), os.getegid())]:
                call = getattr(adapter, name)
                call.argtypes = [ctypes.c_uint] * 3
                self.assertEqual(call(real, effective, effective), 0)
                self.assertEqual(call(-1, -1, -1), 0)
                for arguments in [(real + 1, -1, -1), (-1, effective + 1, -1), (-1, -1, effective + 1)]:
                    self.assertEqual(call(*arguments), -1)
                    self.assertEqual(ctypes.get_errno(), errno.EPERM)
            self.assertEqual((os.getuid(), os.geteuid(), os.getgid(), os.getegid()),
                             before)


if __name__ == '__main__':
    unittest.main()
