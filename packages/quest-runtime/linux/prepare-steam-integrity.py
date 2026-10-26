#!/usr/bin/python3
"""Repair Steam's unmodified UI library from its matching Valve package."""
import hashlib
import os
from pathlib import Path
import re
import sys
import urllib.request
import zipfile
import zlib

MEMBER = 'steamrtarm64/steamui.so'
MAX_PACKAGE = 256 * 1024 * 1024


def checksum(path):
    value = 0
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value = zlib.crc32(block, value)
    return value


def sha256(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def prepare(root):
    package = root / 'package'
    choice = package / 'stearlight-channel'
    feed = 'steamdeck_stable' if choice.exists() and choice.read_text().strip() == 'rel' else 'publicbeta'
    record = package / f'steam_client_{feed}_linuxarm64.installed'
    manifest = package / f'steam_client_{feed}_linuxarm64.manifest'
    if not record.is_file() or not manifest.is_file():
        return False  # First launch of a recovery image has no committed public feed yet.
    row = next((row for row in record.read_text().splitlines() if row.startswith(MEMBER + ',')), '')
    match = re.fullmatch(re.escape(MEMBER) + r',([0-9]+);[0-9]+;([0-9]+)', row)
    if not match:
        return False
    size, expected_crc = map(int, match.groups())
    if not 0 < size <= MAX_PACKAGE or not 0 <= expected_crc <= 0xffffffff:
        raise ValueError('Invalid installed Steam UI record')
    target = root / MEMBER
    try:
        if target.stat().st_size == size and checksum(target) == expected_crc:
            return False
    except FileNotFoundError:
        pass
    # This ELF is never patched by our adapters. Do not validate adapted SDL,
    # browser JavaScript or launchers against Valve's original checksums.
    print('Steam UI integrity mismatch; repairing the matching Valve library.', flush=True)
    section = re.search(r'"bins_linuxarm64_linuxarm64"\s*\{([^}]+)', manifest.read_text())
    fields = dict(re.findall(r'"([^"\n]+)"\s+"([^"\n]+)"', section[1])) if section else {}
    name, digest = fields.get('file', ''), fields.get('sha2', '')
    archive_size = int(fields.get('size', '0'))
    if (not re.fullmatch(r'bins_linuxarm64_linuxarm64\.zip\.[a-f0-9]{40}', name) or
        not re.fullmatch(r'[a-f0-9]{64}', digest) or not 0 < archive_size <= MAX_PACKAGE):
        raise ValueError('No matching verified ARM64 Steam package')
    archive = package / name
    if not archive.is_file() or archive.stat().st_size != archive_size or sha256(archive) != digest:
        temporary = archive.with_name(archive.name + '.quest-repair')
        try:
            with urllib.request.urlopen('https://client-update.akamai.steamstatic.com/' + name, timeout=30) as response, temporary.open('wb') as output:
                total = 0
                while block := response.read(1024 * 1024):
                    total += len(block)
                    if total > archive_size:
                        raise ValueError('Steam repair package exceeds its recorded size')
                    output.write(block)
            if total != archive_size or sha256(temporary) != digest:
                raise ValueError('Steam repair package checksum mismatch')
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    temporary = target.with_name(target.name + '.quest-repair')
    try:
        with zipfile.ZipFile(archive) as source:
            entry = source.getinfo(MEMBER)
            if entry.file_size != size or entry.CRC != expected_crc:
                raise ValueError('Steam repair package and installed version differ')
            # Validate the complete member before replacing the installed file.
            data = source.read(entry)
        with temporary.open('wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        temporary.chmod(target.stat().st_mode & 0o777 if target.exists() else 0o755)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)
    print('Steam UI restored; account, games and user settings preserved.', flush=True)
    return True


if __name__ == '__main__':
    try:
        prepare(Path(os.environ.get('STEARLIGHT_STEAM_ROOT', '/opt/steam')))
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print('Steam UI repair failed; check the connection and retry: ' + str(error), file=sys.stderr)
        sys.exit(1)
