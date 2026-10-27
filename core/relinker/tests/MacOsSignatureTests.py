"""Validates the ad-hoc code signature the macOS writer embeds.

Ports a synthetic ELF with `ryty --macos`, then parses the output Mach-O's
LC_CODE_SIGNATURE SuperBlob and checks the structure and every 4096-byte page
hash against hashlib's SHA-256. This is the ground truth for the signature the
writer attaches: a wrong page hash is what macOS SIGKILLs a binary for.
"""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest

from ElfFixture import make_elf


RYTY = Path(sys.argv.pop(1)).resolve()

LC_CODE_SIGNATURE = 0x1D
LC_SOURCE_VERSION = 0x2A
CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02
CS_ADHOC = 0x2
CS_HASHTYPE_SHA256 = 2
CS_EXECSEG_MAIN_BINARY = 0x1
PAGE = 4096
SOURCE_VERSION_1_1_0 = 0x0001000001000000


class MacOsSignatureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._temporary = tempfile.TemporaryDirectory(prefix="ryty-macos-")
        source = Path(cls._temporary.name) / "input.elf"
        source.write_bytes(make_elf("test_nid"))
        cls.output = Path(cls._temporary.name) / "output.app"
        result = subprocess.run(
            [str(RYTY), "--skip-sce-module", "--skip-syscall-check", "--macos", str(source), str(cls.output)],
            capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        if result.returncode != 0:
            raise AssertionError((result.stdout or "") + (result.stderr or ""))
        cls.image = cls.output.read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls._temporary.cleanup()

    def load_commands(self):
        magic, _, _, _, ncmds, sizeofcmds, _, _ = struct.unpack_from("<8I", self.image, 0)
        self.assertEqual(magic, 0xFEEDFACF)
        commands = []
        offset = 32
        for _ in range(ncmds):
            cmd, size = struct.unpack_from("<II", self.image, offset)
            commands.append((cmd, offset, size))
            offset += size
        self.assertEqual(offset, 32 + sizeofcmds)
        return commands

    def test_source_version_is_1_1_0(self):
        for cmd, offset, _ in self.load_commands():
            if cmd == LC_SOURCE_VERSION:
                version = struct.unpack_from("<Q", self.image, offset + 8)[0]
                self.assertEqual(version, SOURCE_VERSION_1_1_0)
                return
        self.fail("LC_SOURCE_VERSION missing")

    def test_code_signature_command_present(self):
        commands = self.load_commands()
        signatures = [(o, struct.unpack_from("<II", self.image, o + 8)) for c, o, _ in commands if c == LC_CODE_SIGNATURE]
        self.assertEqual(len(signatures), 1)
        (data_offset, data_size) = signatures[0][1]
        self.assertEqual(data_offset % 16, 0)
        self.assertEqual(data_offset + data_size, len(self.image), "the signature must close the file")

    def code_directory(self):
        commands = self.load_commands()
        for cmd, offset, _ in commands:
            if cmd != LC_CODE_SIGNATURE:
                continue
            data_offset, data_size = struct.unpack_from("<II", self.image, offset + 8)
            magic, length, count = struct.unpack_from(">III", self.image, data_offset)
            self.assertEqual(magic, CSMAGIC_EMBEDDED_SIGNATURE)
            self.assertEqual(length, data_size)
            self.assertEqual(count, 1)
            index_type, index_offset = struct.unpack_from(">II", self.image, data_offset + 12)
            self.assertEqual(index_type, 0)  # CSSLOT_CODEDIRECTORY
            cd = data_offset + index_offset
            fields = struct.unpack_from(">9I", self.image, cd)
            (cd_magic, cd_length, version, flags, hash_offset, ident_offset,
             n_special, n_code, code_limit) = fields
            self.assertEqual(cd_magic, CSMAGIC_CODEDIRECTORY)
            self.assertEqual(version, 0x00020400)
            self.assertEqual(flags, CS_ADHOC)
            hash_size, hash_type, _, page_size = struct.unpack_from(">4B", self.image, cd + 36)
            self.assertEqual((hash_size, hash_type), (32, CS_HASHTYPE_SHA256))
            self.assertEqual(page_size, 12)
            self.assertEqual(code_limit, data_offset, "the hashes must cover everything before the signature")
            code_limit64, exec_base, exec_limit, exec_flags = struct.unpack_from(">4Q", self.image, cd + 56)
            self.assertEqual(code_limit64, data_offset)
            self.assertEqual(exec_flags, CS_EXECSEG_MAIN_BINARY)
            identifier = self.image[cd + ident_offset: cd + hash_offset - 1].decode()
            return cd, hash_offset, n_code, identifier
        self.fail("LC_CODE_SIGNATURE missing")

    def test_identifier_and_page_hashes(self):
        cd, hash_offset, n_code, identifier = self.code_directory()
        self.assertEqual(identifier, "ryty.bundle")
        signature_offset, _ = struct.unpack_from("<II", self.image, [o for c, o, _ in self.load_commands() if c == LC_CODE_SIGNATURE][0] + 8)
        self.assertEqual(n_code, (signature_offset + PAGE - 1) // PAGE)
        for slot in range(n_code):
            start = slot * PAGE
            end = min(start + PAGE, signature_offset)
            expected = hashlib.sha256(self.image[start:end]).digest()
            actual = self.image[cd + hash_offset + slot * 32: cd + hash_offset + slot * 32 + 32]
            self.assertEqual(expected, actual, f"page {slot} hash mismatch")


if __name__ == "__main__":
    unittest.main()
