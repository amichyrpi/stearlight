#!/usr/bin/env python3
"""Adapt signal delivery in the verified Steam FEX build for Quest's kernel."""
import hashlib
import os
from pathlib import Path
import stat
import sys

ORIGINAL = 'f9cbb7a70e804b9930fed02fd9d1349d49d7808faace703063a9b4944bc3568e'
PATCHED = 'b5483e28266c73454b8579de630a6e3458bfde20b36ae5d9601f8180cc010afd'

# FEX-2607-76-g37265b1. File offsets, not virtual addresses.
# HandleGuestSignal: accept SEGV_MAPERR or SEGV_ACCERR only at the exact
# InterruptFaultPage address. Quest's 4.19 kernel reports MAPERR for this
# deliberate PROT_NONE access; FEX otherwise delivers it as a guest crash.
# Load code/address together, compare unsigned (code - 1) <= 1, then check
# the original fault-page address. Other signals and addresses retain their
# existing handling.
# RestoreFrame_{x64,ia32,rt_ia32}: restore InSyscallInfo before both return
# paths, retaining the original saved arguments in the modified-IP branch.
# Equivalent source correction: https://github.com/FEX-Emu/FEX/issues/5901
# Source: FEX/37265b1/Source/Tools/LinuxEmulation/LinuxSyscalls/SignalDelegator.cpp
PATCHES = [
    (0x1fb298, "e92b40b9", "e9ab42a9"),
    (0x1fb2a0, "3f090071", "2b050051"),
    (0x1fb2a4, "e1000054", "7f050071"),
    (0x1fb2a8, "e91b40f9", "c8000054"),
    (0x1fb2b0, "3f0100eb", "5f0100eb"),
    (0x225658, "f40301aa", "4a504679"),
    (0x22565c, "f50300aa", "6ae602f9"),
    (0x225678, "48504679", "f50300aa"),
    (0x225680, "e10314aa", "f40301aa"),
    (0x225684, "68e602f9", "1f2003d5"),
    (0x2258e4, "f40301aa", "4a504679"),
    (0x2258e8, "f50300aa", "6ae602f9"),
    (0x225904, "48504679", "f50300aa"),
    (0x225908, "e10314aa", "f40301aa"),
    (0x22590c, "68e602f9", "1f2003d5"),
    (0x225cc8, "f40301aa", "4a504679"),
    (0x225ccc, "f50300aa", "6ae602f9"),
    (0x225ce8, "48504679", "f50300aa"),
    (0x225cec, "e10314aa", "f40301aa"),
    (0x225cf0, "68e602f9", "1f2003d5"),
]


def prepare(binary, restore=False):
    binary = binary.resolve()
    data = bytearray(binary.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    source, target = (PATCHED, ORIGINAL) if restore else (ORIGINAL, PATCHED)
    if digest == target:
        return False
    if digest != source:
        print('Quest FEX signal adapter does not match this build; leaving it unchanged.', file=sys.stderr)
        return False
    for offset, before, after in PATCHES:
        before, after = bytes.fromhex(before), bytes.fromhex(after)
        if restore:
            before, after = after, before
        if data[offset:offset + len(before)] != before:
            raise ValueError('Unexpected FEX signal instructions')
        data[offset:offset + len(before)] = after
    if hashlib.sha256(data).hexdigest() != target:
        raise ValueError('Quest FEX signal adapter checksum mismatch')
    temporary = binary.with_name(binary.name + '.quest-signal')
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
        prepare(root / 'steamapps/common/FEX-Emu/usr/bin/FEX',
                os.environ.get('STEARLIGHT_FEX_SIGNAL_FIX', '1') == '0')
    except (OSError, ValueError) as error:
        print(f'Quest FEX signal adapter unavailable: {error}', file=sys.stderr)
