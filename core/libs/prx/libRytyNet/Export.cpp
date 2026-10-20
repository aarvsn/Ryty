#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include "SceTypes.hpp"
#include "prx/libRytyNet/src/RytyNetBridge.hpp"

// libRytyNet is the Ryty 1.0.7 experimental online-mode bridge. The library
// itself carries the host-side HTTP client and the enablement policy; the game
// facing libraries (libSceHttp, libSceNpManager) call into it and fall back to
// their offline behaviour while it is disabled. The exports below expose the
// bridge state to guests and diagnostics so a port can report why online mode
// is (or is not) active. Everything here is experimental: names and behaviour
// may change without notice.

namespace {

constexpr std::uint32_t RYTNET_VERSION = 0x00010007u; // 1.0.7

struct RytyNetStatus {
    std::uint32_t structSize;   // sizeof(RytyNetStatus), set by the caller
    std::uint32_t version;      // RYTNET_VERSION
    std::int32_t  enabled;      // nonzero when the bridge is active
    std::int32_t  onlineId;     // reserved
    char          reason[128];  // why the bridge is (dis)abled, NUL padded
};

}

extern "C" {

// Returns nonzero when the experimental online mode is active for this process.
int APS5_VABI RytyNetIsEnabled(void) {
    return RytyNet::Bridge().enabled ? 1 : 0;
}

// The bridge version (1.0.7 -> 0x00010007).
std::uint32_t APS5_VABI RytyNetVersion(void) {
    return RYTNET_VERSION;
}

// Fills a caller-provided RytyNetStatus with the bridge state; the struct size
// gate keeps the ABI inspectable while the layout is experimental.
int APS5_VABI RytyNetGetStatus(RytyNetStatus* status) {
    if (!status || status->structSize < sizeof(RytyNetStatus)) return -1;
    const RytyNet::BridgeState state = RytyNet::Bridge();
    status->version = RYTNET_VERSION;
    status->enabled = state.enabled ? 1 : 0;
    status->onlineId = 0;
    std::memset(status->reason, 0, sizeof(status->reason));
    std::strncpy(status->reason, state.reason, sizeof(status->reason) - 1);
    return 0;
}

// Forces the bridge decision for diagnostics; 1 enables, 0 disables, anything
// else returns to the environment/marker decision. Diagnostic use only - real
// setups enable the bridge with RTY_NET_ENABLE=1 or `ryty --rytynet`.
int APS5_VABI RytyNetSetEnabled(int enable) {
    RytyNet::OverrideBridge(enable == 0 ? RytyNet::BridgeOverride::Disabled : (enable == 1 ? RytyNet::BridgeOverride::Enabled : RytyNet::BridgeOverride::None));
    return 0;
}

}
