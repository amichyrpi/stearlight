"""Exercise verified downloads without downloading Valve's multi-GB image."""
import bz2
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

spec = importlib.util.spec_from_file_location("recovery", Path(__file__).parents[1] / "build_files" / "fetch-recovery.py")
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    disk = bytearray(8192)
    disk[512:520] = b"EFI PART"
    struct.pack_into("<QII", disk, 512 + 72, 2, 1, 128)
    struct.pack_into("<QQ", disk, 1024 + 32, 8, 15)
    name = "rootfs-A".encode("utf-16le")
    disk[1024 + 56:1024 + 56 + len(name)] = name
    source = root / "source.bz2"
    source.write_bytes(bz2.compress(disk))
    lock = root / "lock.json"
    metadata = {"recovery": {"url": source.as_uri(), "sha256": hashlib.sha256(source.read_bytes()).hexdigest()}}
    lock.write_text(json.dumps(metadata))
    destination = root / "disk.raw"
    partial = root / "disk.raw.partial"
    recovery.fetch(lock, destination)
    assert destination.read_bytes() == disk
    assert recovery.root_partition(destination) == (4096, 4096)

    metadata["recovery"]["sha256"] = "0" * 64
    lock.write_text(json.dumps(metadata))
    try:
        recovery.fetch(lock, destination)
        raise AssertionError("Bad checksum accepted")
    except ValueError:
        pass
    assert destination.read_bytes() == disk and not partial.exists()

    partial.write_bytes(b"existing unfinished download")
    try:
        recovery.fetch(lock, destination)
        raise AssertionError("Existing partial overwritten")
    except FileExistsError:
        pass
    assert partial.read_bytes() == b"existing unfinished download"

    struct.pack_into("<QQ", disk, 1024 + 32, 8, 100)
    destination.write_bytes(disk)
    try:
        recovery.root_partition(destination)
        raise AssertionError("Out-of-range partition accepted")
    except ValueError:
        pass

    # A zero tail must retain its logical length when written as a sparse file.
    struct.pack_into("<QQ", disk, 1024 + 32, 8, 15)
    disk.extend(bytes(9 * 1024 * 1024))
    source.write_bytes(bz2.compress(disk))
    metadata['recovery']['sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
    lock.write_text(json.dumps(metadata))
    partial.unlink()
    recovery.fetch(lock, destination)
    assert destination.stat().st_size == len(disk)
    assert destination.read_bytes() == disk

print("Recovery checksum, sparse writes, atomic replacement and partition bounds passed.")
