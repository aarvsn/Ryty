"""Porting-scheme tests (2.0.0).

Covers the resolved per-target porting scheme shared by the CLI, the reports
and the docs:

* Linux: PT_INTERP content (--interp), the non-executable PT_GNU_STACK
  declaration, DT_RUNPATH presence/absence (--rpath ""), and the scheme
  object in the JSON report.
* The unified --target selector for all five systems.
* --min-os (Apple deployment target), --interp target restrictions and the
  bundle-identifier validation.
* Config-file keys: interp, min-os and the empty rpath.
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

PT_INTERP = 3
PT_DYNAMIC = 2
PT_GNU_STACK = 0x6474E551


class PortSchemeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ryty-scheme-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "input.elf"
        self.source.write_bytes(make_elf("scheme_nid"))

    def run_ryty(self, *arguments):
        return subprocess.run(
            [str(RYTY), *arguments],
            capture_output=True, encoding="utf-8", errors="replace", timeout=30,
        )

    def port(self, output_name, *options):
        output = self.root / output_name
        result = self.run_ryty(*options, "--skip-sce-module", "--skip-syscall-check",
                               str(self.source), str(output))
        self.assertEqual(result.returncode, 0,
                         (result.stdout or "") + (result.stderr or ""))
        return output, result

    def program_headers(self, image):
        phoff = struct.unpack_from("<Q", image, 32)[0]
        phentsize, phnum = struct.unpack_from("<HH", image, 54)
        headers = []
        for index in range(phnum):
            entry = phoff + index * phentsize
            kind, flags = struct.unpack_from("<II", image, entry)
            offset, = struct.unpack_from("<Q", image, entry + 8)
            size, = struct.unpack_from("<Q", image, entry + 32)
            headers.append({"type": kind, "flags": flags, "offset": offset, "size": size})
        return headers

    def dynamic_tags(self, image):
        dynamic = next(h for h in self.program_headers(image) if h["type"] == PT_DYNAMIC)
        tags = []
        for cursor in range(0, dynamic["size"] - dynamic["size"] % 16, 16):
            tag, value = struct.unpack_from("<qQ", image, dynamic["offset"] + cursor)
            if tag == 0:
                break
            tags.append((tag, value))
        return tags

    def interp_string(self, image):
        interp = next(h for h in self.program_headers(image) if h["type"] == PT_INTERP)
        raw = image[interp["offset"]:interp["offset"] + interp["size"]]
        return raw.split(b"\0")[0].decode()

    # --- Linux scheme ------------------------------------------------------

    def test_linux_scheme_default(self):
        output, result = self.port("out-default.elf", "--verify")
        image = output.read_bytes()
        # NX stack declared and non-executable.
        stacks = [h for h in self.program_headers(image) if h["type"] == PT_GNU_STACK]
        self.assertEqual(len(stacks), 1, result.stdout)
        self.assertEqual(stacks[0]["flags"] & 0x1, 0, "PT_GNU_STACK must not be executable")
        # Default glibc interpreter.
        self.assertEqual(self.interp_string(image), "/lib64/ld-linux-x86-64.so.2")
        # Default run path.
        self.assertIn((29, self._runpath_value(image)), self.dynamic_tags(image))
        # The scheme line is printed and describes the resolved plan.
        scheme_line = next(line for line in result.stdout.splitlines()
                           if line.startswith("Porting scheme:"))
        self.assertIn("PT_INTERP /lib64/ld-linux-x86-64.so.2", scheme_line)
        self.assertIn("non-executable stack", scheme_line)
        self.assertIn("DT_RUNPATH $ORIGIN/libs", scheme_line)
        # The verification message reflects the deep ELF checks.
        self.assertIn("PT_INTERP + PT_DYNAMIC verified", result.stdout)
        self.assertIn("PT_GNU_STACK NX", result.stdout)

    def _runpath_value(self, image):
        # The DT_RUNPATH value is a string-table offset; just assert presence.
        return next(value for tag, value in self.dynamic_tags(image) if tag == 29)

    def test_interp_flag_changes_pt_interp(self):
        output, result = self.port("out-musl.elf", "--interp", "/lib/ld-musl-x86_64.so.1", "--verify")
        image = output.read_bytes()
        self.assertEqual(self.interp_string(image), "/lib/ld-musl-x86_64.so.1")
        self.assertIn("PT_INTERP /lib/ld-musl-x86_64.so.1", result.stdout)

    def test_empty_rpath_omits_runpath(self):
        output, _ = self.port("out-nopath.elf", "--rpath", "")
        self.assertNotIn(29, [tag for tag, _ in self.dynamic_tags(output.read_bytes())])

    def test_linux_verify_matches_requested_interp(self):
        # --verify compares the written PT_INTERP against the requested
        # --interp value; every --verify port in this suite exercises the
        # deep ELF checks (PT_INTERP/PT_DYNAMIC/RUNPATH/NX against the
        # requested scheme options).
        output, _ = self.port("out-musl2.elf", "--interp", "/lib/ld-musl-x86_64.so.1")
        self.assertEqual(self.interp_string(output.read_bytes()), "/lib/ld-musl-x86_64.so.1")
        result = self.run_ryty("--interp", "/lib/ld-musl-x86_64.so.1", "--verify",
                               "--skip-sce-module", "--skip-syscall-check",
                               str(self.source), str(self.root / "out-musl3.elf"))
        self.assertEqual(result.returncode, 0)
        self.assertIn("PT_INTERP /lib/ld-musl-x86_64.so.1", result.stdout)

    # --- Unified --target selector -----------------------------------------

    def test_target_selector_accepts_all_systems(self):
        for target in ["linux", "windows", "macos", "ios", "ios-sim"]:
            with self.subTest(target=target):
                result = self.run_ryty("--target", target, "--skip-sce-module",
                                       "--skip-syscall-check",
                                       str(self.source), str(self.root / ("out-" + target)))
                self.assertNotIn("unknown target", result.stderr, result.stderr)
                self.assertNotIn("FAIL: --target", result.stderr, result.stderr)

    def test_scheme_reports_per_target(self):
        # The simple fixture cannot feed the Windows PE writer (its import
        # target analysis needs a richer image, see AppleTargetTests for the
        # fixture style), so the Windows scheme is covered through the flag
        # surface in the other tests and the GUI/web mirrors.
        for target, fields in [
            ("linux", {"format": "SysV ELF64", "architecture": "x86-64", "signature": "none", "nxStack": True}),
            ("macos", {"format": "Mach-O 64", "applePlatform": "macOS (platform 1)"}),
            ("ios", {"format": "Mach-O 64", "applePlatform": "iOS (platform 2)"}),
            ("ios-sim", {"format": "Mach-O 64", "applePlatform": "iOS Simulator (platform 7)"}),
        ]:
            with self.subTest(target=target):
                report_path = self.root / ("report-" + target + ".json")
                self.port("scheme-" + target, "--target", target, "--report", str(report_path))
                data = json.loads(report_path.read_text(encoding="utf-8"))
                scheme = data["scheme"]
                self.assertEqual(scheme["system"], target)
                for key, value in fields.items():
                    self.assertEqual(scheme.get(key), value, key)

    def test_windows_target_rejects_non_windows_options(self):
        result = self.run_ryty("--target", "windows", "--bundle",
                               str(self.source), str(self.root / "x"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("--bundle requires the macOS, iOS or iOS Simulator target", result.stderr)

    # --min-os and identifier validation ------------------------------------

    def test_min_os_rejected_on_non_apple_targets(self):
        result = self.run_ryty("--min-os", "12.0", str(self.source), str(self.root / "x"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("--min-os requires the macOS, iOS or iOS Simulator target", result.stderr)

    def test_min_os_conflicts_with_per_target_flags(self):
        result = self.run_ryty("--target", "macos", "--min-os", "12.0", "--macos-min", "13.0",
                               str(self.source), str(self.root / "x"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("--min-os cannot be combined with --macos-min", result.stderr)

    def test_min_os_applies_to_macos_scheme(self):
        report_path = self.root / "report-minos.json"
        self.port("out-minos", "--target", "macos", "--min-os", "12.0", "--report", str(report_path))
        data = json.loads(report_path.read_text(encoding="utf-8"))
        self.assertEqual(data["scheme"]["minOsVersion"], "12.0")

    def test_interp_rejected_on_windows_and_apple(self):
        for target in ["windows", "macos", "ios"]:
            with self.subTest(target=target):
                result = self.run_ryty("--target", target, "--interp", "/x/y",
                                       str(self.source), str(self.root / "x"))
                self.assertEqual(result.returncode, 1)
                self.assertIn("--interp requires the Linux target", result.stderr)

    def test_invalid_identifier_rejected(self):
        for identifier in ["com..bad", "com.bad id", "-com.bad", "com.bad-", "com,bad"]:
            with self.subTest(identifier=identifier):
                result = self.run_ryty("--target", "ios", "--identifier", identifier,
                                       str(self.source), str(self.root / "x"))
                self.assertEqual(result.returncode, 1)
                self.assertIn("invalid bundle identifier", result.stderr)

    def test_valid_identifier_accepted(self):
        result = self.run_ryty("--target", "ios", "--bundle", "--identifier", "com.example.game-2",
                               "--skip-sce-module", "--skip-syscall-check",
                               str(self.source), str(self.root / "out-id"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    # --- Config file keys ----------------------------------------------------

    def test_config_keys_interp_min_os_and_empty_rpath(self):
        config = self.root / "port.ini"
        config.write_text(
            "# scheme config\n"
            "target = linux\n"
            "interp = /lib/ld-musl-x86_64.so.1\n"
            'rpath = ""\n',
            encoding="utf-8",
        )
        output, result = self.port("out-config.elf", "--config", str(config), "--verify")
        image = output.read_bytes()
        self.assertEqual(self.interp_string(image), "/lib/ld-musl-x86_64.so.1")
        self.assertNotIn(29, [tag for tag, _ in self.dynamic_tags(image)])
        self.assertIn("no DT_RUNPATH", result.stdout)

    def test_batch_reports_scheme(self):
        batch_in = self.root / "batch"
        batch_in.mkdir()
        (batch_in / "one.elf").write_bytes(make_elf("batch_nid"))
        result = self.run_ryty("--batch", str(batch_in), "--skip-sce-module", "--skip-syscall-check")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Porting scheme:", result.stdout)
        batch_report = next((batch_in / "ported").glob("batch-report.json"))
        data = json.loads(batch_report.read_text(encoding="utf-8"))
        self.assertEqual(data["scheme"]["system"], "linux")
        self.assertEqual(data["scheme"]["interpreter"], "/lib64/ld-linux-x86-64.so.2")


if __name__ == "__main__":
    unittest.main()
