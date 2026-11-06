#include <elfpatcher/apple/AppleBundle.hpp>
#include <io/ZipWriter.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace Elfpatcher::Apple {

namespace {

std::string EscapeXml(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char character : value) {
        switch (character) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += character;
        }
    }
    return out;
}

std::vector<std::uint8_t> ReadAllBytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read file: " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

void WriteTextFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write file: " + path.string());
    output << content;
    output.close();
    if (!output) throw std::runtime_error("failed to write file: " + path.string());
}

}

std::string BuildInfoPlist(const BundleSpec& spec) {
    std::ostringstream plist;
    plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          << "<plist version=\"1.0\">\n"
          << "<dict>\n"
          << "  <key>CFBundleExecutable</key>\n  <string>" << EscapeXml(spec.executableName) << "</string>\n"
          << "  <key>CFBundleIdentifier</key>\n  <string>" << EscapeXml(spec.bundleIdentifier) << "</string>\n"
          << "  <key>CFBundleName</key>\n  <string>" << EscapeXml(spec.displayName) << "</string>\n"
          << "  <key>CFBundleDisplayName</key>\n  <string>" << EscapeXml(spec.displayName) << "</string>\n"
          << "  <key>CFBundlePackageType</key>\n  <string>APPL</string>\n"
          << "  <key>CFBundleInfoDictionaryVersion</key>\n  <string>6.0</string>\n"
          << "  <key>CFBundleShortVersionString</key>\n  <string>" << EscapeXml(spec.version) << "</string>\n"
          << "  <key>CFBundleVersion</key>\n  <string>" << EscapeXml(spec.version) << "</string>\n";
    if (spec.ios) {
        plist << "  <key>MinimumOSVersion</key>\n  <string>" << EscapeXml(spec.minOsVersion) << "</string>\n";
        if (spec.simulator) {
            plist << "  <key>CFBundleSupportedPlatforms</key>\n  <array>\n    <string>iPhoneSimulator</string>\n  </array>\n";
        } else {
            plist << "  <key>CFBundleSupportedPlatforms</key>\n  <array>\n    <string>iPhoneOS</string>\n  </array>\n"
                  << "  <key>LSRequiresIPhoneOS</key>\n  <true/>\n";
        }
        plist << "  <key>UIDeviceFamily</key>\n  <array>\n    <integer>1</integer>\n    <integer>2</integer>\n  </array>\n"
              << "  <key>UILaunchScreen</key>\n  <dict/>\n"
              << "  <key>UIRequiredDeviceCapabilities</key>\n  <array>\n    <string>arm64</string>\n  </array>\n";
    } else {
        plist << "  <key>LSMinimumSystemVersion</key>\n  <string>" << EscapeXml(spec.minOsVersion) << "</string>\n"
              << "  <key>NSHighResolutionCapable</key>\n  <true/>\n"
              << "  <key>LSApplicationCategoryType</key>\n  <string>public.app-category.games</string>\n";
    }
    plist << "</dict>\n</plist>\n";
    return plist.str();
}

void CreateBundleSkeleton(const BundleSpec& spec) {
    // The executable directory is created here so guest modules and resources
    // can be written next to it before the Mach-O lands.
    std::filesystem::create_directories(std::filesystem::path(spec.executablePath).parent_path());
    const std::filesystem::path app(spec.appPath);
    if (spec.ios) {
        WriteTextFile(app / "Info.plist", BuildInfoPlist(spec));
        if (!spec.simulator) {
            WriteTextFile(app / "PkgInfo", "APPL????");
        }
    } else {
        WriteTextFile(app / "Contents" / "Info.plist", BuildInfoPlist(spec));
        WriteTextFile(app / "Contents" / "PkgInfo", "APPL????");
    }
}

void EmbedProvisioningProfile(const BundleSpec& spec) {
    if (spec.provisionProfile.empty()) return;
    const std::vector<std::uint8_t> profile = ReadAllBytes(spec.provisionProfile);
    const std::filesystem::path destination = spec.ios
        ? std::filesystem::path(spec.appPath) / "embedded.mobileprovision"
        : std::filesystem::path(spec.appPath) / "Contents" / "embedded.provisionprofile";
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write provisioning profile: " + destination.string());
    output.write(reinterpret_cast<const char*>(profile.data()), static_cast<std::streamsize>(profile.size()));
    output.close();
    if (!output) throw std::runtime_error("failed to write provisioning profile: " + destination.string());
}

