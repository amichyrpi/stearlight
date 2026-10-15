#!/usr/bin/python3
"""Release idle Frame KIO workers after five seconds on Quest."""
import hashlib
from pathlib import Path
import struct
import sys

# KDE scheduler.cpp keeps *returned, idle* workers for 180 seconds and polls
# every 90 seconds. That pool alone can exceed Android's child-process cap.
# Match the complete original .text, which relocation of ELF RPATHs preserves.
# Active jobs never enter WorkerManager::m_idleWorkers and are unaffected.
# Source: https://invent.kde.org/frameworks/kio/-/blob/v6.8.0/src/core/scheduler.cpp
ORIGINAL_TEXT = 'f7356926df82dd5f54d54b2d1a650e496cde6461c8d6e09d3a339cc28c35ed0d'
PATCHES = (
    # cmp w0,179 -> cmp w0,4: reap at idleTime() >= 5 seconds.
    (0x11532c, 0x7102cc1f, 0x7100101f),
    # Both QTimer::start(90000) sites -> QTimer::start(2500).
    (0x115454, 0x528bf201, 0x52813881),
    (0x11545c, 0x72a00021, 0x72a00001),
    (0x119908, 0x528bf201, 0x52813881),
    (0x119910, 0x72a00021, 0x72a00001),
)


def prepare(path):
    data = bytearray(path.read_bytes())
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected a little-endian ELF64 KIO library')
    table = struct.unpack_from('<Q', data, 40)[0]
    size, count, strings = struct.unpack_from('<HHH', data, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, table + i * size) for i in range(count)]
    names = sections[strings]
    names = data[names[4]:names[4] + names[5]]
    text = next(s for s in sections if names[s[0]:].split(b'\0', 1)[0] == b'.text')
    positions = [(address - text[3] + text[4], before, after) for address, before, after in PATCHES]
    original = bytearray(data)
    # Recognize both original and already-patched instructions before checking
    # the original code hash. An unfamiliar library is never modified.
    for offset, before, after in positions:
        if not text[4] <= offset <= text[4] + text[5] - 4:
            return False
        if struct.unpack_from('<I', data, offset)[0] not in (before, after):
            return False
        struct.pack_into('<I', original, offset, before)
    if hashlib.sha256(original[text[4]:text[4] + text[5]]).hexdigest() != ORIGINAL_TEXT:
        return False
    for offset, before, after in positions:
        struct.pack_into('<I', data, offset, after)
    if data != path.read_bytes():
        temporary = path.with_suffix('.quest-next')
        temporary.write_bytes(data)
        temporary.chmod(path.stat().st_mode & 0o777)
        temporary.replace(path)
    return True


if __name__ == '__main__':
    library = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/opt/stearlight-desktop/usr/lib/libKF6KIOCore.so.6')
    if not prepare(library):
        print('Quest KIO idle-worker adapter: unrecognized library; leaving it unchanged', file=sys.stderr)
