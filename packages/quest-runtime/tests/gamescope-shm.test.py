import importlib.util
from pathlib import Path
import struct
import unittest

path = Path(__file__).resolve().parents[3] / 'build_files/quest/patch-gamescope.py'
spec = importlib.util.spec_from_file_location('gamescope_patch', path)
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


class GamescopeShmTest(unittest.TestCase):
    def test_arm_branches_target_importer_and_release_original_owner(self):
        # Absolute symbols must resolve relative to the injected code address.
        # An assembler's literal immediates would silently branch outside the ELF.
        words = struct.unpack('<18I', adapter.TRAMPOLINE)
        targets = {2: 0x2e9d80, 5: 0x185568, 7: 0x185688, 10: 0x2e9d80,
                   13: 0x153400, 15: 0x185618, 17: 0x185bbc}
        for index, target in targets.items():
            self.assertEqual(words[index] & 0x7c000000, 0x14000000)
            pc = adapter.ADDRESS + index * 4
            self.assertEqual(pc + signed(words[index] & 0x3ffffff, 26) * 4, target)
        for index, target in ((3, adapter.ADDRESS + 24), (11, adapter.ADDRESS + 64)):
            pc = adapter.ADDRESS + index * 4
            self.assertEqual(pc + signed((words[index] >> 5) & 0x3fff, 14) * 4, target)
        self.assertFalse(words[3] & (1 << 24))  # SHM skips lookup.
        self.assertTrue(words[11] & (1 << 24))  # DMA-BUF retains memoization.

    def test_new_load_preserves_original_mappings_and_loader_metadata(self):
        data = bytearray(0x303b48)
        struct.pack_into('<Q', data, 32, 64)
        struct.pack_into('<HH', data, 54, 56, 12)
        headers = []
        for i, kind in enumerate((6, 3, 4, 1, 1, 1, 1, 7, 2, 0x6474e550, 0x6474e551, 0x6474e552)):
            address = {3: 0, 4: 0x14c090, 5: 0x318290, 6: 0x330d20}.get(i, 0)
            h = struct.pack('<IIQQQQQQ', kind, 5 if i == 4 else 6,
                            address, address, address, 0x1000, 0x2000, 65536)
            headers.append(h)
            data[64 + i * 56:64 + (i + 1) * 56] = h
        original = bytes(data)
        result = adapter.adapt_elf(original)
        after = [result[64 + i * 56:64 + (i + 1) * 56] for i in range(12)]
        self.assertEqual(after[:2], headers[:2])
        self.assertEqual(after[2:6], headers[3:7])
        self.assertEqual(after[7:], headers[7:])
        load = struct.unpack('<IIQQQQQQ', after[6])
        self.assertEqual(load[:2], (1, 5))
        self.assertEqual(load[3], adapter.ADDRESS)
        self.assertEqual(load[2] % 65536, load[3] % 65536)
        self.assertEqual(result[load[2]:load[2] + load[5]], adapter.TRAMPOLINE)
        self.assertEqual(bytes(data), original)

    def test_unknown_runtime_is_rejected_before_modification(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            adapter.patch_image(b'unknown Valve build')


if __name__ == '__main__':
    unittest.main()
