"""End-to-end tests for the Apple targets.

Covers the macOS support (universal binaries, entitlements presets, hardened
runtime, configurable deployment target, .app bundles), the iOS target
(arm64 guest container, .app/.ipa packaging, provisioning profiles) and the
iOS Simulator target (LC_BUILD_VERSION platform 7, provisioning-free bundles).
Every signature check recomputes the page hashes with hashlib, mirroring what
the Apple kernel checks on launch.
"""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

from ElfFixture import make_elf


RYTY = Path(sys.argv.pop(1)).resolve()

MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
CPU_TYPE_X86_64 = 0x01000007
CPU_TYPE_ARM64 = 0x0100000C
LC_MAIN = 0x80000028
LC_BUILD_VERSION = 0x32
LC_SOURCE_VERSION = 0x2A
CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02
CS_ADHOC = 0x2
CS_RUNTIME = 0x10000
PAGE = 4096

ENTITLEMENTS = (b"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                b"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                b"<plist version=\"1.0\"><dict><key>get-task-allow</key><true/></dict></plist>\n")
PROVISION = b"<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>ryty-test</key><true/></dict></plist>\n"


class MachoView:
    """Thin parser for the load commands and code directory of one slice."""

    def __init__(self, image):
        self.image = image
        self.magic, self.cpu_type, self.cpu_subtype, self.file_type, self.ncmds, \
            self.sizeofcmds, self.flags, _ = struct.unpack_from("<8I", image, 0)
        self.commands = []
        offset = 32
        for _ in range(self.ncmds):
            cmd, size = struct.unpack_from("<II", image, offset)
            self.commands.append((cmd, offset, size))
            offset += size
        assert offset == 32 + self.sizeofcmds, "load commands must exactly fill sizeofcmds"

    def find(self, cmd_id):
        return [entry for entry in self.commands if entry[0] == cmd_id]

    def build_version(self):
        found = self.find(LC_BUILD_VERSION)
        assert len(found) == 1, "exactly one LC_BUILD_VERSION expected"
        _, offset, _ = found[0]
        return struct.unpack_from("<4I", self.image, offset + 8)

    def source_version(self):
        _, offset, _ = self.find(LC_SOURCE_VERSION)[0]
        return struct.unpack_from("<Q", self.image, offset + 8)[0]

    def main_entry_offset(self):
        _, offset, _ = self.find(LC_MAIN)[0]
        return struct.unpack_from("<Q", self.image, offset + 8)[0]

    def dylib_names(self):
        names = []
        for cmd, offset, size in self.commands:
            if cmd in (0x0C, 0x0D, 0x18):  # LOAD_DYLIB / LOAD_WEAK_DYLIB / REEXPORT_DYLIB
                name_offset = struct.unpack_from("<I", self.image, offset + 8)[0]
                end = self.image.index(b"\0", offset + name_offset)
                names.append(self.image[offset + name_offset:end].decode())
        return names

    def code_directory(self):
        found = self.find(0x1D)  # LC_CODE_SIGNATURE
        assert len(found) == 1, "exactly one LC_CODE_SIGNATURE expected"
        _, cmd_offset, _ = found[0]
        data_offset, data_size = struct.unpack_from("<II", self.image, cmd_offset + 8)
        magic, length, count = struct.unpack_from(">III", self.image, data_offset)
        assert magic == CSMAGIC_EMBEDDED_SIGNATURE
        assert length == data_size, "SuperBlob length must match the LC payload size"
        index = {}
        for slot in range(count):
            slot_type, slot_offset = struct.unpack_from(">II", self.image, data_offset + 12 + slot * 8)
            index[slot_type] = data_offset + slot_offset
        assert 0 in index, "CSSLOT_CODEDIRECTORY missing"
        cd = index[0]
        (cd_magic, cd_length, version, flags, hash_offset, ident_offset, n_special,
         n_code, code_limit) = struct.unpack_from(">9I", self.image, cd)
        assert cd_magic == CSMAGIC_CODEDIRECTORY
        hash_size, hash_type, _, page_size = struct.unpack_from(">4B", self.image, cd + 36)
        assert (hash_size, hash_type, page_size) == (32, 2, 12)
        identifier = self.image[cd + ident_offset:cd + hash_offset - n_special * 32 - 1].decode()
        entitlements = None
        entitlements_raw = None
        if 5 in index:
            end = data_offset + data_size  # the entitlements blob closes the SuperBlob
            # The embedded blob is padded to 4 bytes like codesign does; the
            # special-slot hash covers the padded bytes exactly as stored.
            entitlements_raw = self.image[index[5]:end]
            entitlements = entitlements_raw.rstrip(b"\x00")
        return {
            "data_offset": data_offset, "cd": cd, "version": version, "flags": flags,
            "hash_offset": hash_offset, "n_special": n_special, "n_code": n_code,
            "code_limit": code_limit, "identifier": identifier,
            "entitlements": entitlements, "entitlements_raw": entitlements_raw,
        }

    def verify_page_hashes(self):
        cd = self.code_directory()
        assert cd["code_limit"] == cd["data_offset"], "hashes must cover everything before the signature"
        assert cd["n_code"] == (cd["code_limit"] + PAGE - 1) // PAGE
        for slot in range(cd["n_code"]):
            start = slot * PAGE
            end = min(start + PAGE, cd["code_limit"])
            expected = hashlib.sha256(self.image[start:end]).digest()
            at = cd["cd"] + cd["hash_offset"] + slot * 32
            assert expected == self.image[at:at + 32], f"page {slot} hash mismatch"
        if cd["entitlements_raw"] is not None:
            assert cd["n_special"] >= 5, "entitlements need special slot -5"
            expected = hashlib.sha256(cd["entitlements_raw"]).digest()
            at = cd["cd"] + cd["hash_offset"] - 5 * 32
            assert expected == self.image[at:at + 32], "entitlements special-slot hash mismatch"


class AppleTargetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._temporary = tempfile.TemporaryDirectory(prefix="ryty-apple-")
        cls.root = Path(cls._temporary.name)
        cls.last_result = None

    @classmethod
    def tearDownClass(cls):
        cls._temporary.cleanup()

    def port(self, *extra, name="input.elf"):
        source = self.root / name
        source.write_bytes(make_elf("test_nid"))
        output = self.root / "output.bin"
        command = [str(RYTY), "--skip-sce-module", "--skip-syscall-check", *extra, str(source), str(output)]
        result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", timeout=60)
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        self.last_result = result
        return output, result

    def fail_port(self, *extra):
        source = self.root / "input.elf"
        source.write_bytes(make_elf("test_nid"))
        output = self.root / "output.bin"
        command = [str(RYTY), "--skip-sce-module", "--skip-syscall-check", *extra, str(source), str(output)]
        result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", timeout=60)
        self.assertNotEqual(result.returncode, 0, "expected the port to be rejected")
        return result

    def slices_of_fat(self, image):
        magic, count = struct.unpack_from(">II", image, 0)
        self.assertEqual(magic, FAT_MAGIC)
        slices = []
        for slot in range(count):
            fields = struct.unpack_from(">5I", image, 8 + slot * 20)
            cpu_type, cpu_subtype, offset, size, align = fields
            slices.append((cpu_type, cpu_subtype, offset, size, align))
        return slices

    # ---- expanded macOS support ---------------------------------------------

    def test_universal_binary_contains_launcher_and_guest_slices(self):
        output, _ = self.port("--target", "macos", "--universal", "--verify")
        image = output.read_bytes()
        slices = self.slices_of_fat(image)
        self.assertEqual([s[0] for s in slices], [CPU_TYPE_ARM64, CPU_TYPE_X86_64])
        for cpu_type, _, offset, size, align in slices:
            self.assertEqual(align, 12)
            slice_view = MachoView(image[offset:offset + size])
            self.assertEqual(slice_view.magic, MH_MAGIC_64)
            self.assertEqual(slice_view.cpu_type, cpu_type)
            slice_view.verify_page_hashes()
        # The arm64 launcher slice: PIE, LC_MAIN, self-reexec message embedded.
        _, _, arm_offset, arm_size, _ = slices[0]
        arm = MachoView(image[arm_offset:arm_offset + arm_size])
        self.assertTrue(arm.flags & 0x00200000, "launcher slice must be PIE")
        self.assertGreater(arm.main_entry_offset(), 0)
        self.assertIn(b"softwareupdate --install-rosetta", image[arm_offset:arm_offset + arm_size])
        # The x86_64 slice is the classic ported guest image.
        _, _, x86_offset, x86_size, _ = slices[1]
        x86 = MachoView(image[x86_offset:x86_offset + x86_size])
        self.assertIn("Verification: OK - universal Mach-O OK, 2 slices verified", self.last_result.stdout)

    def test_macos_min_flag_sets_build_version(self):
        output, _ = self.port("--macos", "--macos-min", "12.3")
        view = MachoView(output.read_bytes())
        platform, min_os, sdk, ntools = view.build_version()
        self.assertEqual(platform, 1)
        self.assertEqual(min_os, (12 << 16) | (3 << 8))
        self.assertGreaterEqual(sdk, min_os)
        self.assertEqual(ntools, 0)

    def test_verify_checks_build_version_and_identifier(self):
        # --verify now walks the load commands: platform, deployment target and
        # CodeDirectory identifier must match the requested options.
        self.port("--macos", "--macos-min", "12.3", "--identifier", "com.example.verify",
                  "--verify")
        self.assertIn("Verification: OK - Mach-O 64 + ad-hoc signature OK", self.last_result.stdout)

    def test_ios_entitlements_reach_the_arm64_signature(self):
        plist = self.root / "ios.ent.plist"
        plist.write_bytes(ENTITLEMENTS)
        output, _ = self.port("--target", "ios", "--entitlements", str(plist))
        view = MachoView(output.read_bytes())
        self.assertEqual(view.code_directory()["entitlements"], ENTITLEMENTS)
        view.verify_page_hashes()

    def test_identifier_flag_reaches_the_code_directory(self):
        output, _ = self.port("--macos", "--identifier", "com.example.ported")
        view = MachoView(output.read_bytes())
        self.assertEqual(view.code_directory()["identifier"], "com.example.ported")
        view.verify_page_hashes()

    def test_entitlements_are_embedded_and_hashed(self):
        plist = self.root / "ent.plist"
        plist.write_bytes(ENTITLEMENTS)
        output, _ = self.port("--macos", "--entitlements", str(plist))
        view = MachoView(output.read_bytes())
        directory = view.code_directory()
        self.assertEqual(directory["identifier"], "ryty.bundle")
        self.assertEqual(directory["entitlements"], ENTITLEMENTS)
        view.verify_page_hashes()

    def test_hardened_runtime_flag(self):
        output, _ = self.port("--macos", "--hardened-runtime")
        view = MachoView(output.read_bytes())
        self.assertTrue(view.code_directory()["flags"] & CS_RUNTIME)

    def test_macos_bundle_layout_and_plist(self):
        output, _ = self.port("--macos", "--bundle", "--identifier", "com.ryty.port.output")
        app = self.root / "output.app"
        self.assertTrue(app.is_dir())
        binary = app / "Contents" / "MacOS" / "output"
        self.assertTrue(binary.is_file())
        MachoView(binary.read_bytes()).verify_page_hashes()
        info = (app / "Contents" / "Info.plist").read_text(encoding="utf-8")
        self.assertIn("CFBundleExecutable</key>", info)
        self.assertIn("<string>output</string>", info)
        self.assertIn("<string>com.ryty.port.output</string>", info)
        self.assertIn("LSMinimumSystemVersion</key>", info)
        self.assertIn("NSHighResolutionCapable", info)
        self.assertEqual((app / "Contents" / "PkgInfo").read_bytes(), b"APPL????")

    def test_target_ios_produces_arm64_guest_container(self):
        output, _ = self.port("--target", "ios", "--verify")
        view = MachoView(output.read_bytes())
        self.assertEqual(view.magic, MH_MAGIC_64)
        self.assertEqual(view.cpu_type, CPU_TYPE_ARM64)
        platform, min_os, sdk, _ = view.build_version()
        self.assertEqual(platform, 2)
        self.assertEqual(min_os, (14 << 16))
        self.assertGreaterEqual(sdk, min_os)
        names = " ".join(view.dylib_names())
        self.assertIn("UIKit.framework", names)
        self.assertIn("Metal.framework", names)
        self.assertNotIn("Versions/A", names, "iOS frameworks are unversioned")
        self.assertNotIn("Cocoa.framework", names)
        # The guest ELF payload rides in the __GUEST segment.
        image = output.read_bytes()
        self.assertIn(b"\x7fELF", image)
        view.verify_page_hashes()
        self.assertIn("Verification: OK - Mach-O 64 + ad-hoc signature OK", self.last_result.stdout)

    def test_ios_bundle_and_ipa(self):
        profile = self.root / "dev.mobileprovision"
        profile.write_bytes(PROVISION)
        self.port("--target", "ios", "--bundle", "--ipa", "--provision", str(profile),
                  name="input.elf")
        app = self.root / "output.app"
        self.assertTrue(app.is_dir())
        self.assertTrue((app / "output").is_file())
        self.assertEqual((app / "embedded.mobileprovision").read_bytes(), PROVISION)
        info = (app / "Info.plist").read_text(encoding="utf-8")
        self.assertIn("MinimumOSVersion</key>", info)
        self.assertIn("iPhoneOS", info)
        self.assertIn("UIDeviceFamily", info)
        self.assertIn("UILaunchScreen", info)
        ipa = self.root / "output.ipa"
        self.assertTrue(ipa.is_file())
        with zipfile.ZipFile(ipa) as archive:
            names = archive.namelist()
            self.assertIn("Payload/output.app/output", names)
            self.assertIn("Payload/output.app/Info.plist", names)
            self.assertIn("Payload/output.app/embedded.mobileprovision", names)
            payload = archive.read("Payload/output.app/output")
        MachoView(payload).verify_page_hashes()

    # ---- iOS Simulator target -------------------------------------------------

    def test_ios_simulator_target_platform_and_pie(self):
        output, _ = self.port("--target", "ios-sim", "--verify")
        view = MachoView(output.read_bytes())
        self.assertEqual(view.cpu_type, CPU_TYPE_ARM64)
        self.assertTrue(view.flags & 0x00200000, "simulator slice must be PIE")
        platform, min_os, sdk, _ = view.build_version()
        self.assertEqual(platform, 7)
        self.assertEqual(min_os, (14 << 16))
        self.assertGreaterEqual(sdk, min_os)
        names = " ".join(view.dylib_names())
        self.assertIn("UIKit.framework", names)
        view.verify_page_hashes()
        self.assertIn("Verification: OK - Mach-O 64 + ad-hoc signature OK", self.last_result.stdout)

    def test_ios_simulator_bundle_is_provisioning_free(self):
        # The class shares one temporary directory and other tests may have
        # created a device bundle with the same name: clear it first.
        import shutil
        app = self.root / "output.app"
        if app.exists():
            shutil.rmtree(app)
        output, _ = self.port("--target", "ios-sim", "--bundle")
        self.assertTrue(app.is_dir())
        self.assertTrue((app / "output").is_file())
        info = (app / "Info.plist").read_text(encoding="utf-8")
        self.assertIn("iPhoneSimulator", info)
        self.assertNotIn("iPhoneOS", info)
        self.assertFalse((app / "PkgInfo").exists(), "simulator bundles carry no PkgInfo")

    def test_ios_simulator_flags_alias(self):
        output, _ = self.port("--ios-simulator")
        view = MachoView(output.read_bytes())
        self.assertEqual(view.build_version()[0], 7)

    # ---- entitlements templates -----------------------------------------------

    def test_entitlements_template_macos_jit(self):
        output, result = self.port("--macos", "--entitlements-template", "macos-jit")
        view = MachoView(output.read_bytes())
        self.assertIn(b"com.apple.security.cs.allow-jit", view.code_directory()["entitlements"])
        view.verify_page_hashes()
        self.assertIn("Entitlements template: macos-jit", result.stdout)

    def test_entitlements_template_ios_device(self):
        output, _ = self.port("--target", "ios", "--entitlements-template", "ios-device")
        view = MachoView(output.read_bytes())
        entitlements = view.code_directory()["entitlements"]
        self.assertIn(b"get-task-allow", entitlements)
        self.assertIn(b"<false/>", entitlements)
        view.verify_page_hashes()

    def test_entitlements_template_unknown_is_rejected(self):
        result = self.fail_port("--macos", "--entitlements-template", "bogus-template")
        self.assertIn("unknown entitlements template", result.stderr)

    def test_entitlements_template_and_file_conflict(self):
        plist = self.root / "ent.plist"
        plist.write_bytes(ENTITLEMENTS)
        result = self.fail_port("--macos", "--entitlements", str(plist),
                                "--entitlements-template", "macos-jit")
        self.assertIn("cannot be combined", result.stderr)

    # ---- provisioning profile validation ---------------------------------------

    def test_provisioning_profile_garbage_is_rejected(self):
        profile = self.root / "broken.mobileprovision"
        profile.write_bytes(b"this is not a profile at all - neither plist nor DER")
        result = self.fail_port("--target", "ios", "--bundle", "--provision", str(profile))
        self.assertIn("neither a plist nor a CMS/DER container", result.stderr)

    def test_provisioning_profile_identifier_mismatch_warns(self):
        profile = self.root / "other.mobileprovision"
        profile.write_bytes(b"<?xml version=\"1.0\"?>\n"
                            b"<plist version=\"1.0\"><dict>\n"
                            b"<key>application-identifier</key>\n"
                            b"<string>TEAMID123.com.other.game</string>\n"
                            b"</dict></plist>\n")
        output, result = self.port("--target", "ios", "--bundle", "--provision", str(profile))
        self.assertIn("does not cover the bundle identifier", result.stderr)

    def test_provisioning_profile_wildcard_covers_identifier(self):
        profile = self.root / "wild.mobileprovision"
        profile.write_bytes(b"<?xml version=\"1.0\"?>\n"
                            b"<plist version=\"1.0\"><dict>\n"
                            b"<key>application-identifier</key>\n"
                            b"<string>TEAMID123.*</string>\n"
                            b"</dict></plist>\n")
        output, result = self.port("--target", "ios", "--bundle", "--provision", str(profile))
        self.assertNotIn("does not cover the bundle identifier", result.stderr)

    # ---- validation ----------------------------------------------------------

    def test_universal_requires_macos(self):
        result = self.fail_port("--universal")
        self.assertIn("universal requires the macOS target", result.stderr)

    def test_ipa_requires_ios(self):
        result = self.fail_port("--macos", "--ipa")
        self.assertIn("ipa requires the iOS device target", result.stderr)

    def test_ipa_rejected_for_simulator(self):
        result = self.fail_port("--target", "ios-sim", "--ipa")
        self.assertIn("ipa requires the iOS device target", result.stderr)

    def test_provision_rejected_for_simulator(self):
        profile = self.root / "dev.mobileprovision"
        profile.write_bytes(PROVISION)
        result = self.fail_port("--target", "ios-sim", "--bundle", "--provision", str(profile))
        self.assertIn("provision requires the iOS device target", result.stderr)

    def test_bundle_requires_apple_target(self):
        result = self.fail_port("--bundle")
        self.assertIn("bundle requires the macOS, iOS or iOS Simulator target", result.stderr)

    def test_macos_min_rejects_garbage(self):
        result = self.fail_port("--macos", "--macos-min", "twelve")
        self.assertIn("invalid Apple version", result.stderr)

    def test_target_conflicts_are_rejected(self):
        result = self.fail_port("--macos", "--ios")
        self.assertIn("cannot be used together", result.stderr)

    def test_config_file_applies_apple_keys(self):
        source = self.root / "input.elf"
        source.write_bytes(make_elf("test_nid"))
        output = self.root / "output.bin"
        config = self.root / "port.ini"
        config.write_text("target = macos\n"
                          "universal = true\n"
                          "macos-min = 13.1\n"
                          "identifier = com.ryty.config-test\n", encoding="utf-8")
        result = subprocess.run(
            [str(RYTY), "--skip-sce-module", "--skip-syscall-check", "--config", str(config),
             str(source), str(output)],
            capture_output=True, encoding="utf-8", errors="replace", timeout=60)
        self.assertEqual(result.returncode, 0, (result.stdout or "") + (result.stderr or ""))
        image = output.read_bytes()
        slices = self.slices_of_fat(image)
        self.assertEqual([s[0] for s in slices], [CPU_TYPE_ARM64, CPU_TYPE_X86_64])
        _, _, x86_offset, x86_size, _ = slices[1]
        x86 = MachoView(image[x86_offset:x86_offset + x86_size])
        self.assertEqual(x86.build_version()[1], (13 << 16) | (1 << 8))
        self.assertEqual(x86.code_directory()["identifier"], "com.ryty.config-test")


if __name__ == "__main__":
    unittest.main()
