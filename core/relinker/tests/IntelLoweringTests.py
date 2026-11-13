"""End-to-end tests for the --to-intel AMD-only instruction lowering.

Builds ELF images with a dedicated executable segment holding an AMD-only
instruction and checks that `ryty --to-intel` lowers it, reports it, and leaves
no AMD-only instruction behind in the finished image. Also checks the Intel
host guidance the CLI prints when the flag is omitted.
"""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest

from ElfFixture import make_elf


RYTY = Path(sys.argv.pop(1)).resolve()

BASE = 0x400000
CODE_OFFSET = 0x900
CODE_SIZE = 0x100
# Byte offset of the AMD-only instruction inside the dedicated code segment.
SITE = 0x40

MONITORX = bytes([0x0F, 0x01, 0xFA])  # in-place lowering
CLZERO = bytes([0x0F, 0x01, 0xFC])    # trampoline lowering
RDPRU = bytes([0x0F, 0x01, 0xFD])     # trampoline lowering


def make_intel_elf(instruction):
    """make_elf() plus a dedicated R+X PT_LOAD holding `instruction` at SITE."""
    image = bytearray(make_elf("intel_nid"))
    # Shrink the primary R+X segment so it stops before the code segment, then
    # reuse a spare program-header slot for the dedicated segment.
    struct.pack_into("<IIQQQQQQ", image, 64, 1, 5, 0, BASE, BASE, CODE_OFFSET, CODE_OFFSET, 0x1000)
    struct.pack_into("<IIQQQQQQ", image, 64 + 2 * 56, 1, 5,
                     CODE_OFFSET, BASE + CODE_OFFSET, BASE + CODE_OFFSET, CODE_SIZE, CODE_SIZE, 0x1000)
    sled = bytearray([0x90]) * CODE_SIZE  # NOP sled: every byte is an instruction boundary
    sled[SITE:SITE + len(instruction)] = instruction
    image[CODE_OFFSET:CODE_OFFSET + CODE_SIZE] = sled
    return image


class IntelLoweringTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ryty-intel-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def port(self, instruction, extra=None, name="out.elf"):
        source = self.root / "input.elf"
        output = self.root / name
        report = self.root / (name + ".report.json")
        source.write_bytes(make_intel_elf(instruction))
        command = [str(RYTY), "--skip-sce-module", "--skip-syscall-check",
                   "--report", str(report)]
        if extra:
            command += extra
        result = subprocess.run(command + [str(source), str(output)],
                                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        return result, output, report

    def test_in_place_instruction_is_lowered_and_reported(self):
        result, output, report = self.port(MONITORX, extra=["--to-intel", "--verbose"])
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        self.assertIn("Intel substitution: MONITORX", result.stdout)
        self.assertIn("Intel verification: no AMD-only instructions remain", result.stdout)
        data = json.loads(report.read_text(encoding="utf-8"))
        self.assertTrue(data["scheme"]["toIntel"])
        self.assertEqual([site["instruction"] for site in data["intelSubstitutions"]], ["MONITORX"])
        self.assertEqual(data["intelSubstitutions"][0]["lowering"], "in-place")
        self.assertEqual(data["intelSubstitutions"][0]["offset"], CODE_OFFSET + SITE)
        self.assertEqual(data["summary"]["inPlaceReplacements"], 1)
        self.assertEqual(data["summary"]["trampolines"], 0)
        # The opcode must be gone from the finished image.
        self.assertNotIn(MONITORX, output.read_bytes())

    def test_trampoline_instruction_is_lowered_and_reported(self):
        for instruction, name in [(CLZERO, "CLZERO"), (RDPRU, "RDPRU")]:
            with self.subTest(instruction=name):
                result, output, report = self.port(instruction, extra=["--to-intel", "--verbose"],
                                                   name="out-" + name + ".elf")
                self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
                self.assertIn("Intel substitution: " + name, result.stdout)
                data = json.loads(report.read_text(encoding="utf-8"))
                self.assertEqual(data["intelSubstitutions"][0]["lowering"], "trampoline")
                self.assertEqual(data["summary"]["trampolines"], 1)
                self.assertEqual(data["summary"]["inPlaceReplacements"], 0)
                self.assertNotIn(instruction, output.read_bytes())

    def test_without_flag_no_lowering_happens(self):
        result, output, report = self.port(MONITORX, extra=[])
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        data = json.loads(report.read_text(encoding="utf-8"))
        self.assertFalse(data["scheme"]["toIntel"])
        self.assertEqual(data["intelSubstitutions"], [])
        self.assertEqual(data["summary"]["inPlaceReplacements"], 0)
        self.assertIn(MONITORX, output.read_bytes())

    def test_scheme_line_mentions_intel_lowering(self):
        result, _, _ = self.port(MONITORX, extra=["--to-intel"])
        scheme_line = next(line for line in result.stdout.splitlines()
                           if line.startswith("Porting scheme:"))
        self.assertIn("Intel lowering (--to-intel)", scheme_line)

    def test_batch_report_records_intel_lowering(self):
        batch = self.root / "batch"
        batch.mkdir()
        (batch / "game.elf").write_bytes(make_intel_elf(MONITORX))
        result = subprocess.run([str(RYTY), "--batch", str(batch), "--to-intel",
                                 "--skip-sce-module", "--skip-syscall-check"],
                                capture_output=True, encoding="utf-8", errors="replace", timeout=60)
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        report = json.loads((batch / "ported" / "batch-report.json").read_text(encoding="utf-8"))
        self.assertTrue(report["scheme"]["toIntel"])
        self.assertTrue(report["options"]["toIntel"])


if __name__ == "__main__":
    unittest.main()
