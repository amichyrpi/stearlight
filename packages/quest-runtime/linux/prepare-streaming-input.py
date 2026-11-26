#!/usr/bin/env python3
"""Adapt the verified Frame player's controller encoding for Touch emulation."""
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import sys

ORIGINAL = '31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153'
PATCHED = 'f5a75fc63a46c2bcedd51ac490d6fcf1675f346871547213ac342d50b846f291'
LENGTHS = ((0x57ffc, 34, 35), (0x58050, 35, 34), (0x58744, 35, 36), (0x587a4, 36, 35))
FRAME_2194 = (
    '0a7ea7cfc3c52ba06debde52483709d2985d885ca45c2bb9054db9f42fca8aad',
    'cf1c2cd1b34279228c973cd097c68e98f2edf642efae228ee1cdcc4d20eec3a8',
    ((0x5ad8c, 34, 35), (0x5ade0, 35, 34), (0x5b4d4, 35, 36), (0x5b534, 36, 35)),
)
FRAME_0511 = (
    '97817b325b7c8e2ded04753bb73f6d295d8c5c77790606db0df910c07cee9095',
    'd64da87e72064815777ff956148ba548e445392d21ac4122b4a4c24b0e6370ae',
    ((0x5afbc, 34, 35), (0x5b010, 35, 34), (0x5b704, 35, 36), (0x5b764, 36, 35)),
)
EMULATION = {'simulated_controller_type': 'oculus_touch', 'simulate_rendermodel': True, 'simulate_hmd': False}
OLD_TYPE, NEW_TYPE = b'frame_controller\0', b'oculus_touch\0\0\0\0\0'


def transform(data, restore=False, lengths=LENGTHS):
    before, after = (NEW_TYPE, OLD_TYPE) if restore else (OLD_TYPE, NEW_TYPE)
    if data.count(before) != 1:
        raise ValueError('Unexpected VRLink controller type table')
    result = bytearray(data.replace(before, after))
    # These std::string constructors use compiled lengths, not strlen. Update
    # them with the source table so "trigger/value" cannot become "trigger/valu".
    for offset, old, new in lengths:
        before, after = (new, old) if restore else (old, new)
        expected = 0xd2800004 | (before << 5)  # mov x4, #length
        if struct.unpack_from('<I', result, offset)[0] != expected:
            raise ValueError('Unexpected VRLink source constructor length')
        struct.pack_into('<I', result, offset, 0xd2800004 | (after << 5))
    for hand in ('left', 'right'):
        paths = {name: f'/user/hand/{hand}/input/{name}'.encode()
                 for name in ('thumbstick/x', 'thumbstick/y', 'trigger/value')}
        offsets = {name: result.index(value + b'\0') for name, value in paths.items()}
        # Frame packs X,Y,trigger,grip; Touch expects trigger,X,Y,grip.
        order = [('thumbstick/x', 'trigger/value'), ('thumbstick/y', 'thumbstick/x'), ('trigger/value', 'thumbstick/y')]
        if restore:
            order = [(after, before) for before, after in order]
        for before, after in order:
            offset = offsets[before]
            if result[offset + len(paths[before]):offset + 40] != bytes(40 - len(paths[before])):
                raise ValueError('Unexpected VRLink analog source table')
            result[offset:offset + 40] = paths[after].ljust(40, b'\0')
    return bytes(result)


def atomic(path, data):
    temporary = path.with_name(path.name + '.quest-touch-next')
    try:
        temporary.write_bytes(data)
        temporary.chmod(stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o600)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def prepare(binary, bindings, restore=False):
    data = binary.read_bytes()
    current = hashlib.sha256(data).hexdigest()
    original, patched, lengths = next(
        (build for build in ((ORIGINAL, PATCHED, LENGTHS), FRAME_2194, FRAME_0511) if current in build[:2]),
        (None, None, ()),
    )
    known = original is not None
    restore = restore or not known
    target = original if restore else patched
    changes = {}
    for path in bindings:
        raw = path.read_bytes()
        binding = json.loads(raw)
        if not isinstance(binding, dict) or binding.get('controller_type') != 'frame_controller':
            raise ValueError('Unexpected VRLink binding controller type')
        options = binding.setdefault('options', {})
        if not isinstance(options, dict):
            raise ValueError('Invalid VRLink binding options')
        marker = path.with_name(path.name + '.quest-touch-options.json')
        if restore:
            if not marker.exists():
                continue
            original_options = json.loads(marker.read_text())
            if not isinstance(original_options, dict):
                raise ValueError('Invalid saved VRLink binding options')
            for key, value in EMULATION.items():
                if options.get(key) != value:
                    continue  # Preserve subsequent user choices.
                if key in original_options:
                    options[key] = original_options[key]
                else:
                    options.pop(key, None)
        else:
            if not marker.exists():
                atomic(marker, json.dumps(options).encode())
            options.update(EMULATION)
        updated = (json.dumps(binding, indent=2) + '\n').encode()
        if json.loads(raw) != binding:
            changes[path] = (raw, updated)
    if known and current != target:
        updated = transform(data, restore, lengths)
        if hashlib.sha256(updated).hexdigest() != target:
            raise ValueError('VRLink Touch adapter checksum mismatch')
        changes[binary] = (data, updated)
    applied = []
    try:
        for path, (previous, updated) in changes.items():
            atomic(path, updated)
            applied.append((path, previous))
    except OSError:
        for path, previous in reversed(applied):
            atomic(path, previous)
        raise
    if not known:
        print('Quest Touch adapter does not match this VRLink build; retaining its original input path.', file=sys.stderr)
    return bool(changes)


if __name__ == '__main__':
    root = Path(os.environ.get('STEARLIGHT_STEAMVR_ROOT', '/opt/steamvr'))
    try:
        prepare(root / 'tools/vrlink/bin/linuxarm64/vrlink.real', [
            root / 'tools/vrlink/resources/vrlink_legacy_bindings_frame_controller.json',
            root / 'drivers/frame_controller/resources/input/vrlink_legacy_bindings_frame_controller.json',
        ], '--restore' in sys.argv or os.environ.get('STEARLIGHT_STREAM_TOUCH') == '0')
    except (OSError, ValueError) as error:
        print(f'Quest Touch streaming adapter unavailable: {error}', file=sys.stderr)
