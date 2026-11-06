#ifndef ELFPATCHER_APPLE_APPLEBUNDLE_HPP
#define ELFPATCHER_APPLE_APPLEBUNDLE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace Elfpatcher::Apple {

// 2.0.0: .app bundle / .ipa archive writers for the macOS, iOS and iOS
// Simulator targets.

struct BundleSpec {
    bool ios = false;               // iOS device bundle (iPhoneOS keys)
    bool simulator = false;         // iOS Simulator bundle (iPhoneSimulator keys)
    std::string appPath;            // .../Foo.app (created)
    std::string executablePath;     // path the Mach-O is written to (inside the bundle)
    std::string executableName;     // CFBundleExecutable (e.g. "Foo")
    std::string bundleIdentifier;   // CFBundleIdentifier
    std::string displayName;        // CFBundleName / CFBundleDisplayName
    std::string minOsVersion;       // "11.0" (macOS) / "14.0" (iOS)
    std::string version;            // CFBundleShortVersionString
    std::string provisionProfile;   // optional embedded.mobileprovision source (iOS)
};

// Generates the Info.plist XML for the bundle spec (escaped).
std::string BuildInfoPlist(const BundleSpec& spec);

// Creates the bundle directory structure and writes Info.plist + PkgInfo.
// The executable itself is written by the caller (it owns the Mach-O bytes);
// `executablePath` in the spec tells the caller where to put it.
void CreateBundleSkeleton(const BundleSpec& spec);

// Copies the provisioning profile into the bundle (iOS). Throws on I/O errors.
void EmbedProvisioningProfile(const BundleSpec& spec);

// Outcome of ValidateProvisioningProfile.
struct ProvisionCheckResult {
    bool ok = false;                       // false: hard error (throw in the writer)
    std::vector<std::string> warnings;     // non-fatal observations
};

// Sanity-checks a .mobileprovision before it is embedded into an iOS device
// bundle. Accepts XML plists and DER/CMS containers (the real-world format);
// anything else is rejected. When the profile is a plist with an
// application-identifier that does not cover `bundleIdentifier`, a warning is
// reported (the embedder still proceeds, mirroring what codesign does).
ProvisionCheckResult ValidateProvisioningProfile(const std::string& path,
                                                 const std::string& bundleIdentifier);

// Zips `appPath` into an .ipa: Payload/<AppName.app>/... Returns the ipa path.
std::string WriteIpa(const std::string& appPath, const std::string& ipaPath);

}

#endif
