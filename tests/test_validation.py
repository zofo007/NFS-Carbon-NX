import struct
import unittest

from scripts.check_game import check_archive_index, xex_identity
from scripts.validate_build import validate_elf, validate_nro


class ValidationTests(unittest.TestCase):
    def test_editions_reject_each_others_executable(self):
        for edition, media, version, entry in (
                ("standard", 0x51D2B53F, 1, 0x829446E0),
                ("collectors", 0x1F8BEB88, 7, 0x82944510)):
            data = bytearray(128)
            data[:4] = b"XEX2"
            struct.pack_into(">I", data, 20, 3)
            for i, pair in enumerate(((0x40006, 64), (0x10201, 0x82000000), (0x10100, entry))):
                struct.pack_into(">II", data, 24 + i * 8, *pair)
            struct.pack_into(">IIII", data, 64, media, version, 0, 0x454107EC)
            self.assertEqual(xex_identity(data, edition)["media_id"], f"{media:08X}")
            other = "collectors" if edition == "standard" else "standard"
            with self.assertRaisesRegex(ValueError, "edition"):
                xex_identity(data, other)
        with self.assertRaises(ValueError):
            xex_identity(data, "unknown")

    def test_archive_bounds_and_empty_terminal_archive(self):
        index = struct.pack("<6I", 123, 10, 0, 0, 0, 0)
        self.assertEqual(check_archive_index(index, {10: 0}), 1)
        index = struct.pack("<6I", 123, 0, 1, 1, 17, 0)
        self.assertEqual(check_archive_index(index, {0: 2065}), 1)
        for data, sizes in ((index, {0: 2064}), (index, {}), (index[:-1], {0: 2065})):
            with self.assertRaises(ValueError):
                check_archive_index(data, sizes)

    def test_xex_identity_and_wrong_game(self):
        data = bytearray(128)
        data[:4] = b"XEX2"
        struct.pack_into(">I", data, 20, 3)
        for i, pair in enumerate(((0x40006, 64), (0x10201, 0x82000000), (0x10100, 0x829446E0))):
            struct.pack_into(">II", data, 24 + i * 8, *pair)
        struct.pack_into(">IIII", data, 64, 0x51D2B53F, 1, 0, 0x454107EC)
        self.assertEqual(xex_identity(data)["title_id"], "454107EC")
        struct.pack_into(">I", data, 76, 0x454107D9)
        with self.assertRaisesRegex(ValueError, "Expected Xbox 360 Carbon"):
            xex_identity(data)

    def test_xex_truncation_and_optional_offsets(self):
        for data in (b"XBEH", b"XEX2" + bytes(16) + struct.pack(">I", 4097)):
            with self.assertRaises(ValueError):
                xex_identity(data)
        data = bytearray(32)
        data[:4] = b"XEX2"
        struct.pack_into(">III", data, 20, 1, 0x40006, 0xFFFFFFFF)
        with self.assertRaisesRegex(ValueError, "execution identity"):
            xex_identity(data)

    def test_nro_segments_and_elf_architecture(self):
        data = bytearray(0x3000)
        data[0x10:0x14] = b"NRO0"
        struct.pack_into("<I", data, 0x18, len(data))
        for i in range(3):
            struct.pack_into("<II", data, 0x20 + i * 8, i * 0x1000, 0x1000)
        self.assertEqual(validate_nro(data), len(data))
        struct.pack_into("<I", data, 0x28, 0)
        with self.assertRaisesRegex(ValueError, "overlapping"):
            validate_nro(data)
        elf = self.valid_elf()
        validate_elf(elf)
        struct.pack_into("<H", elf, 18, 62)
        with self.assertRaisesRegex(ValueError, "ARM64"):
            validate_elf(elf)

    @staticmethod
    def valid_elf():
        elf = bytearray(0x2000)
        elf[:7] = b"\x7fELF\x02\x01\x01"
        struct.pack_into("<HHIQQ", elf, 16, 3, 183, 1, 0, 64)
        struct.pack_into("<HHH", elf, 52, 64, 56, 1)
        struct.pack_into("<II6Q", elf, 64, 1, 5, 0x1000, 0, 0, 0x1000, 0x1000, 0x1000)
        return elf

    def test_elf_program_headers_and_entry(self):
        cases = ((32, "<Q", 0xFFFFFFFF), (56, "<H", 0), (64 + 32, "<Q", 0x3000),
                 (64 + 40, "<Q", 1), (64 + 48, "<Q", 3), (64 + 4, "<I", 4), (24, "<Q", 0x2000))
        for offset, layout, value in cases:
            with self.subTest(offset=offset):
                elf = self.valid_elf()
                struct.pack_into(layout, elf, offset, value)
                with self.assertRaises(ValueError):
                    validate_elf(elf)

    def test_empty_and_truncated_artifacts(self):
        for data in (b"", b"NRO0", bytes(127)):
            with self.assertRaises(ValueError):
                validate_nro(data)
        with self.assertRaises(ValueError):
            validate_elf(b"\x7fELF")


if __name__ == "__main__":
    unittest.main()
