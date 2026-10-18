"""Exercise the fresh-install archives and continuous release failure paths."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_apk', ROOT / 'post_process/quest/package-apk.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class QuestBuildTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)

    def apk(self, omit=None, corrupt=False):
        overlay = io.BytesIO()
        names = ('opt/quest/start-session.sh opt/quest/runtime-monitor opt/quest/libquest_vulkan.so '
                 'opt/quest/libquest_socket.so opt/quest/libquest_video.so opt/quest/libquest_streaming_drm.so '
                 'opt/quest/libquest_gamepad_aarch64.so opt/quest/libquest_gamepad_x86_64.so '
                 'opt/quest/fna3d/libFNA3D.so.0 opt/quest/prepare-flatpak.py '
                 'opt/quest/prepare-steam-input.py opt/quest/game-launch-wrapper opt/quest/prepare-game-runtime.sh opt/quest/prepare-fex.py '
                 'opt/quest/stearlight-quest/bin/linuxarm64/driver_stearlight-quest.so '
                 'opt/stearlight-desktop/usr/bin/plasmashell opt/stearlight-desktop/usr/bin/dolphin '
                 'opt/stearlight-desktop/usr/bin/konsole opt/stearlight-browser/usr/lib/chromium/chromium').split()
        with tarfile.open(fileobj=overlay, mode='w:gz') as archive:
            for name in names:
                if name == omit:
                    continue
                entry = tarfile.TarInfo(name)
                entry.size = 4
                archive.addfile(entry, io.BytesIO(b'test'))
        path = self.root / 'input.apk'
        with zipfile.ZipFile(path, 'w') as archive:
            for name in ('AndroidManifest.xml', 'classes.dex', 'assets/quest-dependencies.json',
                         'lib/arm64-v8a/libstearlight_quest.so', 'lib/arm64-v8a/libopenxr_loader.so',
                         'lib/arm64-v8a/libproot_loader.so', 'lib/arm64-v8a/libtalloc.so',
                         'lib/arm64-v8a/libandroid-shmem.so'):
                archive.writestr(name, b'test')
            archive.writestr('lib/arm64-v8a/libproot_exec.so', b'PROOT_QUEST_ROBUST')
            archive.writestr('assets/boot/clips.json', json.dumps({name: {'starts_ms': [0], 'duration_ms': 16} for name in ('startup', 'loop')}))
            for name in ('startup', 'loop'):
                archive.writestr(f'assets/boot/{name}-0001.png', b'image')
            archive.writestr('assets/rootfs.list', 'rootfs/part-000.bin\nrootfs/part-001.bin\n')
            archive.writestr('assets/rootfs/part-000.bin', b'first')
            archive.writestr('assets/rootfs/part-001.bin', b'wrong' if corrupt else b'second')
            archive.writestr('assets/rootfs.sha256', hashlib.sha256(b'firstsecond').hexdigest())
            archive.writestr('assets/quest-overlay.bin', overlay.getvalue())
        return path

    def release(self):
        sources = self.root / 'sources.zip'
        with zipfile.ZipFile(sources, 'w') as archive:
            for name in ('build_files/quest/prepare-payload.py', 'packages/quest-runtime/proot/robust.c',
                         'packages/quest-runtime/linux/quest_wsi.cpp', 'upstream/example.tar.gz'):
                archive.writestr(name, b'source')
        output = self.root / 'dist'
        package.package(self.apk(), sources, output, 'a' * 40)
        return output

    def test_complete_apk_and_corrupted_or_missing_runtime(self):
        self.assertEqual(package.verify(self.apk())['rootfs_parts'], 2)
        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
            package.verify(self.apk(corrupt=True))
        with self.assertRaisesRegex(ValueError, 'Incomplete Quest runtime'):
            package.verify(self.apk(omit='opt/stearlight-desktop/usr/bin/plasmashell'))
        with self.assertRaisesRegex(ValueError, 'Incomplete Quest runtime'):
            package.verify(self.apk(omit='opt/quest/prepare-steam-input.py'))

    def test_fresh_base_and_separate_overlay_install(self):
        base, overlay = self.root / 'base', self.root / 'overlay'
        for root in (base, overlay):
            (root / 'opt/quest').mkdir(parents=True)
        (base / 'opt/steam/steamrtarm64').mkdir(parents=True)
        (base / 'opt/steam/steamrtarm64/steam').write_bytes(b'steam')
        (base / 'opt/quest/start-session.sh').write_bytes(b'old')
        (overlay / 'opt/quest/start-session.sh').write_bytes(b'new')
        (overlay / 'opt/quest/start-session.sh').chmod(0o755)
        (base / 'tmp').mkdir()
        (base / 'tmp/private-session').write_bytes(b'private')
        archive = self.root / 'rootfs.tar.gz'
        subprocess.run([sys.executable, str(ROOT / 'build_files/quest/package-rootfs.py'), '--root', str(base),
                        '--overlay', str(overlay), '--output', str(archive)], check=True, stdout=subprocess.DEVNULL)
        with tarfile.open(archive) as stream:
            names = stream.getnames()
            self.assertIn('opt/steam/steamrtarm64/steam', names)
            self.assertNotIn('opt/quest/start-session.sh', names)
            self.assertNotIn('tmp/private-session', names)
            destination = self.root / 'install'
            stream.extractall(destination, filter='data')
        # This is the same base-then-overlay order used on first APK launch.
        import shutil
        shutil.copytree(overlay, destination, dirs_exist_ok=True)
        self.assertEqual((destination / 'opt/quest/start-session.sh').read_bytes(), b'new')
        self.assertTrue((destination / 'opt/quest/start-session.sh').stat().st_mode & 0o111)

    def publish(self, assets, **overrides):
        executable = self.root / 'gh'
        executable.write_text('''#!/usr/bin/env python3
import json,os,sys
args=sys.argv[1:]
with open(os.environ['GH_CALLS'],'a') as out:out.write(json.dumps(args)+'\\n')
if args[:2]==['api','repos/example/stearlight']:print('main')
elif args[0]=='api' and '/heads/' in args[1]:print(os.environ.get('FAKE_HEAD',os.environ['GITHUB_SHA']))
elif args[:2]==['release','view'] and '--json' in args:print('stearlight-quest.apk\\nstearlight-frame-windows.zip')
elif args[:2]==['release','upload'] and os.environ.get('FAIL_UPLOAD'):sys.exit(1)
''')
        executable.chmod(0o755)
        calls = self.root / 'calls.jsonl'
        env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ['PATH'], GH_CALLS=str(calls),
                   GH_REPO='example/stearlight', GITHUB_REF='refs/heads/main', GITHUB_EVENT_NAME='push',
                   GITHUB_SHA='a' * 40, GITHUB_RUN_ID='123')
        env.update(overrides)
        result = subprocess.run(['bash', str(ROOT / 'post_process/quest/publish-continuous.sh'), str(assets)],
                                env=env, capture_output=True, text=True)
        commands = [json.loads(line) for line in calls.read_text().splitlines()] if calls.exists() else []
        return result, commands

    def test_release_upload_precedes_vm_cleanup(self):
        result, calls = self.publish(self.release())
        self.assertEqual(result.returncode, 0, result.stderr)
        upload = next(i for i, call in enumerate(calls) if call[:2] == ['release', 'upload'])
        delete = next(i for i, call in enumerate(calls) if call[:2] == ['release', 'delete-asset'])
        self.assertLess(upload, delete)

    def test_failed_upload_preserves_old_assets(self):
        result, calls = self.publish(self.release(), FAIL_UPLOAD='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(call[:2] == ['release', 'delete-asset'] for call in calls))

    def test_branch_build_cannot_publish(self):
        result, calls = self.publish(self.release(), GITHUB_REF='refs/heads/feature')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(call[0] == 'release' for call in calls))

    def test_superseded_build_does_not_publish(self):
        result, calls = self.publish(self.release(), FAKE_HEAD='b' * 40)
        self.assertEqual(result.returncode, 0)
        self.assertFalse(any(call[0] == 'release' for call in calls))

    def test_checksum_failure_prevents_tag_or_release_changes(self):
        output = self.release()
        (output / 'stearlight-quest.apk').write_bytes(b'corrupted upload')
        result, calls = self.publish(output)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(call[0] == 'release' or '--method' in call for call in calls))


if __name__ == '__main__':
    unittest.main()
