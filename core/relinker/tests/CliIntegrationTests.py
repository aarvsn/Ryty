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

    def run_ryty(self, nid="test_nid", rpath=None):
        source = self.root / "input.elf"
        output = self.root / "output.elf"
        report = self.root / "report.json"
        source.write_bytes(make_elf(nid))
        command = [str(RYTY), "--skip-syscall-check", "--skip-sce-module",
                   "--registry", "--report", str(report)]
        if rpath is not None:
            command += ["--rpath", rpath]
        result = subprocess.run(command + [str(source), str(output)],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return report, self.root / "output.registry.json"

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
                                        capture_output=True, text=True, timeout=30,
                                        env={**os.environ, "RYTY_AUTORUN_NAME": "substituted"})
                self.assertEqual(result.returncode, 37, result.stdout + result.stderr)

    def test_autorun_reports_launch_failure(self):
        target = self.root / ("invalid" + AUTORUN.suffix)
        target.write_bytes(b"not an executable")
        result = subprocess.run([str(AUTORUN), str(target)], input="\n",
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Cannot launch executable", result.stderr)


if __name__ == "__main__":
    unittest.main()
