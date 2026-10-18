"""Regressions for clean, unprivileged Quest builds (GitHub issue #14)."""
import importlib.util
import io
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

REPO = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('build_proot', REPO / 'build_files/quest/build-proot.py')
proot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(proot)


class CleanBuildTest(unittest.TestCase):
    def test_proot_and_loader_come_from_the_same_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'proot.zip'
            with zipfile.ZipFile(archive, 'w') as bundle:
                bundle.writestr('proot-5.1.107.96/src/GNUmakefile', '')
            ndk = root / 'ndk/toolchains/llvm/prebuilt/linux-x86_64/bin'
            ndk.mkdir(parents=True)
            (ndk / 'aarch64-linux-android29-clang').touch()

            def run(command, **kwargs):
                if command[0] == 'make':
                    source = Path(kwargs['cwd'])
                    (source / 'proot').write_bytes(b'source-built-proot')
                    (source / 'loader').mkdir()
                    (source / 'loader/loader').write_bytes(b'source-built-loader')

            with patch.object(proot.patch, 'patch'), patch.object(proot.subprocess, 'run', side_effect=run):
                proot.build(archive, root / 'empty-prefix', root / 'ndk',
                            root / 'jni/libproot_exec.so', root / 'jni/libproot_loader.so')
            self.assertEqual((root / 'jni/libproot_exec.so').read_bytes(), b'source-built-proot')
            self.assertEqual((root / 'jni/libproot_loader.so').read_bytes(), b'source-built-loader')

    @unittest.skipUnless(os.name == 'posix' and shutil.which('zstd'), 'Linux and zstd are required')
    def test_unprivileged_extraction_keeps_hardlinks_and_makes_inputs_readable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            root.chmod(0o777)
            archive = root / 'rootfs.tar'
            outside = root / 'outside'
            outside.write_bytes(b'host file')
            outside.chmod(0o444)
            with tarfile.open(archive, 'w') as output:
                entry = tarfile.TarInfo('bin')
                entry.type, entry.mode = tarfile.DIRTYPE, 0o555
                output.addfile(entry)
                entry = tarfile.TarInfo('bin/bbsuid')
                entry.mode, entry.size = 0o111, 7
                output.addfile(entry, io.BytesIO(b'busybox'))
                entry = tarfile.TarInfo('bin/busybox')
                entry.type, entry.linkname = tarfile.LNKTYPE, 'bin/bbsuid'
                output.addfile(entry)
                entry = tarfile.TarInfo('bin/host-link')
                entry.type, entry.linkname = tarfile.SYMTYPE, str(outside)
                output.addfile(entry)
            subprocess.run(['zstd', '-q', str(archive)], check=True)
            # CI normally runs without root. Also exercise that path when
            # these tests are run by root in a local container.
            def unprivileged():
                os.setgid(65534)
                os.setuid(65534)
            kwargs = {'preexec_fn': unprivileged} if os.geteuid() == 0 else {}
            result = subprocess.run(['bash', str(REPO / 'build_files/quest/extract-rootfs.sh'),
                                     str(archive) + '.zst', str(root / 'extracted')],
                                    capture_output=True, text=True, **kwargs)
            self.assertEqual(result.returncode, 0, result.stderr)
            executable = root / 'extracted/bin/bbsuid'
            self.assertEqual(executable.read_bytes(), b'busybox')
            self.assertEqual(stat.S_IMODE(executable.stat().st_mode) & 0o700, 0o700)
            self.assertEqual(executable.stat().st_ino, (root / 'extracted/bin/busybox').stat().st_ino)
            self.assertTrue((root / 'extracted/bin/host-link').is_symlink())
            self.assertEqual(stat.S_IMODE(outside.stat().st_mode), 0o444)
            # Refusing a second extraction protects files from earlier builds.
            result = subprocess.run(['bash', str(REPO / 'build_files/quest/extract-rootfs.sh'),
                                     str(archive) + '.zst', str(root / 'extracted')],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('must be empty', result.stderr)


if __name__ == '__main__':
    unittest.main()
