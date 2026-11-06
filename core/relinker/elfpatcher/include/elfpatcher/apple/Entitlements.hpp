#ifndef ELFPATCHER_APPLE_ENTITLEMENTS_HPP
#define ELFPATCHER_APPLE_ENTITLEMENTS_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace Elfpatcher::Apple {

// Built-in entitlements presets for the macOS, iOS and iOS Simulator targets.
//
// A preset spares users a hand-written plist for the common porting cases:
// every template is a well-formed XML property list embedded as
// CSSLOT_ENTITLEMENTS into the ad-hoc code signature. `bundleIdentifier` is
// substituted into keys that reference the app (currently none require it, but
// the parameter keeps the signature stable for future templates).
//
//   macos-app-sandbox - App Sandbox plus the read/write user-selected files
//                       and network client/server grants a game port needs.
//   macos-jit         - hardened-runtime exceptions for guest code: JIT,
//                       unsigned executable memory, dyld environment variables
//                       and disabled library validation.
//   ios-device        - distribution-style device build: get-task-allow=false
//                       and the increased-memory-limit kernel entitlement.
//   ios-debug         - development-style device build: get-task-allow=true so
//                       lldb can attach through the A64 translation runtime.

std::vector<std::string> EntitlementsTemplateNames();

// Builds the plist bytes for `name`. Throws std::runtime_error (with the list
// of valid names) for an unknown preset.
std::vector<std::uint8_t> MakeEntitlementsTemplate(const std::string& name,
                                                   const std::string& bundleIdentifier);

}

#endif
