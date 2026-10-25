#include "RytyNetBridge.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__APPLE__)
#include <arpa/inet.h>
#include <cctype>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#else
#include <arpa/inet.h>
#include <cctype>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

// The bridge keeps to the same conventions as the other runtime libraries: a
// [rytynet] prefixed log line on stderr, host sockets behind small wrappers,
// and strict error reporting instead of silent approximation.

namespace RytyNet {

namespace {

constexpr std::uint32_t DefaultTimeoutMs = 15000;
constexpr std::size_t MaxResponseBytes = 64 * 1024 * 1024;

#ifdef _WIN32
using NativeSocket = SOCKET;
using NativeLength = int;
constexpr NativeSocket InvalidNativeSocket = INVALID_SOCKET;
bool initializeSockets() {
    static const int result = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data);
    }();
    return result == 0;
}
void closeSocket(NativeSocket socket) { closesocket(socket); }
int lastError() { return WSAGetLastError(); }
#else
using NativeSocket = int;
using NativeLength = socklen_t;
constexpr NativeSocket InvalidNativeSocket = -1;
bool initializeSockets() { return true; }
void closeSocket(NativeSocket socket) { ::close(socket); }
int lastError() { return errno; }
#endif

void log(const char* format, ...) {
    if (!LogEnabled()) return;
    va_list arguments;
    va_start(arguments, format);
    std::fputs("[rytynet] ", stderr);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}

std::string executablePath() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    return length > 0 && length < MAX_PATH ? std::string(path) : std::string();
#elif defined(__APPLE__)
    std::uint32_t size = 4096;
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        buffer.resize(std::strlen(buffer.c_str()));
        return buffer;
    }
    buffer.resize(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        buffer.resize(std::strlen(buffer.c_str()));
        return buffer;
    }
    return std::string();
#else
    std::error_code error;
    const auto link = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::string() : link.string();
#endif
}

std::mutex bridgeMutex;
BridgeState bridgeState{false, "not probed yet"};
BridgeOverride bridgeOverride = BridgeOverride::None;

// Runs the environment/marker probe. The caller holds bridgeMutex.
BridgeState probeBridgeLocked() {
    if (std::strcmp(bridgeState.reason, "not probed yet") != 0) return bridgeState;
    if (!initializeSockets()) {
        bridgeState = {false, "host socket initialization failed"};
    } else if (const char* enable = std::getenv("RTY_NET_ENABLE")) {
        const bool on = std::strcmp(enable, "1") == 0 || std::strcmp(enable, "on") == 0 || std::strcmp(enable, "true") == 0;
        const bool off = std::strcmp(enable, "0") == 0 || std::strcmp(enable, "off") == 0 || std::strcmp(enable, "false") == 0;
        if (on) bridgeState = {true, "RTY_NET_ENABLE=1"};
        else if (off) bridgeState = {false, "RTY_NET_ENABLE=0"};
        else bridgeState = {false, "RTY_NET_ENABLE is set but not a boolean value"};
    } else {
        const std::string executable = executablePath();
        if (!executable.empty()) {
            std::error_code error;
            const auto marker = std::filesystem::path(executable).parent_path()
                / (std::filesystem::path(executable).filename().string() + ".rytynet");
            if (std::filesystem::exists(marker, error) && !error) bridgeState = {true, "marker file present (ryty --rytynet)"};
        }
        if (!bridgeState.enabled) bridgeState = {false, "disabled by default (set RTY_NET_ENABLE=1 or port with --rytynet)"};
    }
    log("%s", bridgeState.reason);
    return bridgeState;
}

}

void OverrideBridge(BridgeOverride override) {
    std::lock_guard lock(bridgeMutex);
    bridgeOverride = override;
    if (override != BridgeOverride::None) (void)probeBridgeLocked(); // probe once so None later restores a real decision
}

BridgeState Bridge() {
    std::lock_guard lock(bridgeMutex);
    if (bridgeOverride == BridgeOverride::Enabled) return BridgeState{true, "overridden by RytyNetSetEnabled"};
    if (bridgeOverride == BridgeOverride::Disabled) return BridgeState{false, "overridden by RytyNetSetEnabled"};
    return probeBridgeLocked();
}

bool LogEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("RTY_NET_LOG");
        if (!value) return false;
        return std::strcmp(value, "0") != 0 && std::strcmp(value, "off") != 0 && std::strcmp(value, "false") != 0;
    }();
    return enabled;
}

