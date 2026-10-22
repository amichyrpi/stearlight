#!/usr/bin/env python3
"""Stop the unavailable Linux raw-HID poller in verified Frame SDL builds."""
import hashlib
import os
from pathlib import Path
import stat
import sys

ORIGINAL = 'fd665ec00ecd0f90da0baa9a3dfde52949572cde3d079e52c77cca8794e39ff5'
PATCHED = '1ace18e5dddc797e11e5f155ab6ad42a268de84bfe09673ccdce05b1de85e604'
OFFSET = 0xd4cb8
BUILDS = (
    (ORIGINAL, PATCHED, OFFSET),
    # Client 1792626604: verified HIDAPI_JoystickDetect at 0xd4df0.
    # Its atomic guard calls SDL_hid_device_change_count, compares the saved
    # count, updates the HID device list on change, then releases the guard.
    ('484b0375810a738592c7dbc5f7b6003dd9676ff40708759929e1d5427cb2eed5',
     'e9aa7a344acf940d9396b735762c4fe5146ea1570ec896d6a550c73d8409ded7', 0xd4df0),
)
PROLOGUE = bytes.fromhex('fd7bbea9')
RETURN = bytes.fromhex('c0035fd6')


def prepare(binary, raw_hid=False):
    binary = binary.resolve()
    data = bytearray(binary.read_bytes())
    current = hashlib.sha256(data).hexdigest()
    build = next((item for item in BUILDS if current in item[:2]), None)
    if build is None:
        print('Quest SDL input adapter does not match this build; leaving it unchanged.', file=sys.stderr)
        return False
    original, patched, offset = build
    target = original if raw_hid else patched
    if current == target:
        return False
    before, after = (RETURN, PROLOGUE) if raw_hid else (PROLOGUE, RETURN)
    if data[offset:offset + 4] != before:
        raise ValueError('Unexpected SDL HIDAPI_JoystickDetect prologue')
    # Only HIDAPI_JoystickDetect returns immediately. Its failed Linux backend
    # otherwise retries initialization on every Steam controller poll. Android
    # owns raw USB/Bluetooth HID; OpenXR, SDL virtual joysticks, keyboard/mouse,
    # and existing joystick updates use their separate, unchanged paths.
    data[offset:offset + 4] = after
    if hashlib.sha256(data).hexdigest() != target:
        raise ValueError('Quest SDL input adapter checksum mismatch')
    temporary = binary.with_name(binary.name + '.quest-input')
    try:
        temporary.write_bytes(data)
        temporary.chmod(stat.S_IMODE(binary.stat().st_mode))
        temporary.replace(binary)
    finally:
        temporary.unlink(missing_ok=True)
    return True


if __name__ == '__main__':
    try:
        root = Path(os.environ.get('STEARLIGHT_STEAM_ROOT', '/opt/steam'))
        prepare(root / 'steamrtarm64/libSDL3.so.0', os.environ.get('STEARLIGHT_LINUX_HIDAPI') == '1')
    except (OSError, ValueError) as error:
        print(f'Quest SDL input adapter unavailable: {error}', file=sys.stderr)
