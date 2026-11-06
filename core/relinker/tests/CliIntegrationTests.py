import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from ElfFixture import make_elf


RYTY = Path(sys.argv.pop(1)).resolve()
AUTORUN = Path(sys.argv.pop(1)).resolve()


class CliIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ryty-cli-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_ryty(self, nid="test_nid", rpath=None, extra=None):
        source = self.root / "input.elf"
        output = self.root / "output.elf"
        report = self.root / "report.json"
        source.write_bytes(make_elf(nid))
        command = [str(RYTY), "--skip-syscall-check", "--skip-sce-module",
                   "--registry", "--report", str(report)]
        if rpath is not None:
            command += ["--rpath", rpath]
        if extra:
            command += extra
        result = subprocess.run(command + [str(source), str(output)],
                                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        return report, self.root / "output.registry.json"

    def test_rytynet_flag_writes_marker_and_report(self):
        report, _ = self.run_ryty(extra=["--rytynet"])
        parsed = json.loads(report.read_text(encoding="utf-8"))
        self.assertTrue(parsed["options"]["rytyNet"])
        marker = self.root / "output.elf.rytynet"
        self.assertTrue(marker.exists(), "the RytyNet marker must be written next to the output")
        self.assertIn("ryty-net/1", marker.read_text(encoding="utf-8"))
        self.assertTrue(any("experimental" in warning for warning in parsed["warnings"]))

    def test_version_flag_prints_release(self):
        result = subprocess.run([str(RYTY), "--version"], capture_output=True, encoding="utf-8",
                                errors="replace", timeout=10)
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        self.assertEqual(result.stdout.strip(), "ryty 2.0.0")

    def test_report_carries_version_and_digests(self):
        report, _ = self.run_ryty(extra=["--verify"])
        parsed = json.loads(report.read_text(encoding="utf-8"))
        self.assertEqual(parsed["schema"], "ryty-port-report/2")
        self.assertEqual(parsed["version"], "2.0.0")
        self.assertTrue(parsed["verified"])
        self.assertEqual(len(parsed["inputSha256"]), 64)
        self.assertEqual(len(parsed["outputSha256"]), 64)
        self.assertGreaterEqual(parsed["elapsedMs"], 0.0)

    def test_config_file_applies_options(self):
        source = self.root / "input.elf"
        output = self.root / "output.elf"
        report = self.root / "report.json"
        source.write_bytes(make_elf("test_nid"))
        config = self.root / "port.ini"
        config.write_text("# ryty options\n"
                          "report = " + str(report) + "\n"
                          "skip-syscall-check = true\n"
                          "skip-sce-module = true\n"
                          "registry = true\n", encoding="utf-8")
        result = subprocess.run([str(RYTY), "--config", str(config), str(source), str(output)],
                                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        parsed = json.loads(report.read_text(encoding="utf-8"))
        self.assertTrue(parsed["options"]["skipSyscallCheck"])
        self.assertTrue(parsed["options"]["skipSceModule"])
        self.assertTrue((self.root / "output.registry.json").exists())

    def test_config_file_rejects_unknown_key(self):
        source = self.root / "input.elf"
        output = self.root / "output.elf"
        source.write_bytes(make_elf("test_nid"))
        config = self.root / "bad.ini"
        config.write_text("nonsense-key = 1\n", encoding="utf-8")
        result = subprocess.run([str(RYTY), "--config", str(config), str(source), str(output)],
                                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("nonsense-key", result.stderr)

    def test_dry_run_writes_no_executable(self):
        source = self.root / "input.elf"
        output = self.root / "dry.elf"
        report = self.root / "dry-report.json"
        source.write_bytes(make_elf("test_nid"))
        result = subprocess.run([str(RYTY), "--dry-run", "--verify", "--skip-sce-module",
                                 "--skip-syscall-check", "--report", str(report), str(source), str(output)],
                                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        self.assertFalse(output.exists(), "a dry run must not write the ported executable")
        self.assertIn("Dry run complete", result.stdout)
        parsed = json.loads(report.read_text(encoding="utf-8"))
        self.assertTrue(parsed["dryRun"])
        self.assertTrue(parsed["verified"])

    def test_batch_mode_ports_every_elf(self):
        games = self.root / "games"
        games.mkdir()
        (games / "eboot.bin").write_bytes(make_elf("first_nid"))
        (games / "second.elf").write_bytes(make_elf("second_nid"))
        (games / "notes.txt").write_bytes(b"not an elf at all")
        result = subprocess.run([str(RYTY), "--batch", str(games), "--skip-sce-module",
                                 "--skip-syscall-check", "--verify"],
                                capture_output=True, encoding="utf-8", errors="replace", timeout=60)
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        self.assertIn("2 ported, 0 failed", result.stdout)
        self.assertIn("Batch report:", result.stdout)
        self.assertTrue((games / "ported" / "eboot" / "eboot.elf").exists())
        self.assertTrue((games / "ported" / "second" / "second.elf").exists())
        batchReport = json.loads((games / "ported" / "batch-report.json").read_text(encoding="utf-8"))
        self.assertEqual(batchReport["schema"], "ryty-batch-report/1")
        self.assertEqual(batchReport["version"], "2.0.0")
        self.assertEqual(batchReport["summary"], {"total": 2, "ported": 2, "failed": 0})
        perFile = json.loads((games / "ported" / "eboot" / "eboot.elf.report.json").read_text(encoding="utf-8"))
        self.assertTrue(perFile["verified"])

    def test_batch_mode_reports_failures(self):
        games = self.root / "games"
        games.mkdir()
        (games / "good.bin").write_bytes(make_elf("test_nid"))
        # An ELF whose dynamic section is broken must fail the port, not the run.
        broken = bytearray(make_elf("test_nid"))
        broken[0x200] = 0x7F  # clobber DT_STRTAB tag -> malformed dynamic section
        (games / "broken.elf").write_bytes(bytes(broken))
        result = subprocess.run([str(RYTY), "--batch", str(games), "--skip-sce-module",
                                 "--skip-syscall-check"],
                                capture_output=True, encoding="utf-8", errors="replace", timeout=60)
        self.assertEqual(result.returncode, 1, (result.stdout or "") + (result.stderr or ""))
        self.assertIn("1 ported, 1 failed", result.stdout)
        batchReport = json.loads((games / "ported" / "batch-report.json").read_text(encoding="utf-8"))
        self.assertEqual(batchReport["summary"]["ported"], 1)
        self.assertEqual(batchReport["summary"]["failed"], 1)


    def test_without_rytynet_flag_no_marker_is_written(self):
        report, _ = self.run_ryty()
        parsed = json.loads(report.read_text(encoding="utf-8"))
        self.assertFalse(parsed["options"]["rytyNet"])
        self.assertFalse((self.root / "output.elf.rytynet").exists())

    def test_report_round_trips_every_control_character(self):
        rpath = "libs/" + "".join(chr(value) for value in range(1, 32)) + '"\\ café'
        report, _ = self.run_ryty(rpath=rpath)
        self.assertEqual(json.loads(report.read_text(encoding="utf-8"))["options"]["rpath"], rpath)

    def test_registry_round_trips_every_control_character(self):
        nid = "nid_" + "".join(chr(value) for value in range(1, 32)) + '"\\ café'
        _, registry = self.run_ryty(nid=nid)
        entries = json.loads(registry.read_text(encoding="utf-8"))
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["nid"], nid)

    def test_autorun_uses_literal_filename_and_preserves_exit_code(self):
        names = ["with spaces", "literal $RYTY_AUTORUN_NAME",
                 "literal %RYTY_AUTORUN_NAME%"]
        if os.name != "nt":
            names += ['literal $(printf substituted)', 'literal `printf substituted`',
                      'literal "quotes" and \'apostrophes\'']
        for name in names:
            with self.subTest(name=name):
                target = self.root / (name + AUTORUN.suffix)
                shutil.copy2(AUTORUN, target)
                result = subprocess.run([str(AUTORUN), str(target)], input="\n",
                                        capture_output=True, encoding="utf-8", errors="replace", timeout=30,
                                        env={**os.environ, "RYTY_AUTORUN_NAME": "substituted"})
                self.assertEqual(result.returncode, 37, (result.stdout or "") + (result.stderr or ""))

    def test_autorun_reports_launch_failure(self):
        target = self.root / ("invalid" + AUTORUN.suffix)
        target.write_bytes(b"not an executable")
        result = subprocess.run([str(AUTORUN), str(target)], input="\n",
                                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        self.assertEqual(result.returncode, 1, (result.stdout or "") + (result.stderr or ""))
        self.assertIn("Cannot launch executable", result.stderr)


if __name__ == "__main__":
    unittest.main()
