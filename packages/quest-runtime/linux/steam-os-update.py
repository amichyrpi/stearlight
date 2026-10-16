#!/usr/bin/python3
"""Steam's OS-update command, adapted to the Quest's unprivileged rootfs."""
import configparser
from concurrent.futures import ThreadPoolExecutor, as_completed
import ctypes
import fcntl
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import urllib.request
import urllib.parse

QUEST = Path('/opt/quest')
CACHE = Path('/home/stearlight/.cache/stearlight/os-update')
META = 'https://holo-atomupd.steamos.cloud/meta/holo/steamos/aarch64/vr/'
IMAGES = 'https://holo-images.steamos.cloud/'
CA_HASH = '1d6e246961284ee152cce21e5197a2a0f03dc2fbc0f66a66566850b77714100d'
DEV_CA_HASH = 'f42090ff488c7e39b50ed35841e991fc685b3c911ab66cf0e093c9e0c0648ec2'
HTTP = threading.local()


def atomic_json(path, value):
    temporary = path.with_suffix('.next')
    temporary.write_text(json.dumps(value) + '\n')
    temporary.replace(path)


def fetch(url):
    for attempt in range(4):
        try:
            if url.startswith(IMAGES + 'vr/chunks.castr/'):
                connection = getattr(HTTP, 'connection', None)
                if connection is None:
                    connection = http.client.HTTPSConnection(urllib.parse.urlsplit(IMAGES).hostname, timeout=30)
                    HTTP.connection = connection
                connection.request('GET', url[len(IMAGES) - 1:])
                response = connection.getresponse()
                if response.status != 200:
                    raise OSError('SteamOS chunk server returned HTTP ' + str(response.status))
                return response.read()
            with urllib.request.urlopen(url, timeout=30) as response:
                return response.read()
        except (OSError, http.client.HTTPException):
            connection = getattr(HTTP, 'connection', None)
            if connection is not None:
                connection.close()
                del HTTP.connection
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)


def candidate(channel):
    data = json.loads(fetch(META + channel + '.json'))
    candidates = data.get('minor', {}).get('candidates', [])
    valid = []
    for item in candidates:
        image = item.get('image', {})
        if (image.get('arch'), image.get('product'), image.get('variant')) != ('aarch64', 'steamos', 'vr'):
            continue
        if not re.fullmatch(r'\d{8}\.\d+', image.get('buildid', '')):
            continue
        for key in ('update_path', 'chunks_store_path'):
            value = item.get(key, '')
            if not value.startswith('vr/') or '..' in value.split('/') or not re.fullmatch(r'[a-zA-Z0-9_./-]+', value):
                raise ValueError('Invalid Valve image path')
        valid.append(item)
    if not valid:
        raise ValueError('Valve returned no compatible Frame OS image')
    return max(valid, key=lambda item: tuple(map(int, item['image']['buildid'].split('.'))))


def tool(name, *arguments):
    # Explicit glibc loader keeps these tools separate from Alpine helpers.
    command = ['/lib/ld-linux-aarch64.so.1', '--library-path',
               '/usr/lib/aarch64-linux-gnu', str(QUEST / 'update-tools' / name), *map(str, arguments)]
    subprocess.run(command, check=True, stdout=sys.stderr)


def trusted_certificates(channel):
    # Valve's authenticated 0.4.5 image ships this separate development key
    # and lists beta in BranchesDev. Stable retains production-only trust.
    names = [('steamdeck-images.pem', CA_HASH)]
    if channel == 'beta':
        names.append(('steamos-dev-images.pem', DEV_CA_HASH))
    result = []
    for name, digest in names:
        value = (QUEST / name).read_bytes()
        if hashlib.sha256(value).hexdigest() != digest:
            raise ValueError('Valve signing certificate changed: ' + name)
        result.append(value)
    return b'\n'.join(result)