bool ParseHttpUrl(const std::string& url, bool& isHttps, std::string& host, std::uint16_t& port, std::string& path) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    const std::string scheme = url.substr(0, schemeEnd);
    if (scheme == "http") isHttps = false;
    else if (scheme == "https") isHttps = true;
    else return false;
    std::string authority = url.substr(schemeEnd + 3);
    const auto pathStart = authority.find('/');
    if (pathStart == std::string::npos) {
        path = "/";
    } else {
        path = authority.substr(pathStart);
        authority = authority.substr(0, pathStart);
    }
    if (authority.empty()) return false;
    // Strip userinfo if present.
    const auto at = authority.rfind('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    if (!authority.empty() && authority.front() == '[') {
        // IPv6 literal [::1]:8080
        const auto close = authority.find(']');
        if (close == std::string::npos) return false;
        host = authority.substr(1, close - 1);
        if (close + 2 < authority.size() && authority[close + 1] == ':') {
            const auto parsed = std::strtoul(authority.c_str() + close + 2, nullptr, 10);
            if (parsed == 0 || parsed > 65535) return false;
            port = static_cast<std::uint16_t>(parsed);
        } else {
            port = isHttps ? 443 : 80;
        }
        return true;
    }
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(':') == colon) {
        host = authority.substr(0, colon);
        const auto parsed = std::strtoul(authority.c_str() + colon + 1, nullptr, 10);
        if (parsed == 0 || parsed > 65535) return false;
        port = static_cast<std::uint16_t>(parsed);
    } else {
        host = authority;
        port = isHttps ? 443 : 80;
    }
    return !host.empty();
}

namespace {

bool waitForSocket(NativeSocket socket, bool write, std::uint32_t timeoutMs) {
#ifdef _WIN32
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(socket, &fds);
    timeval timeout{static_cast<long>(timeoutMs / 1000), static_cast<long>((timeoutMs % 1000) * 1000)};
    const int ready = ::select(0, write ? nullptr : &fds, write ? &fds : nullptr, nullptr, &timeout);
    return ready > 0;
#else
    pollfd fds{socket, static_cast<short>(write ? POLLOUT : POLLIN), 0};
    const int ready = ::poll(&fds, 1, static_cast<int>(timeoutMs));
    return ready == 1 && (fds.revents & (write ? POLLOUT : (POLLIN | POLLHUP | POLLERR))) != 0;
#endif
}

bool sendAll(NativeSocket socket, const std::uint8_t* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
#ifdef _WIN32
        const int chunk = ::send(socket, reinterpret_cast<const char*>(data + sent), static_cast<int>(std::min<std::size_t>(size - sent, 1u << 20)), 0);
#else
        const ssize_t chunk = ::send(socket, data + sent, std::min<std::size_t>(size - sent, 1u << 20), MSG_NOSIGNAL);
#endif
        if (chunk <= 0) return false;
        sent += static_cast<std::size_t>(chunk);
    }
    return true;
}

// Reads until the header block is complete and (per Content-Length or chunked
// framing) the body is too. Returns false on timeout or truncation.
bool receiveResponse(NativeSocket socket, std::string& raw, std::uint32_t timeoutMs) {
    std::uint8_t buffer[16384];
    std::size_t headerEnd = std::string::npos;
    const auto started = std::chrono::steady_clock::now();
    auto remainingMs = [&]() -> std::uint32_t {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        return elapsed >= timeoutMs ? 0 : static_cast<std::uint32_t>(timeoutMs - elapsed);
    };
    while (true) {
        if (headerEnd == std::string::npos) {
            headerEnd = raw.find("\r\n\r\n");
        }
        std::size_t need = 0;
        if (headerEnd != std::string::npos) {
            // Determine how much more the body requires.
            std::size_t bodyStart = headerEnd + 4;
            bool chunked = false;
            std::size_t contentLength = 0;
            std::size_t lineStart = 0;
            while (lineStart < headerEnd) {
                std::size_t lineEnd = raw.find("\r\n", lineStart);
                if (lineEnd == std::string::npos || lineEnd > headerEnd) break;
                const std::string line = raw.substr(lineStart, lineEnd - lineStart);
                const auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string name = line.substr(0, colon);
                    std::string value = line.substr(colon + 1);
                    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
                    if (name == "transfer-encoding" && value.find("chunked") != std::string::npos) chunked = true;
                    else if (name == "content-length") contentLength = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
                }
                lineStart = lineEnd + 2;
            }
            if (chunked) {
                // A chunked body ends with "0\r\n\r\n".
                const std::size_t terminator = raw.find("0\r\n\r\n", bodyStart);
                if (terminator != std::string::npos) return true;
                need = bodyStart + 1;
            } else {
                if (raw.size() >= bodyStart + contentLength) return true;
                need = bodyStart + contentLength;
            }
            if (need > MaxResponseBytes) return false;
        }
        if (!waitForSocket(socket, false, remainingMs())) return false;
#ifdef _WIN32
        const int received = ::recv(socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
#else
        const ssize_t received = ::recv(socket, buffer, sizeof(buffer), 0);
#endif
        if (received <= 0) return false; // closed mid-response or error
        raw.append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(received));
        if (raw.size() > MaxResponseBytes) return false;
    }
}

