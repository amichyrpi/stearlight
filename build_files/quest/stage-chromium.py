#!/usr/bin/env python3
"""Stage a checksum-pinned ARM64 Chromium without Flatpak or a system install."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import urllib.request


def stage(frame, desktop, output, downloads):
    lock = json.loads((Path(__file__).parent / 'dependencies.json').read_text())['chromium']
    output.mkdir(parents=True, exist_ok=True)
    downloads.mkdir(parents=True, exist_ok=True)
    for name, source in lock.items():
        archive = downloads / (source['sha256'][:16] + '-' + source['url'].rsplit('/', 1)[1])
        if not archive.exists():
            with urllib.request.urlopen(source['url'], timeout=60) as response, archive.open('wb') as target:
                shutil.copyfileobj(response, target)
        with archive.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != source['sha256']:
                raise ValueError('Chromium package checksum mismatch: ' + name)
        subprocess.run(['dpkg-deb', '-x', str(archive), str(output)], check=True)
    lib = output / 'usr/lib'
    libraries = {}
    for path in (frame / 'usr/lib').rglob('*.so*'):
        if path.is_file():
            libraries.setdefault(path.name, path)
    for path in lib.rglob('*.so*'):
        if path.is_file():
            libraries[path.name] = path
    # NSS loads its crypto modules with dlopen(), outside the ELF NEEDED list.
    # Falling back to the rootfs's musl modules breaks TLS initialization.
    for name in ('libsoftokn3.so', 'libfreebl3.so', 'libfreeblpriv3.so', 'libnssckbi.so'):
        if name not in libraries:
            raise ValueError('Missing Chromium NSS module: ' + name)
        if libraries[name].resolve() != (lib / name).resolve():
            shutil.copyfile(libraries[name], lib / name)
    pending = []
    for path in lib.rglob('*'):
        if path.is_file():
            with path.open('rb') as stream:
                if stream.read(4) == b'\x7fELF':
                    pending.append(path)
    seen = set()
    rpath = '/opt/stearlight-browser/usr/lib/aarch64-linux-gnu:/opt/stearlight-browser/usr/lib:/opt/stearlight-desktop/usr/lib'
    while pending:
        binary = pending.pop()
        if binary in seen:
            continue
        seen.add(binary)
        text = subprocess.check_output(['readelf', '-d', str(binary)], text=True)
        for name in re.findall(r'\(NEEDED\).*?\[(.*?)\]', text):
            # The existing desktop ABI stays authoritative; don't copy libc.
            if (desktop / 'usr/lib' / name).is_file():
                continue
            target = lib / name
            if not target.is_file():
                if name not in libraries:
                    raise ValueError('Missing Chromium dependency: ' + name)
                shutil.copyfile(libraries[name], target)
            pending.append(target)
        program = subprocess.check_output(['readelf', '-l', str(binary)], text=True)
        subprocess.run(['patchelf', '--force-rpath', '--set-rpath', rpath, str(binary)], check=True)
        if 'Requesting program interpreter' in program:
            subprocess.run(['patchelf', '--set-interpreter',
                            '/opt/stearlight-desktop/usr/lib/ld-linux-aarch64.so.1', str(binary)], check=True)
    print(f'Staged ARM64 Chromium: {len(seen)} ELF files')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('frame', 'desktop', 'output', 'downloads'):
        parser.add_argument('--' + option, type=Path, required=True)
    args = parser.parse_args()
    stage(args.frame, args.desktop, args.output, args.downloads)
