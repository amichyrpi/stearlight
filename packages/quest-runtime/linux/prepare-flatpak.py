#!/usr/bin/python3
"""Set up verified user installs and the Quest desktop launch adapter."""
import base64
import configparser
import hashlib
from pathlib import Path
import subprocess

PREFIX = Path('/opt/stearlight-desktop/usr/bin')
HOME = Path('/home/stearlight')


def prepare():
    executable = PREFIX / 'flatpak'
    native = PREFIX / 'flatpak.native'
    if executable.read_bytes()[:4] == b'\x7fELF':
        executable.replace(native)
    if not native.is_file():
        raise RuntimeError('The native Flatpak installer is missing')
    executable.write_text('#!/bin/sh\nexec /usr/bin/python3 /opt/quest/flatpak-launcher.py "$@"\n')
    executable.chmod(0o755)
    alias = Path('/usr/bin/flatpak')
    if not alias.exists():
        alias.symlink_to(executable)
    # Discover enumerates the default system installation before the user one.
    # Keep a valid empty repository; all app remotes and installs remain per-user.
    repository = Path('/var/lib/flatpak/repo')
    if not (repository / 'config').is_file():
        repository.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(PREFIX / 'ostree'), '--repo=' + str(repository),
                        'init', '--mode=bare-user-only'], check=True)
    descriptor = Path('/opt/quest/flathub.flatpakrepo')
    fingerprint = hashlib.sha256(descriptor.read_bytes()).hexdigest()
    state = HOME / '.local/state/stearlight-desktop/flathub'
    marker = state / 'descriptor.sha256'
    if marker.exists() and marker.read_text().strip() == fingerprint and (HOME / '.local/share/flatpak/repo/config').is_file():
        return
    state.mkdir(parents=True, exist_ok=True)
    metadata = configparser.ConfigParser(interpolation=None)
    metadata.read(descriptor)
    key = state / 'flathub.gpg'
    key.write_bytes(base64.b64decode(metadata['Flatpak Repo']['GPGKey'], validate=True))
    subprocess.run([str(native), '--user', 'remote-add', '--if-not-exists', 'flathub', str(descriptor)], check=True)
    subprocess.run([str(native), '--user', 'remote-modify', '--gpg-import=' + str(key), 'flathub'], check=True)
    marker.write_text(fingerprint + '\n')


if __name__ == '__main__':
    prepare()
