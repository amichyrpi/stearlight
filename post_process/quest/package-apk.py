#!/usr/bin/env python3
"""Validate a complete standalone APK and assemble the continuous download."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tarfile
import zipfile

REPO = Path(__file__).resolve().parents[2]
LIMIT = 2 * 1024**3  # Keep individual GitHub release assets below 2 GiB.


def sha(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def verify(apk):
    if not 0 < apk.stat().st_size < LIMIT:
        raise ValueError('Standalone APK must be nonempty and smaller than 2 GiB')
    with zipfile.ZipFile(apk) as archive:
        for name in ('AndroidManifest.xml', 'classes.dex', 'assets/boot/clips.json',
                     'assets/quest-dependencies.json', 'lib/arm64-v8a/libstearlight_quest.so',
                     'lib/arm64-v8a/libopenxr_loader.so', 'lib/arm64-v8a/libproot_exec.so',
                     'lib/arm64-v8a/libproot_loader.so', 'lib/arm64-v8a/libtalloc.so',
                     'lib/arm64-v8a/libandroid-shmem.so'):
            if archive.getinfo(name).file_size == 0:
                raise ValueError('Empty APK entry: ' + name)
        if b'PROOT_QUEST_ROBUST' not in archive.read('lib/arm64-v8a/libproot_exec.so'):
            raise ValueError('The APK lacks the maintained standalone PRoot adapter')
        clips = json.loads(archive.read('assets/boot/clips.json'))
        for name in ('startup', 'loop'):
            if not clips[name]['starts_ms'] or clips[name]['duration_ms'] <= 0:
                raise ValueError('Empty boot/loading animation: ' + name)
            for number in range(1, len(clips[name]['starts_ms']) + 1):
                if not archive.getinfo(f'assets/boot/{name}-{number:04d}.png').file_size:
                    raise ValueError('Empty boot/loading animation frame')
        parts = archive.read('assets/rootfs.list').decode('ascii').splitlines()
        if not parts or parts != [f'rootfs/part-{i:03d}.bin' for i in range(len(parts))]:
            raise ValueError('Rootfs part list must be nonempty, contiguous and ordered')
        if set('assets/' + name for name in parts) != {name for name in archive.namelist() if name.startswith('assets/rootfs/')}:
            raise ValueError('APK rootfs parts differ from the installation manifest')
        digest = hashlib.sha256()
        for name in parts:
            if archive.getinfo('assets/' + name).file_size == 0:
                raise ValueError('Empty rootfs part: ' + name)
            with archive.open('assets/' + name) as source:
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    digest.update(block)
        if digest.hexdigest() != archive.read('assets/rootfs.sha256').decode('ascii').strip():
            raise ValueError('Rootfs content SHA-256 mismatch')
        required = {
            'opt/quest/start-session.sh', 'opt/quest/runtime-monitor', 'opt/quest/libquest_vulkan.so',
            'opt/quest/libquest_socket.so', 'opt/quest/libquest_video.so', 'opt/quest/libquest_streaming_drm.so',
            'opt/quest/libquest_gamepad_aarch64.so', 'opt/quest/libquest_gamepad_x86_64.so',
            'opt/quest/fna3d/libFNA3D.so.0', 'opt/quest/prepare-flatpak.py',
            'opt/quest/prepare-steam-input.py', 'opt/quest/game-launch-wrapper',
            'opt/quest/prepare-fex.py',
            'opt/quest/prepare-game-runtime.sh',
            'opt/quest/stearlight-quest/bin/linuxarm64/driver_stearlight-quest.so',
            'opt/stearlight-desktop/usr/bin/plasmashell', 'opt/stearlight-desktop/usr/bin/dolphin',
            'opt/stearlight-desktop/usr/bin/konsole', 'opt/stearlight-browser/usr/lib/chromium/chromium',
        }
        with archive.open('assets/quest-overlay.bin') as source, tarfile.open(fileobj=source, mode='r|gz') as overlay:
            for member in overlay:
                name = member.name.removeprefix('./')
                if name in required:
                    if not member.isfile() or not member.size:
                        raise ValueError('Empty or invalid runtime component: ' + name)
                    required.remove(name)
        if required:
            raise ValueError('Incomplete Quest runtime: ' + ', '.join(sorted(required)))
    return {'rootfs_parts': len(parts), 'rootfs_sha256': digest.hexdigest()}


def package(apk, sources, output, commit):
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('A full source commit SHA is required')
    if output.exists() and any(output.iterdir()):
        raise ValueError('Release output must be empty')
    runtime = verify(apk)
    if not 0 < sources.stat().st_size < LIMIT:
        raise ValueError('Dependency source archive must fit in a release asset')
    with zipfile.ZipFile(sources) as bundle:
        for name in ('build_files/quest/prepare-payload.py', 'packages/quest-runtime/proot/robust.c',
                     'packages/quest-runtime/linux/quest_wsi.cpp'):
            if not bundle.read(name):
                raise ValueError('Incomplete dependency source bundle')
        if not any(name.startswith('upstream/') for name in bundle.namelist()):
            raise ValueError('Dependency source bundle has no upstream sources')
    output.mkdir(parents=True, exist_ok=True)
    for source, name in ((apk, 'stearlight-quest.apk'), (sources, 'stearlight-quest-sources.zip')):
        try:
            os.link(source, output / name)
        except OSError:
            shutil.copyfile(source, output / name)
    shutil.copyfile(REPO / 'packages/quest-runtime/README.md', output / 'README.md')
    shutil.copyfile(REPO / 'LICENSE.md', output / 'LICENSE.md')
    metadata = {'commit': commit, 'target': 'Quest 2 ARM64', **runtime}
    (output / 'BUILD.json').write_text(json.dumps(metadata, indent=2) + '\n')
    apk_hash = sha(output / 'stearlight-quest.apk')
    (output / 'stearlight-quest.apk.sha256').write_text(apk_hash + '  stearlight-quest.apk\n')
    files = sorted(output.iterdir())
    (output / 'SHA256SUMS').write_text(''.join(f'{sha(path)}  {path.name}\n' for path in files))
    print(f'Verified standalone APK: {len(files)} download files; commit {commit}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('apk', 'sources', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    package(args.apk, args.sources, args.output, args.commit)
