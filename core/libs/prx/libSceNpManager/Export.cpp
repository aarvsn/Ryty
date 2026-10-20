#include <cstdint>
#include <cstddef>
#include <cstring>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libRytyNet/src/RytyNetBridge.hpp"
#include <atomic>

// PSN is not emulated. Since Ryty 1.0.7, when the experimental RytyNet bridge
// is active (RTY_NET_ENABLE=1 or a --rytynet port), the user is reported as
// signed in with a local identity so games that refuse to start while signed
// out can reach their menus; nothing talks to Sony's servers. Without the
// bridge the offline behaviour below is unchanged.
static constexpr int SCE_NP_ERROR_INVALID_ARGUMENT = static_cast<int>(0x80550003);
static constexpr int SCE_NP_ERROR_SIGNED_OUT = static_cast<int>(0x80550006);
static constexpr uint32_t NP_STATE_SIGNED_OUT = 1;
static constexpr uint32_t NP_STATE_SIGNED_IN = 0;
static constexpr int NP_POLL_ASYNC_FINISHED = 0;
static constexpr uint32_t NP_REACHABILITY_STATE_UNAVAILABLE = 0;
static constexpr uint32_t NP_REACHABILITY_STATE_AVAILABLE = 1;

std::atomic<int> g_nextRequest{1};

// The local identity RytyNet reports while the bridge is active.
constexpr const char* RYTYNET_ONLINE_ID = "RytyPlayer";
constexpr std::uint64_t RYTYNET_ACCOUNT_ID = 0x5259545900000001ULL; // 'RYTY' + 1

bool npOnlineMode() {
    return RytyNet::Bridge().enabled;
}

void fillOnlineId(NpOnlineId* online_id) {
    if (!online_id) return;
    std::memset(online_id, 0, sizeof(*online_id));
    std::strncpy(online_id->data, RYTYNET_ONLINE_ID, sizeof(online_id->data) - 1);
}

