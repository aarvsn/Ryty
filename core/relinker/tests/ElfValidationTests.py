from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from ElfFixture import make_elf


RYTY = Path(sys.argv.pop(1)).resolve()


def set_tag(image, tag, value, replacement=None):
    for offset in range(0x200, 0x280, 16):
        current = struct.unpack_from("<q", image, offset)[0]
        if current == tag:
            struct.pack_into("<qQ", image, offset, tag if replacement is None else replacement, value)
            return
    raise ValueError(tag)


def multiple_code_segments():
    image = make_elf("test_nid")
    struct.pack_into("<I", image, 68, 4)
    struct.pack_into("<QQ", image, 96, 0x800, 0x800)
    for index, offset, size in [(2, 0x800, 1), (3, 0x900, 2)]:
        struct.pack_into("<IIQQQQQQ", image, 64 + index * 56, 1, 5, offset,
                         0x400000 + offset, 0x400000 + offset, size, size, 1)
    image[0x900:0x902] = b"\x0f\x05"
    return image


class ElfValidationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ryty-elf-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_image(self, image, skip_syscalls=True):
        source = self.root / "input.elf"
        output = self.root / "output.elf"
        source.write_bytes(image)
        output.write_bytes(b"existing output")
        command = [str(RYTY), "--skip-sce-module"]
        if skip_syscalls:
            command.append("--skip-syscall-check")
        result = subprocess.run(command + [str(source), str(output)],
                                capture_output=True, text=True, timeout=15)
        return result, output

    def assert_rejected(self, image, skip_syscalls=True):
        result, output = self.run_image(image, skip_syscalls)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("FAIL:", result.stderr)
        self.assertNotIn("Sanitizer", result.stderr)
        self.assertEqual(output.read_bytes(), b"existing output")

    def test_valid_stripped_elf_still_converts(self):
        result, output = self.run_image(make_elf("test_nid"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(output.read_bytes()[:4], b"\x7fELF")

    def test_rejects_unsupported_elf_encoding(self):
        for offset, encoding, value in [(4, "B", 1), (5, "B", 2), (6, "B", 0),
                                         (18, "H", 183), (20, "I", 0), (52, "H", 63)]:
            with self.subTest(offset=offset, value=value):
                image = make_elf("test_nid")
                struct.pack_into("<" + encoding, image, offset, value)
                self.assert_rejected(image)

    def test_rejects_invalid_header_tables(self):
        for offset, encoding, value in [(54, "H", 0), (54, "H", 55), (54, "H", 64),
                                         (32, "Q", 0xFF8), (32, "Q", (1 << 64) - 8)]:
            with self.subTest(offset=offset, value=value):
                image = make_elf("test_nid")
                struct.pack_into("<" + encoding, image, offset, value)
                self.assert_rejected(image)

    def test_rejects_invalid_section_header_tables(self):
        for section_offset, entry_size, count, string_index in [
                (0xA00, 63, 1, 0), (0xFF0, 64, 1, 0),
                ((1 << 64) - 8, 64, 1, 0), (0xA00, 64, 1, 1)]:
            with self.subTest(section_offset=section_offset, entry_size=entry_size, string_index=string_index):
                image = make_elf("test_nid")
                struct.pack_into("<Q", image, 40, section_offset)
                struct.pack_into("<HHH", image, 58, entry_size, count, string_index)
                self.assert_rejected(image)

    def test_rejects_invalid_load_segments(self):
        for offset, size, memory_size, address in [
                (0, 0x1000, 0xFFF, 0x400000),
                (0xFF8, 16, 16, 0x400000),
                (0, 0x1000, 0x1000, (1 << 64) - 8)]:
            with self.subTest(offset=offset, address=address):
                image = make_elf("test_nid")
                struct.pack_into("<QQQQQQ", image, 72, offset, address, address, size, memory_size, 1)
                self.assert_rejected(image)

    def test_rejects_invalid_dynamic_segments(self):
        for offset, size in [(0x200, 127), (0x200, 112), (0xFF8, 16), ((1 << 64) - 8, 16)]:
            with self.subTest(offset=offset, size=size):
                image = make_elf("test_nid")
                struct.pack_into("<Q", image, 128, offset)
                struct.pack_into("<Q", image, 152, size)
                self.assert_rejected(image)

    def test_rejects_duplicate_dynamic_segments(self):
        image = make_elf("test_nid")
        image[176:232] = image[120:176]
        self.assert_rejected(image)

    def test_rejects_invalid_dynamic_strings(self):
        for size in [5, 0x1000, (1 << 64) - 1]:
            with self.subTest(size=size):
                image = make_elf("test_nid")
                set_tag(image, 10, size)
                self.assert_rejected(image)

    def test_rejects_relocation_table_partial_entries(self):
        for size in [1, 23, 25]:
            with self.subTest(size=size):
                image = make_elf("test_nid")
                set_tag(image, 8, size)
                self.assert_rejected(image)

    def test_rejects_overflowing_relocation_offset(self):
        image = make_elf("test_nid")
        set_tag(image, 7, (1 << 64) - 16, replacement=0x6100002F)
        self.assert_rejected(image)

    def test_rejects_overflowing_symbol_offset(self):
        image = make_elf("test_nid")
        set_tag(image, 6, (1 << 64) - 25, replacement=0x61000039)
        self.assert_rejected(image)

    def test_rejects_truncated_symbol_entry(self):
        image = make_elf("test_nid")
        set_tag(image, 6, 0x400FE0)
        self.assert_rejected(image)

    def test_scans_every_executable_segment(self):
        result, output = self.run_image(multiple_code_segments(), skip_syscalls=False)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("Forbidden syscall", result.stderr)
        self.assertEqual(output.read_bytes(), b"existing output")

    def test_syscall_check_can_still_be_explicitly_skipped(self):
        result, _ = self.run_image(multiple_code_segments())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
