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
    # Client 1792711043 retains the same verified SDL3 detection routine.
    ('a809e5dee7b5382a3c8e1c3122a7c3071cbd52aad369cfe9427586c237f0dc39',
     '17445fc82bc64ccdab2bfb5edf369942b4aeeba934710cad6e73a32df8c2914d', 0xd4df0),
    # Updated Frame SDL3: the same guarded device-change-count call at
    # 0xd4e24 and HID list refresh at 0xd4e50. A controller-rendering replay
    # traced repeated Linux udev setup through this exact detection routine.
    ('cc411c02ad76e07e1b230223c21d470ae597ad099a4d77a5128e2425a9b6343d',
     '66259ccd13fb3501244aeacd7e80eefa466641583f6cb1d9487cf915a54b6204', 0xd4df0),
    # October 29 beta: detection, device-change count and HID list refresh
    # bodies are byte-identical to the verified build immediately above.
    ('79ada316e51ac004ba2c4edce00f0151409a301ceafcd169cc27437dc9d92c43',
     '78a24c2a109bced7c2a979ccd7c99e7f8f373133e076fe4b3c13e51705265298', 0xd4df0),
    # Debian ARM64 SDL 2.26.5, used by Wine's controller service. Verified
    # against build-id 9c22a11ac822b10572c713275311e288d856a346 and SDL source.
    ('72457d21f5a6874e98a89b537ea96a8f7204399a3938dc2a0e41f2a82ed44a59',
     'e960c98217759a07c324fbd5e39116b0566ae238239e19c8f5a2a7ab559730ad', 0x12ed50),
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
    root = Path(os.environ.get('STEARLIGHT_STEAM_ROOT', '/opt/steam'))
    for binary in (root / 'steamrtarm64/libSDL3.so.0',
                   Path('/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0')):
        try:
            prepare(binary, os.environ.get('STEARLIGHT_LINUX_HIDAPI') == '1')
        except (OSError, ValueError) as error:
            print(f'Quest SDL input adapter unavailable for {binary.name}: {error}', file=sys.stderr)
