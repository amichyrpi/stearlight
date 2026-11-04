#!/usr/bin/env python3
"""Stage a checksum-pinned ARM64 Chromium without Flatpak or a system install."""
import argparse
import hashlib
from http.client import IncompleteRead
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import urllib.error
import urllib.request


def fetch(source, downloads):
    archive = downloads / (source['sha256'][:16] + '-' + source['url'].rsplit('/', 1)[1])
    def verified(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest() == source['sha256']
    if archive.exists():
        if verified(archive):
            return archive
        archive.unlink()
    temporary = archive.with_name(archive.name + '.partial')
    for attempt in range(5):
        try:
            with urllib.request.urlopen(source['url'], timeout=90) as response, temporary.open('wb') as target:
                shutil.copyfileobj(response, target)
            if not verified(temporary):
                raise ValueError('Chromium package checksum mismatch: ' + source['url'])
            temporary.replace(archive)
            return archive
        except (urllib.error.URLError, TimeoutError, ConnectionError, IncompleteRead) as error:
            if isinstance(error, urllib.error.HTTPError) and error.code not in (408, 429, 500, 502, 503, 504):
                raise
            if attempt == 4:
                raise
            print(f'Chromium download interrupted; retry {attempt + 1}/4: {error}', flush=True)
            time.sleep(2 ** attempt)
        finally:
            temporary.unlink(missing_ok=True)


def stage(frame, desktop, output, downloads):
    lock = json.loads((Path(__file__).parent / 'dependencies.json').read_text())['chromium']
    output.mkdir(parents=True, exist_ok=True)
    downloads.mkdir(parents=True, exist_ok=True)
    for source in lock.values():
        archive = fetch(source, downloads)
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