def verified_bundle(item, channel='stable'):
    bundle = fetch(IMAGES + item['update_path'])
    if len(bundle) < 8:
        raise ValueError('Truncated SteamOS bundle')
    size = struct.unpack('>Q', bundle[-8:])[0]
    if not 0 < size < len(bundle) - 8:
        raise ValueError('Invalid SteamOS signature length')
    ca = CACHE / 'trusted-certificates.pem'
    ca.write_bytes(trusted_certificates(channel))
    payload, signature = CACHE / 'bundle.sqfs', CACHE / 'bundle.sig'
    payload.write_bytes(bundle[:-size-8])
    signature.write_bytes(bundle[-size-8:-8])
    tool('openssl', 'cms', '-verify', '-binary', '-inform', 'DER', '-in', signature,
         '-content', payload, '-CAfile', ca, '-purpose', 'any', '-out', '/dev/null')
    directory = CACHE / 'bundle'
    if directory.exists():
        shutil.rmtree(directory)
    tool('unsquashfs', '-q', '-d', directory, payload)
    manifest = configparser.ConfigParser()
    manifest.read(directory / 'manifest.raucm')
    if manifest['update']['compatible'] != 'steamos-aarch64' or manifest['update']['version'] != item['image']['buildid']:
        raise ValueError('SteamOS manifest does not match the selected update')
    image = manifest['image.rootfs']
    if image['filename'] != 'rootfs.img.caibx' or not re.fullmatch(r'[0-9a-f]{64}', image['sha256']):
        raise ValueError('Invalid signed root image manifest')
    return directory / image['filename'], int(image['size']), image['sha256']


def index_chunks(data, expected_size):
    if len(data) < 104:
        raise ValueError('Truncated casync index')
    header, magic, flags, minimum, average, maximum = struct.unpack_from('<6Q', data)
    table_size, table_magic = struct.unpack_from('<2Q', data, 48)
    if header != 48 or magic != 0x96824D9C7B129FF9 or table_magic != 0xE75B9E112F17417D:
        raise ValueError('Invalid casync index')
    body = data[64:]
    if len(body) % 40 or struct.unpack_from('<Q', body, len(body)-8)[0] != 0x4B4F050E5549ECD1:
        raise ValueError('Invalid casync table')
    groups, offset = {}, 0
    for position in range(0, len(body)-40, 40):
        end = struct.unpack_from('<Q', body, position)[0]
        digest = body[position+8:position+40]
        if not offset < end <= expected_size or end-offset > maximum:
            raise ValueError('Invalid casync chunk extent')
        groups.setdefault(digest, []).append((offset, end-offset))
        offset = end
    if offset != expected_size:
        raise ValueError('Root image size does not match its signed manifest')
    return groups


def sparse_chunks(groups):
    # Casync emits full-size zero chunks for unused Btrfs space. Keep those
    # extents sparse, while the final signed checksum still covers every byte.
    length = max(locations[0][1] for locations in groups.values())
    digest = hashlib.new('sha512_256', bytes(length)).digest()
    zeros = groups.get(digest, [])
    if any(count != length for _, count in zeros):
        raise ValueError('Inconsistent zero chunk size')
    return {key: value for key, value in groups.items() if key != digest}, zeros


def clear_extents(fd, locations):
    if not locations or not os.fstat(fd).st_blocks:
        return
    libc = ctypes.CDLL(None, use_errno=True)
    libc.fallocate.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_longlong, ctypes.c_longlong]
    libc.fallocate.restype = ctypes.c_int
    ranges = []
    for offset, length in sorted(locations):
        if ranges and ranges[-1][0] + ranges[-1][1] == offset:
            ranges[-1][1] += length
        else:
            ranges.append([offset, length])
    for offset, length in ranges:
        if libc.fallocate(fd, 3, offset, length):  # KEEP_SIZE | PUNCH_HOLE
            error = ctypes.get_errno()
            raise OSError(error, 'Cannot keep unused SteamOS image space sparse')