std::string decodeChunkedBody(const std::string& raw, std::size_t bodyStart) {
    std::string body;
    std::size_t cursor = bodyStart;
    while (cursor < raw.size()) {
        const auto lineEnd = raw.find("\r\n", cursor);
        if (lineEnd == std::string::npos) break;
        const std::size_t size = static_cast<std::size_t>(std::strtoull(raw.c_str() + cursor, nullptr, 16));
        if (size == 0) break;
        if (lineEnd + 2 + size > raw.size()) break;
        body.append(raw, lineEnd + 2, size);
        cursor = lineEnd + 2 + size + 2; // skip the chunk data and its CRLF
        if (body.size() > MaxResponseBytes) break;
    }
    return body;
}

}

HttpResponse PerformHttpRequest(const HttpRequest& request, std::uint32_t timeoutMs) {
    HttpResponse response;
    if (timeoutMs == 0) timeoutMs = DefaultTimeoutMs;
    if (!Bridge().enabled) {
        response.error = "RytyNet is disabled";
        return response;
    }
    // Resolve the host (name or literal) and connect a plain TCP socket.
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    char portText[6];
    std::snprintf(portText, sizeof(portText), "%u", request.port);
    addrinfo* results = nullptr;
    if (getaddrinfo(request.host.c_str(), portText, &hints, &results) != 0 || results == nullptr) {
        response.error = "name resolution failed for " + request.host;
        log("resolve failed host=%s", request.host.c_str());
        return response;
    }
    NativeSocket socket = InvalidNativeSocket;
    for (addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
        socket = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (socket == InvalidNativeSocket) continue;
        // Non-blocking connect with a poll-bounded wait keeps the game's thread
        // from hanging for the OS default on unreachable hosts.
#ifdef _WIN32
        u_long nonBlocking = 1;
        ioctlsocket(socket, FIONBIO, &nonBlocking);
#else
        const int flags = ::fcntl(socket, F_GETFL, 0);
        ::fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif
        if (::connect(socket, candidate->ai_addr, static_cast<NativeLength>(candidate->ai_addrlen)) == 0) break;
        int connectError = lastError();
#ifdef _WIN32
        if (connectError == WSAEWOULDBLOCK) {
#else
        if (connectError == EINPROGRESS) {
#endif
            if (waitForSocket(socket, true, timeoutMs)) {
                int soError = 0;
                NativeLength soLength = sizeof(soError);
                getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &soLength);
                if (soError == 0) break;
            }
        }
        closeSocket(socket);
        socket = InvalidNativeSocket;
    }
    freeaddrinfo(results);
    if (socket == InvalidNativeSocket) {
        response.error = "connect to " + request.host + " failed";
        log("connect failed host=%s port=%u", request.host.c_str(), request.port);
        return response;
    }

    // Build the wire request. The bridge keeps the exchange to HTTP/1.1 without
    // keep-alive: one request, one response, then the socket is closed.
    std::string wire = request.method + " " + request.path + " HTTP/1.1\r\n";
    wire += "Host: " + request.host + "\r\n";
    if (!request.body.empty()) {
        wire += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
    }
    wire += "Connection: close\r\n";
    wire += request.headerBlock;
    if (wire.find("User-Agent:") == std::string::npos) {
        wire += "User-Agent: RytyNet/1.0.9 (experimental)\r\n";
    }
    wire += "\r\n";
    wire += request.body;

    const bool sent = sendAll(socket, reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size());
    std::string raw;
    bool received = sent && receiveResponse(socket, raw, timeoutMs);
    closeSocket(socket);
    if (!sent) {
        response.error = "sending the request to " + request.host + " failed";
        return response;
    }
    if (!received) {
        response.error = "incomplete or too large response from " + request.host;
        log("exchange failed host=%s status=none bytes=%zu", request.host.c_str(), raw.size());
        return response;
    }

    // Parse the status line and header block.
    const auto headerEnd = raw.find("\r\n\r\n");
    const std::string statusLine = raw.substr(0, raw.find("\r\n"));
    if (statusLine.rfind("HTTP/1.", 0) != 0) {
        response.error = "malformed response status line";
        return response;
    }
    const auto space = statusLine.find(' ');
    if (space == std::string::npos) {
        response.error = "malformed response status line";
        return response;
    }
    response.status = std::atoi(statusLine.c_str() + space + 1);
    response.headers = raw.substr(0, headerEnd + 2);
    const std::size_t bodyStart = headerEnd + 4;
    bool chunked = response.headers.find("Transfer-Encoding:") != std::string::npos
        && response.headers.find("chunked") != std::string::npos;
    response.body = chunked ? decodeChunkedBody(raw, bodyStart) : raw.substr(bodyStart);
    response.ok = true;
    log("%s %s:%u -> status=%d body=%zu", request.method.c_str(), request.host.c_str(), request.port, response.status, response.body.size());
    return response;
}

}
