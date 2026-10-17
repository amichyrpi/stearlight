"""Download and verify the pinned recovery while decompressing it to a raw disk."""
import bz2
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import urllib.request


def root_partition(path):
    with open(path, "rb") as disk:
        disk.seek(512)
        header = disk.read(92)
        if header[:8] != b"EFI PART":
            raise ValueError("Recovery must be an uncompressed GPT disk")
        entry_lba, count, size = struct.unpack_from("<QII", header, 72)
        if not 1 <= count <= 4096 or not 128 <= size <= 4096:
            raise ValueError("Invalid GPT partition table")
        disk.seek(entry_lba * 512)
        for _ in range(count):
            entry = disk.read(size)
            if len(entry) != size:
                break
            if entry[56:128].decode("utf-16le").rstrip("\0") == "rootfs-A":
                first, last = struct.unpack_from("<QQ", entry, 32)
                offset, length = first * 512, (last - first + 1) * 512
                if last < first or offset + length > os.path.getsize(path):
                    raise ValueError("Recovery partition extends past the disk")
                return offset, length
    raise ValueError("Recovery has no rootfs-A partition")


def fetch(lock_path, destination):
    recovery = json.loads(Path(lock_path).read_text())["recovery"]
    destination = Path(destination)
    temporary = destination.with_suffix(destination.suffix + ".partial")
    digest = hashlib.sha256()
    decoder = bz2.BZ2Decompressor()
    created = False
    try:
        with urllib.request.urlopen(recovery["url"], timeout=120) as response, temporary.open("xb") as out:
            created = True
            while chunk := response.read(1024 * 1024):
                digest.update(chunk)
                # Bound decompressed chunks so a corrupt stream cannot exhaust RAM.
                pending = chunk
                while True:
                    block = decoder.decompress(pending, max_length=4 * 1024 * 1024)
                    # Recovery disks contain large unused zero ranges. Keep
                    # those sparse so hosted runners have room for the build.
                    if block and block.count(0) == len(block):
                        out.seek(len(block), os.SEEK_CUR)
                    else:
                        out.write(block)
                    pending = b""
                    if decoder.eof or decoder.needs_input:
                        break
            out.truncate()
        if digest.hexdigest() != recovery["sha256"] or not decoder.eof or decoder.unused_data:
            raise ValueError("Recovery checksum or compressed stream validation failed")
        root_partition(temporary)
        temporary.replace(destination)
    except BaseException:
        if created:
            temporary.unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    if sys.argv[1] == "partition":
        print(*root_partition(sys.argv[2]))
    else:
        fetch(sys.argv[1], sys.argv[2])
