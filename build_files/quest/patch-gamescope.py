#!/usr/bin/env python3
"""Refresh wl_shm uploads in the pinned Frame Gamescope runtime.

The stock importer memoizes Vulkan textures for all Wayland buffers. A DMA-BUF
import stays live, but a shared-memory upload is a snapshot. Reusing that
snapshot cycles through old frames when Xwayland reuses its buffers.
See packages/quest-runtime/linux/gamescope-shm-refresh.patch for the equivalent
source change. The two guarded trampolines preserve DMA-BUF memoization and
release the temporary owning reference after every fresh shared-memory upload.
"""
import hashlib
from pathlib import Path
import struct
import sys

STOCK = '45a4057095103258d0e673f1c7297265d323c8d102bddbe4ccca53e792ad2e87'
PATCHED = '079a22db5f67cc095313aaaf7103a33011319dddcac868696787bcaa49aee8a5'
ADDRESS = 0x400000
# AArch64: get_dmabuf(buf); retain the original cache path for DMA-BUFs.
# For wl_shm, skip lookup, then release OwningRc and return the new commit.
TRAMPOLINE = bytes.fromhex(
    'e0031baae12304915ea7fb976000003617f9ff905515f617fc2304919b15f617'
    'e0031baae123049156a7fb97a0000037e0031aaaf34cf597b30200f97715f617'
    'a1e8ff90de16f617')


def patch_image(original):
    digest = hashlib.sha256(original).hexdigest()
    if digest == PATCHED:
        return original
    if digest != STOCK:
        raise ValueError('Unsupported Frame Gamescope; review its wl_shm importer before patching')
    result = adapt_elf(original)
    if hashlib.sha256(result).hexdigest() != PATCHED:
        raise ValueError('Gamescope adapter checksum mismatch')
    return result


def adapt_elf(original):
    data = bytearray(original)
    phoff = struct.unpack_from('<Q', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 54)
    headers = [bytes(data[phoff + i * size:phoff + (i + 1) * size]) for i in range(count)]
    if size != 56 or [struct.unpack_from('<I', h)[0] for h in headers[2:7]] != [4, 1, 1, 1, 1]:
        raise ValueError('Unexpected Gamescope ELF layout')
    offset = (len(data) + 65535) & ~65535
    # Replace the optional NOTE program header with a final executable LOAD.
    # Keep LOAD addresses ordered and preserve TLS, unwind, stack and RELRO.
    header = struct.pack('<IIQQQQQQ', 1, 5, offset, ADDRESS, ADDRESS,
                         len(TRAMPOLINE), len(TRAMPOLINE), 65536)
    headers[2:7] = headers[3:7] + [header]
    for i, value in enumerate(headers):
        data[phoff + i * size:phoff + (i + 1) * size] = value
    for address, target in ((0x185564, ADDRESS), (0x185bb8, ADDRESS + 32)):
        instruction = 0x14000000 | (((target - address) // 4) & 0x3ffffff)
        struct.pack_into('<I', data, address - 0x10000, instruction)
    data.extend(bytes(offset - len(data)))
    data.extend(TRAMPOLINE)
    return bytes(data)


if __name__ == '__main__':
    source, destination = map(Path, sys.argv[1:])
    result = patch_image(source.read_bytes())
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(result)
    destination.chmod(0o755)
