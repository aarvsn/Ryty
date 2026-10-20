#ifndef CORE_LIBS_PRX_LIBRYTUNET_SRC_RYTNETBRIDGE_HPP
#define CORE_LIBS_PRX_LIBRYTUNET_SRC_RYTNETBRIDGE_HPP

#include <cstddef>
#include <cstdint>
#include <string>

// RytyNet is the experimental online-mode bridge introduced in Ryty 1.0.7. It
// gives the ported game's network libraries a direct path onto the host's IP
// stack (plain TCP HTTP/1.1 for now), instead of the offline error returns the
// libraries use by default. It is OFF unless explicitly enabled:
//   * the environment variable RTY_NET_ENABLE=1 (or =0 to force off), or
//   * a <executable>.rytynet marker written by `ryty --rytynet`.
// Everything here lives inside libRytyNet.prx; libSceHttp and libSceNpManager
// link the bridge and keep their offline behaviour when it is disabled.

namespace RytyNet {

enum class BridgeOverride { None, Enabled, Disabled };

struct BridgeState {
    bool enabled = false;
    // Why the bridge is (dis)abled, for the [rytynet] log line.
    const char* reason = "disabled by default";
};

// Reads RTY_NET_ENABLE and the marker file next to the running executable.
// Deterministic per process; the first call logs the decision.
BridgeState Bridge();

// Diagnostic override used by RytyNetSetEnabled; it replaces the cached
// decision (None restores the environment/marker one).
void OverrideBridge(BridgeOverride override);

bool LogEnabled();

// ---- HTTP/1.1 over the host's TCP stack -------------------------------------

struct HttpResponse {
    bool ok = false;          // a full response was parsed
    int status = 0;           // HTTP status code
    std::string headers;      // raw response header block (CRLF line endings)
    std::string body;         // decoded body (chunked transfer decoding applied)
    std::string error;        // when !ok: what went wrong
};

struct HttpRequest {
    std::string method;       // "GET", "POST", ...
    std::string host;         // Host header and connect target
    std::uint16_t port = 80;
    std::string path;         // request path including query
    std::string headerBlock;  // extra request headers, CRLF terminated lines
    std::string body;         // request body ("" when none)
};

// Performs one blocking HTTP/1.1 request. `timeoutMs` bounds connect and the
// total exchange (0 = the bridge's default, 15 s). TLS is not part of the
// experimental bridge: https requests report an error instead of touching the
// network.
HttpResponse PerformHttpRequest(const HttpRequest& request, std::uint32_t timeoutMs);

// Splits "scheme://host:port/path" into its parts; false when the URL is not a
// usable http URL (https parses but PerformHttpRequest refuses it).
bool ParseHttpUrl(const std::string& url, bool& isHttps, std::string& host, std::uint16_t& port, std::string& path);

}

#endif