def reconstruct(index, size, expected_hash, store):
    image = CACHE / 'rootfs.img'
    groups, zeros = sparse_chunks(index_chunks(index.read_bytes(), size))
    physical = sum(length for locations in groups.values() for _, length in locations)
    allocated = image.stat().st_blocks * 512 if image.exists() else 0
    needed = physical + 2 * 1024 ** 3 - min(allocated, physical)
    if shutil.disk_usage(CACHE).free < needed:
        raise OSError(f'SteamOS update needs {needed / 1024 ** 3:.1f} GiB of free storage')
    fd = os.open(image, os.O_RDWR | os.O_CREAT, 0o600)
    os.ftruncate(fd, size)
    local = threading.local()

    def worker(entry):
        digest, locations = entry
        offset, length = locations[0]
        # Reuse completed chunks after a network interruption or app restart.
        raw = os.pread(fd, length, offset)
        if hashlib.new('sha512_256', raw).digest() != digest:
            encoded = fetch(IMAGES + store + '/' + digest.hex()[:4] + '/' + digest.hex() + '.cacnk')
            if not hasattr(local, 'zstd'):
                local.zstd = ctypes.CDLL('/usr/lib/aarch64-linux-gnu/libzstd.so.1')
                local.zstd.ZSTD_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
                local.zstd.ZSTD_decompress.restype = ctypes.c_size_t
            output = ctypes.create_string_buffer(length)
            result = local.zstd.ZSTD_decompress(output, length, encoded, len(encoded))
            raw = output.raw
            if result != length or hashlib.new('sha512_256', raw).digest() != digest:
                raise ValueError('SteamOS chunk failed integrity validation')
        for offset, length in locations:
            if len(raw) != length:
                raise ValueError('Inconsistent casync chunk size')
            if os.pwrite(fd, raw, offset) != length:
                raise OSError('Short write while downloading SteamOS')

    try:
        clear_extents(fd, zeros)
        with ThreadPoolExecutor(max_workers=8) as pool:
            work = [pool.submit(worker, entry) for entry in groups.items()]
            for number, future in enumerate(as_completed(work), 1):
                future.result()
                if number % 500 == 0:
                    print(f'SteamOS download {number * 100 // len(groups)}%', file=sys.stderr, flush=True)
        os.fsync(fd)
    finally:
        os.close(fd)
    with image.open('rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != expected_hash:
            raise ValueError('SteamOS root image failed its signed checksum')
    return image


def restore_executables(runtime):
    # btrfs restore without -m avoids restoring Valve's root ownership on
    # Android, but also loses executable modes. Tools live outside bin/ too.
    for path in runtime.rglob('*'):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open('rb') as source:
            prefix = source.read(4)
        if prefix == b'\x7fELF' or prefix.startswith(b'#!') or path.suffix == '.sh':
            path.chmod(0o755)


def stage(item, channel='stable'):
    index, size, digest = verified_bundle(item, channel)
    image = reconstruct(index, size, digest, item['chunks_store_path'])
    extracted = CACHE / 'extracted'
    if extracted.exists():
        shutil.rmtree(extracted)
    extracted.mkdir()
    # Android owns all extracted files. Do not restore Valve's Unix owners.
    tool('btrfs', 'restore', '-S', '--path-regex', '^/(|opt(|/steamvr(|/.*)))$', image, extracted)
    runtime = extracted / 'opt/steamvr'
    restore_executables(runtime)
    for name in ('vrserver', 'vrstartup', 'vrcompositor', 'vrdashboard', 'vrclient.so'):
        binary = runtime / 'bin/linuxarm64' / name
        if not binary.is_file() or binary.read_bytes()[:20][18:20] != b'\xb7\x00':
            raise ValueError('Update does not contain a complete ARM64 SteamVR runtime')
    # Prepare all Quest adapters on the staged copy; a failure leaves the
    # currently running runtime intact and propagates to Steam Settings.
    environment = {**os.environ, 'STEARLIGHT_STEAMVR_ROOT': str(runtime)}
    subprocess.run(['/usr/bin/python3', str(QUEST / 'prepare-os-runtime.py'), str(runtime)], env=environment, check=True)
    for script in ('prepare-vrclient.sh', 'prepare-frame-controls.sh'):
        subprocess.run(['/bin/sh', str(QUEST / script)], env=environment, check=True)
    pending = Path('/opt/steamvr.next')
    if pending.exists():
        shutil.rmtree(pending)
    runtime.rename(pending)
    atomic_json(CACHE / 'pending.json', item['image'])
    image.unlink()
    print('SteamOS runtime update staged; restart Stearlight to activate it.', file=sys.stderr)


def main(arguments):
    if '--supports-duplicate-detection' in arguments:
        return 0
    if any(value not in ('check', '--beta', '-d', '--enable-duplicate-detection') for value in arguments):
        raise ValueError('Usage: steamos-update [check]')
    CACHE.mkdir(parents=True, exist_ok=True)
    with (CACHE / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if (CACHE / 'pending.json').exists():
            return 8 if 'check' in arguments else 0
        choice = Path('/opt/steam/package/stearlight-os-channel')
        if not choice.exists():
            choice = Path('/opt/steam/package/stearlight-channel')
        channel = 'beta' if choice.exists() and choice.read_text().strip() == 'beta' else 'stable'
        item = candidate(channel)
        installed = CACHE / 'installed.json'
        if installed.exists() and json.loads(installed.read_text())['buildid'] == item['image']['buildid']:
            return 7
        atomic_json(CACHE / 'available.json', item)
        if 'check' in arguments:
            print(item['image']['buildid'])
        else:
            stage(item, channel)
        return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print('SteamOS runtime update failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
