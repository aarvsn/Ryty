#include <elfpatcher/apple/Entitlements.hpp>
#include <algorithm>
#include <stdexcept>

namespace Elfpatcher::Apple {

namespace {

std::string Header() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\">\n"
           "<dict>\n";
}

std::string Key(const std::string& name) {
    return "  <key>" + name + "</key>\n";
}

std::string True() {
    return "  <true/>\n";
}

std::string False() {
    return "  <false/>\n";
}

}

std::vector<std::string> EntitlementsTemplateNames() {
    return {"macos-app-sandbox", "macos-jit", "ios-device", "ios-debug"};
}

std::vector<std::uint8_t> MakeEntitlementsTemplate(const std::string& name,
                                                   const std::string& bundleIdentifier) {
    (void)bundleIdentifier; // reserved for templates that reference the app id
    std::string plist;
    if (name == "macos-app-sandbox") {
        plist = Header()
            + Key("com.apple.security.app-sandbox") + True()
            + Key("com.apple.security.files.user-selected.read-write") + True()
            + Key("com.apple.security.network.client") + True()
            + Key("com.apple.security.network.server") + True()
            + "</dict>\n</plist>\n";
    } else if (name == "macos-jit") {
        plist = Header()
            + Key("com.apple.security.cs.allow-jit") + True()
            + Key("com.apple.security.cs.allow-unsigned-executable-memory") + True()
            + Key("com.apple.security.cs.allow-dyld-environment-variables") + True()
            + Key("com.apple.security.cs.disable-library-validation") + True()
            + "</dict>\n</plist>\n";
    } else if (name == "ios-device") {
        plist = Header()
            + Key("get-task-allow") + False()
            + Key("com.apple.developer.kernel.increased-memory-limit") + True()
            + "</dict>\n</plist>\n";
    } else if (name == "ios-debug") {
        plist = Header()
            + Key("get-task-allow") + True()
            + Key("com.apple.developer.kernel.increased-memory-limit") + True()
            + "</dict>\n</plist>\n";
    } else {
        std::string valid;
        for (const std::string& candidate : EntitlementsTemplateNames()) {
            if (!valid.empty()) valid += ", ";
            valid += candidate;
        }
        throw std::runtime_error("unknown entitlements template: " + name + " (valid templates: " + valid + ")");
    }
    return std::vector<std::uint8_t>(plist.begin(), plist.end());
}

}
