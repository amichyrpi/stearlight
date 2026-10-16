"""Independent public Steam client and Frame OS channel selections."""
import json
import os
from pathlib import Path
import re
import shutil

STEAM = Path(os.environ.get('STEARLIGHT_STEAM_ROOT', '/opt/steam'))
CACHE = Path('/home/stearlight/.cache/stearlight/os-update')


def read(path, default=''):
    try:
        return path.read_text().strip()
    except FileNotFoundError:
        return default


def client_channel():
    choice = read(STEAM / 'package/stearlight-channel')
    return choice if choice in ('rel', 'beta') else 'beta' if read(STEAM / 'package/beta') else 'rel'


def os_channel():
    # Migrate the old shared selector without changing the user's choice.
    return read(STEAM / 'package/stearlight-os-channel', client_channel())


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.next')
    temporary.write_text(value + '\n')
    temporary.replace(path)


def select_client(channel):
    if channel not in ('rel', 'beta'):
        raise ValueError('Unsupported Steam client channel')
    previous = client_channel()
    native = read(STEAM / 'package/beta')
    if not (STEAM / 'package/stearlight-os-channel').exists():
        write(STEAM / 'package/stearlight-os-channel', os_channel())
    write(STEAM / 'package/stearlight-channel', channel)
    if channel == 'beta':
        write(STEAM / 'package/beta', 'publicbeta')
    else:
        (STEAM / 'package/beta').unlink(missing_ok=True)
    if previous != channel or (channel == 'beta' and native != 'publicbeta'):
        write(STEAM / 'package/stearlight-channel-update', channel)


def prepare_manifest_alias():
    # Frame's account profile can restore its recovery branch even when the
    # bootstrapper is explicitly using a moving public feed. Its installed
    # version then comes from the obsolete snapshot manifest. Alias that
    # profile's metadata to the selected feed's committed native manifest.
    package = STEAM / 'package'
    choice = read(package / 'stearlight-channel')
    feed = {'rel': 'steamdeck_stable', 'beta': 'publicbeta'}.get(choice)
    legacy = read(package / 'stearlight-frame-beta', read(package / 'beta'))
    if not feed or not re.fullmatch(r'linux_arm64_beta_[a-f0-9]{8,64}', legacy):
        return
    target = package / f'steam_client_{feed}_linuxarm64.manifest'
    if not target.is_file() or not re.search(r'"version"\s+"\d+"', target.read_text()):
        return
    alias = package / f'steam_client_{legacy}_linuxarm64.manifest'
    if alias.is_symlink() and os.readlink(alias) == target.name:
        return
    backup = alias.with_name(alias.name + '.stearlight-snapshot')
    if alias.is_file() and not alias.is_symlink() and not backup.exists():
        shutil.copy2(alias, backup)
    temporary = alias.with_name(alias.name + '.next')
    temporary.unlink(missing_ok=True)
    temporary.symlink_to(target.name)
    temporary.replace(alias)


def prepare_client():
    package = STEAM / 'package'
    legacy = read(package / 'beta')
    if (re.fullmatch(r'linux_arm64_beta_[a-f0-9]{8,64}', legacy)
            and not (package / 'stearlight-frame-beta').exists()):
        write(package / 'stearlight-frame-beta', legacy)
    if read(package / 'stearlight-channel') not in ('rel', 'beta'):
        write(package / 'stearlight-channel', client_channel())
    prepare_manifest_alias()


def status():
    image = {}
    try:
        image = json.loads((CACHE / 'installed.json').read_text())
    except (FileNotFoundError, ValueError):
        pass
    # Report only a successfully activated Frame userspace runtime, never an
    # available/pending download or Android's kernel/firmware as SteamOS.
    version = image.get('version', '')
    build = image.get('buildid', '')
    if not re.fullmatch(r'\d+\.\d+\.\d+', version) or not re.fullmatch(r'\d{8}\.\d+', build):
        version = build = ''
    return {'client_channel': client_channel(), 'os_channel': os_channel(),
            'frame_version': version, 'frame_build': build}


if __name__ == '__main__':
    prepare_client()