extern "C" {

int APS5_VABI sceNpAbortRequest(int req_id) {
    (void)req_id;
    return 0;
}

int APS5_VABI sceNpCheckCallback(void) {
    return 0;
}

int APS5_VABI sceNpCheckNpAvailability(int req_id, const char* user, void* result) {
 (void)req_id;
 (void)user;
 (void)result;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

int APS5_VABI sceNpCheckNpReachability(int req_id, int user_id) {
 (void)req_id;
 (void)user_id;
 if (npOnlineMode()) return 0;
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpCheckPremium(int req_id, const NpCheckPremiumParameter* param, NpCheckPremiumResult* result) {
    (void)req_id;
    (void)param;
    (void)result;
    if (npOnlineMode()) {
        // No PSN behind the bridge: report a plain (non-premium) account.
        if (result) std::memset(result, 0, sizeof(*result));
        return 0;
    }
    return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpCreateAsyncRequest(const NpCreateAsyncRequestParameter* param) {
    (void)param;
    return g_nextRequest.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpCreateRequest(void) {
    return g_nextRequest.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpDeleteRequest(int req_id) {
    (void)req_id;
    return 0;
}

int APS5_VABI sceNpGetAccountAge(int req_id, int user_id, uint8_t* age) {
 (void)req_id;
 (void)user_id;
 if (!age) return SCE_NP_ERROR_INVALID_ARGUMENT;
 if (npOnlineMode()) {
     *age = 0;
     return 0;
 }
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpGetAccountCountryA(int user_id, void* country_code) {
    (void)user_id;
    if (npOnlineMode() && country_code) {
        std::memcpy(country_code, "US\0", 3);
        return 0;
    }
    return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpGetAccountIdA(int user_id, uint64_t* account_id) {
    (void)user_id;
    if (npOnlineMode() && account_id) {
        *account_id = RYTYNET_ACCOUNT_ID;
        return 0;
    }
    return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpGetNpId(int user_id, NpId* np_id) {
 (void)user_id;
 if (!np_id) return SCE_NP_ERROR_INVALID_ARGUMENT;
 if (npOnlineMode()) {
     fillOnlineId(&np_id->online_id);
     std::memset(np_id->opaque, 0, sizeof(np_id->opaque));
     return 0;
 }
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

int APS5_VABI sceNpGetNpReachabilityState(int user_id, uint32_t* state) {
    (void)user_id;
    if (!state) return SCE_NP_ERROR_INVALID_ARGUMENT;
    *state = npOnlineMode() ? NP_REACHABILITY_STATE_AVAILABLE : NP_REACHABILITY_STATE_UNAVAILABLE;
    return 0;
}

int APS5_VABI sceNpGetOnlineId(int user_id, NpOnlineId* online_id) {
    (void)user_id;
    if (!online_id) return SCE_NP_ERROR_INVALID_ARGUMENT;
    if (npOnlineMode()) {
        fillOnlineId(online_id);
        return 0;
    }
    return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpGetState(int user_id, uint32_t* state) {
    (void)user_id;
    if (!state) return SCE_NP_ERROR_INVALID_ARGUMENT;
    *state = npOnlineMode() ? NP_STATE_SIGNED_IN : NP_STATE_SIGNED_OUT;
    return 0;
}

int APS5_VABI sceNpHasSignedUp(int user_id, bool* has_signed_up) {
    (void)user_id;
    if (!has_signed_up) return SCE_NP_ERROR_INVALID_ARGUMENT;
    *has_signed_up = npOnlineMode();
    return 0;
}

int APS5_VABI sceNpPollAsync(int req_id, int* result) {
    (void)req_id;
    if (result) *result = npOnlineMode() ? 0 : SCE_NP_ERROR_SIGNED_OUT;
    return NP_POLL_ASYNC_FINISHED;
}

void APS5_VABI sceNpRegisterGamePresenceCallback(void* callback, void* userdata) {
 (void)callback;
 (void)userdata;
 NotImplemented_nid_no_patch(__func__);
}

int APS5_VABI sceNpRegisterNpReachabilityStateCallback(void* callback, void* userdata) {
    (void)userdata;
    if (!callback) return SCE_NP_ERROR_INVALID_ARGUMENT;
    return 0;
}

int APS5_VABI sceNpRegisterPlusEventCallback(void* callback, void* userdata) {
    (void)userdata;
    if (!callback) return SCE_NP_ERROR_INVALID_ARGUMENT;
    return 0;
}

int APS5_VABI sceNpRegisterPremiumEventCallback(void* callback, void* userdata) {
    (void)userdata;
    if (!callback) return SCE_NP_ERROR_INVALID_ARGUMENT;
    return 0;
}

int APS5_VABI sceNpRegisterStateCallback(void* callback, void* userdata) {
    (void)userdata;
    if (!callback) return SCE_NP_ERROR_INVALID_ARGUMENT;
    return 0;
}

int APS5_VABI sceNpSetContentRestriction(const NpContentRestriction* restriction) {
 (void)restriction;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

int APS5_VABI sceNpSetNpTitleId(const NpTitleId* title_id, const NpTitleSecret* title_secret) {
    (void)title_id;
    (void)title_secret;
    return 0;
}

int APS5_VABI sceNpUnregisterStateCallback(void) {
    return 0;
}

int APS5_VABI sceNpGetAccountLanguage2(int req_id, int user_id, void* language) {
    (void)req_id;
    (void)user_id;
    if (npOnlineMode() && language) {
        std::memcpy(language, "en\0", 3);
        return 0;
    }
    return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpNotifyPremiumFeature(const void* param) {
    (void)param;
    return 0;
}

int APS5_VABI sceNpRegisterStateCallbackA(void* callback, void* userdata) {
    (void)userdata;
    if (!callback) return SCE_NP_ERROR_INVALID_ARGUMENT;
    return 1;
}

int APS5_VABI sceNpUnregisterStateCallbackA(int callback_id) {
    (void)callback_id;
    return 0;
}

int APS5_VABI sceNpUnregisterPremiumEventCallback(void) {
    return 0;
}

}