ProvisionCheckResult ValidateProvisioningProfile(const std::string& path,
                                                 const std::string& bundleIdentifier) {
    ProvisionCheckResult result;
    const std::vector<std::uint8_t> profile = ReadAllBytes(path);
    if (profile.empty()) throw std::runtime_error("provisioning profile is empty: " + path);

    // Real .mobileprovision files are CMS (DER) signed: an ASN.1 SEQUENCE
    // (0x30 0x82 ...) wrapping an XML plist. Accept either the DER container
    // or a bare plist; anything else cannot be a profile.
    const bool looksDer = profile.size() >= 4 && profile[0] == 0x30 && profile[1] == 0x82;
    const std::string text(reinterpret_cast<const char*>(profile.data()), profile.size());
    const bool looksPlist = text.find("<plist") != std::string::npos;
    if (!looksDer && !looksPlist) {
        throw std::runtime_error("provisioning profile is neither a plist nor a CMS/DER container: " + path);
    }

    // When the payload is a plist we can look at the application-identifier
    // entitlement: it has the shape "<TeamId>.<bundle id>" (often with a
    // trailing .* wildcard). A bundle identifier it does not cover is not a
    // hard error - codesign only warns at signing time - so we report it.
    if (looksPlist && !bundleIdentifier.empty()) {
        const std::size_t keyPos = text.find("application-identifier");
        if (keyPos != std::string::npos) {
            const std::size_t valuePos = text.find("<string>", keyPos);
            if (valuePos != std::string::npos) {
                const std::size_t valueEnd = text.find("</string>", valuePos);
                if (valueEnd != std::string::npos) {
                    std::string appId = text.substr(valuePos + 8, valueEnd - valuePos - 8);
                    // TeamId prefix, then the covered bundle identifier.
                    const std::size_t dot = appId.find('.');
                    std::string covered = dot == std::string::npos ? appId : appId.substr(dot + 1);
                    const bool wildcard = covered == "*" || (covered.size() > 1 && covered.substr(covered.size() - 2) == ".*");
                    if (wildcard && covered != "*") covered = covered.substr(0, covered.size() - 2);
                    // A profile covers the app when the bundle identifier is
                    // equal, or when the profile carries a trailing ".*"
                    // wildcard whose prefix matches.
                    const bool matches = covered == "*"
                        || (!wildcard && covered == bundleIdentifier)
                        || (wildcard && bundleIdentifier.rfind(covered, 0) == 0);
                    if (!matches) {
                        result.warnings.push_back(
                            "Provisioning profile application-identifier '" + appId
                            + "' does not cover the bundle identifier '" + bundleIdentifier + "'");
                    }
                }
            }
        } else {
            result.warnings.push_back("Provisioning profile has no application-identifier entry; "
                                      "the bundle identifier could not be cross-checked");
        }
    }
    result.ok = true;
    return result;
}

std::string WriteIpa(const std::string& appPath, const std::string& ipaPath) {
    const std::filesystem::path app(appPath);
    if (!std::filesystem::is_directory(app)) throw std::runtime_error("IPA source bundle is missing: " + appPath);
    const std::string appName = app.filename().string();

    std::vector<std::pair<std::filesystem::path, std::string>> files;
    for (const auto& item : std::filesystem::recursive_directory_iterator(app)) {
        if (item.is_regular_file()) {
            const auto relative = std::filesystem::relative(item.path(), app.parent_path());
            files.emplace_back(item.path(), relative.generic_string());
        }
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

    Io::ZipWriter zip;
    for (const auto& [path, arcName] : files) {
        std::vector<std::uint8_t> bytes = ReadAllBytes(path.string());
        zip.AddFile("Payload/" + arcName, bytes);
    }
    (void)appName;

    std::filesystem::path output(ipaPath);
    if (output.parent_path() != output.root_name()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream output_file(output, std::ios::binary | std::ios::trunc);
    if (!output_file) throw std::runtime_error("cannot write IPA: " + output.string());
    const std::vector<std::uint8_t> archive = zip.Finish();
    output_file.write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    output_file.close();
    if (!output_file) throw std::runtime_error("failed to write IPA: " + output.string());
    return output.string();
}

}
