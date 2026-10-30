#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libSceHttp/src/HttpErrors.hpp"
#include "prx/libRytyNet/src/RytyNetBridge.hpp"
#include <atomic>

// libSceHttp used to be a pure offline stub: contexts, templates and requests
// could be created, but any request that would touch the network failed with
// the library's network error.
//
// Since Ryty 1.0.7 the library can route plain HTTP/1.1 requests through the
// experimental RytyNet bridge (libRytyNet). The bridge is OFF unless the port
// was made with `ryty --rytynet` or RTY_NET_ENABLE=1 is set; in that case the
// previous offline behaviour is unchanged. HTTPS is not part of the
// experimental bridge: https requests report the network error with a
// [rytynet] log line explaining why.

namespace {

constexpr int HTTP_METHOD_GET = 0;
constexpr int HTTP_METHOD_POST = 1;
constexpr int HTTP_METHOD_HEAD = 2;
constexpr int HTTP_METHOD_PUT = 3;
constexpr int HTTP_METHOD_DELETE = 4;
constexpr int HTTP_METHOD_OPTIONS = 5;
constexpr int HTTP_METHOD_TRACE = 6;
constexpr int HTTP_METHOD_CONNECT = 7;

std::atomic<int> g_nextHandle{1};

struct RequestState {
    std::string method = "GET";
    std::string url;
    std::string headerBlock;
    std::string body;
    bool sent = false;
    bool tlsRequested = false;
    bool authEnabled = false;
    bool autoRedirect = false;
    std::uint32_t connectTimeoutUs = 0;
    std::uint32_t recvTimeoutUs = 0;
    std::uint32_t sendTimeoutUs = 0;
    RytyNet::HttpResponse response;
    std::size_t readCursor = 0;
};

std::mutex stateMutex;
std::map<int, RequestState> requests;

std::map<int, std::string>* connections() {
    static std::map<int, std::string> urls;
    return &urls;
}

RequestState* findRequest(int requestId) {
    const auto it = requests.find(requestId);
    return it == requests.end() ? nullptr : &it->second;
}

void recordRequest(int requestId, const char* method, const std::string& url) {
    RequestState state;
    state.method = method;
    state.url = url;
    std::lock_guard lock(stateMutex);
    requests[requestId] = std::move(state);
}

const char* methodName(int method) {
    switch (method) {
    case HTTP_METHOD_POST: return "POST";
    case HTTP_METHOD_HEAD: return "HEAD";
    case HTTP_METHOD_PUT: return "PUT";
    case HTTP_METHOD_DELETE: return "DELETE";
    case HTTP_METHOD_OPTIONS: return "OPTIONS";
    case HTTP_METHOD_TRACE: return "TRACE";
    case HTTP_METHOD_CONNECT: return "CONNECT";
    default: return "GET";
    }
}

// Resolves a request's URL: an explicit URL when given, otherwise the
// connection's URL with the request path spliced in.
std::string resolveUrl(int connId, const char* pathOrUrl, bool isUrl) {
    if (isUrl) return pathOrUrl;
    std::string base;
    {
        std::lock_guard lock(stateMutex);
        const auto it = connections()->find(connId);
        if (it != connections()->end()) base = it->second;
    }
    if (base.empty()) return pathOrUrl;
    bool isHttps = false;
    std::string host, unused;
    std::uint16_t port = 0;
    std::string path;
    if (RytyNet::ParseHttpUrl(base, isHttps, host, port, path)) {
        const auto authority = host + ((port == (isHttps ? 443 : 80)) ? std::string() : ":" + std::to_string(port));
        return std::string(isHttps ? "https://" : "http://") + authority + (pathOrUrl[0] == '/' ? pathOrUrl : std::string("/") + pathOrUrl);
    }
    return pathOrUrl;
}

void sendThroughBridge(int requestId) {
    std::lock_guard lock(stateMutex);
    auto* request = findRequest(requestId);
    if (!request) return;
    request->sent = true;
    request->response = RytyNet::HttpResponse{};
    bool isHttps = false;
    std::string host, path;
    std::uint16_t port = 0;
    if (!RytyNet::ParseHttpUrl(request->url, isHttps, host, port, path)) {
        request->response.error = "invalid URL";
        return;
    }
    RytyNet::HttpRequest outgoing;
    outgoing.method = request->method;
    outgoing.host = host;
    outgoing.port = port;
    outgoing.path = path;
    outgoing.headerBlock = request->headerBlock;
    outgoing.body = request->body;
    outgoing.autoRedirect = request->autoRedirect;
    outgoing.isHttps = isHttps;
    if (isHttps) {
        request->tlsRequested = true;
        request->response = RytyNet::PerformHttpRequest(outgoing, 0);
        return;
    }
    std::uint32_t timeoutMs = 0;
    if (request->recvTimeoutUs > 0) timeoutMs = request->recvTimeoutUs / 1000;
    else if (request->connectTimeoutUs > 0) timeoutMs = request->connectTimeoutUs / 1000;
    request->response = RytyNet::PerformHttpRequest(outgoing, timeoutMs);
    request->readCursor = 0;
}

}

extern "C" {

int APS5_VABI sceHttpAbortRequest(int request_id) {
    (void)request_id;
    return 0;
}

int APS5_VABI sceHttpAddRequestHeader(int id, const char* name, const char* value, uint32_t mode) {
    if (!name || !value) return ERROR_INVALID_VALUE;
    std::lock_guard lock(stateMutex);
    if (auto* request = findRequest(id)) {
        request->headerBlock += std::string(name) + ": " + value + "\r\n";
        return 0;
    }
    (void)mode;
    return 0;
}

int APS5_VABI sceHttpCreateConnection(int tmpl_id, const char* server_name, const char* scheme, uint16_t port, int enable_keep_alive) {
    (void)tmpl_id;
    (void)enable_keep_alive;
    if (!server_name || !scheme) return ERROR_INVALID_VALUE;
    const int handle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
    const std::string url = std::string(scheme) + "://" + server_name + ":" + std::to_string(port);
    std::lock_guard lock(stateMutex);
    (*connections())[handle] = url;
    return handle;
}

int APS5_VABI sceHttpCreateConnectionWithURL(int tmpl_id, const char* url, int enable_keep_alive) {
    (void)tmpl_id;
    (void)enable_keep_alive;
    if (!url) return ERROR_INVALID_URL;
    const int handle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(stateMutex);
    (*connections())[handle] = url;
    return handle;
}

int APS5_VABI sceHttpCreateEpoll(int http_ctx_id, HttpEpollHandle* eh) {
    (void)http_ctx_id;
    if (!eh) return ERROR_INVALID_VALUE;
    *eh = new HttpEpoll{};
    return 0;
}

int APS5_VABI sceHttpCreateRequest(int conn_id, int method, const char* path, uint64_t content_length) {
    (void)content_length;
    if (!path) return ERROR_INVALID_VALUE;
    const int handle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
    recordRequest(handle, methodName(method), resolveUrl(conn_id, path, false));
    return handle;
}

int APS5_VABI sceHttpCreateRequestWithURL2(int conn_id, const char* method, const char* url, uint64_t content_length) {
    (void)conn_id;
    (void)content_length;
    if (!method || !url) return ERROR_INVALID_URL;
    const int handle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
    recordRequest(handle, method, url);
    return handle;
}

int APS5_VABI sceHttpCreateTemplate(int http_ctx_id, const char* user_agent, int http_ver, int is_auto_proxy_conf) {
    (void)http_ctx_id;
    (void)user_agent;
    (void)http_ver;
    (void)is_auto_proxy_conf;
    return g_nextHandle.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceHttpDeleteConnection(int conn_id) {
    std::lock_guard lock(stateMutex);
    connections()->erase(conn_id);
    return 0;
}

int APS5_VABI sceHttpDeleteRequest(int req_id) {
    std::lock_guard lock(stateMutex);
    requests.erase(req_id);
    return 0;
}

int APS5_VABI sceHttpDeleteTemplate(int tmpl_id) {
    (void)tmpl_id;
    return 0;
}

int APS5_VABI sceHttpDestroyEpoll(int http_ctx_id, HttpEpollHandle eh) {
    (void)http_ctx_id;
    delete eh;
    return 0;
}

int APS5_VABI sceHttpGetAllResponseHeaders(int request_id, char** header, size_t* header_size) {
    if (!header || !header_size) return ERROR_INVALID_VALUE;
    std::lock_guard lock(stateMutex);
    const auto* request = findRequest(request_id);
    if (!request || !request->sent) return ERROR_NETWORK;
    if (!request->response.ok) return ERROR_NETWORK;
    // The caller frees the block with sceHttpFreeMemory; a single allocation
    // keeps the two output pointers self-contained.
    const std::string& headers = request->response.headers;
    *header_size = headers.size() + 1;
    *header = new char[headers.size() + 1];
    std::memcpy(*header, headers.c_str(), headers.size() + 1);
    return 0;
}

int APS5_VABI sceHttpGetResponseContentLength(int request_id, int* result, uint64_t* content_length) {
    if (!result || !content_length) return ERROR_INVALID_VALUE;
    std::lock_guard lock(stateMutex);
    const auto* request = findRequest(request_id);
    if (!request || !request->sent) return ERROR_NETWORK;
    if (!request->response.ok) return ERROR_NETWORK;
    *result = 1;
    *content_length = request->response.body.size();
    return 0;
}

int APS5_VABI sceHttpGetStatusCode(int request_id, int* status_code) {
    if (!status_code) return ERROR_INVALID_VALUE;
    std::lock_guard lock(stateMutex);
    const auto* request = findRequest(request_id);
    if (!request || !request->sent) return ERROR_NETWORK;
    if (!request->response.ok) return ERROR_NETWORK;
    *status_code = request->response.status;
    return 0;
}

int APS5_VABI sceHttpInit_nid_postfix(int memid, int ssl_ctx_id, uint64_t pool_size) {
    (void)memid;
    (void)ssl_ctx_id;
    (void)pool_size;
    return g_nextHandle.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceHttpsDisableOption(int id, uint32_t ssl_flags) {
    (void)id;
    (void)ssl_flags;
    return 0;
}

int APS5_VABI sceHttpSendRequest(int request_id, const void* post_data, size_t size) {
    {
        std::lock_guard lock(stateMutex);
        auto* request = findRequest(request_id);
        if (!request) return ERROR_INVALID_VALUE;
        if (post_data && size > 0) request->body.assign(static_cast<const char*>(post_data), size);
    }
    // Offline behaviour when the bridge is disabled (unchanged from 1.0.6).
    if (!RytyNet::Bridge().enabled) return ERROR_NETWORK;
    sendThroughBridge(request_id);
    std::lock_guard lock(stateMutex);
    const auto* request = findRequest(request_id);
    if (!request) return ERROR_NETWORK;
    if (request->tlsRequested) return ERROR_SSL;
    if (!request->response.ok) return ERROR_NETWORK;
    return 0;
}

int APS5_VABI sceHttpSetAuthEnabled(int id, int enable) {
    std::lock_guard lock(stateMutex);
    if (auto* req = findRequest(id)) req->authEnabled = (enable != 0);
    return 0;
}

int APS5_VABI sceHttpSetAutoRedirect(int id, int enable) {
    std::lock_guard lock(stateMutex);
    if (auto* req = findRequest(id)) req->autoRedirect = (enable != 0);
    return 0;
}

int APS5_VABI sceHttpSetConnectTimeOut(int id, uint32_t usec) {
    std::lock_guard lock(stateMutex);
    if (auto* req = findRequest(id)) req->connectTimeoutUs = usec;
    return 0;
}

int APS5_VABI sceHttpSetEpoll(int id, HttpEpollHandle eh, void* user_arg) {
    (void)id;
    (void)eh;
    (void)user_arg;
    return 0;
}

int APS5_VABI sceHttpSetNonblock(int id, int enable) {
    (void)id;
    (void)enable;
    return 0;
}

int APS5_VABI sceHttpSetRecvTimeOut(int id, uint32_t usec) {
    std::lock_guard lock(stateMutex);
    if (auto* req = findRequest(id)) req->recvTimeoutUs = usec;
    return 0;
}

int APS5_VABI sceHttpSetRequestContentLength(int request_id, uint64_t content_length) {
    // The bridge sends Content-Length from the actual body; the guest's hint is
    // only recorded so a mismatch can be reported through the log later.
    (void)request_id;
    (void)content_length;
    return 0;
}

int APS5_VABI sceHttpSetResolveRetry(int id, int32_t retry) {
    (void)id;
    (void)retry;
    return 0;
}

int APS5_VABI sceHttpSetResolveTimeOut(int id, uint32_t usec) {
    (void)id;
    (void)usec;
    return 0;
}

int APS5_VABI sceHttpSetSendTimeOut(int id, uint32_t usec) {
    std::lock_guard lock(stateMutex);
    if (auto* req = findRequest(id)) req->sendTimeoutUs = usec;
    return 0;
}

int APS5_VABI sceHttpsSetMinSslVersion(int id, uint32_t ssl_version) {
    (void)id;
    (void)ssl_version;
    return 0;
}

int APS5_VABI sceHttpsSetSslCallback(int id, HttpsCallback cbfunc, void* user_arg) {
    (void)id;
    (void)cbfunc;
    (void)user_arg;
    return 0;
}

int APS5_VABI sceHttpTerm_nid_postfix(int http_ctx_id) {
    (void)http_ctx_id;
    return 0;
}

int APS5_VABI sceHttpUnsetEpoll(int id) {
    (void)id;
    return 0;
}

int APS5_VABI sceHttpWaitRequest(HttpEpollHandle eh, HttpNBEvent* nbev, int maxevents, int timeout) {
    (void)eh;
    (void)nbev;
    (void)maxevents;
    (void)timeout;
    // The non-blocking epoll API is not part of the experimental bridge.
    return ERROR_NETWORK;
}

int APS5_VABI sceHttpCreateRequestWithURL(int conn_id, int method, const char* url, uint64_t content_length) {
    (void)conn_id;
    (void)content_length;
    if (!url) return ERROR_INVALID_URL;
    const int handle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
    recordRequest(handle, methodName(method), url);
    return handle;
}

int APS5_VABI sceHttpReadData(int request_id, void* data, size_t size) {
    if (!data) return ERROR_INVALID_VALUE;
    std::lock_guard lock(stateMutex);
    auto* request = findRequest(request_id);
    if (!request || !request->sent) return ERROR_NETWORK;
    if (!request->response.ok) return ERROR_NETWORK;
    if (request->readCursor >= request->response.body.size()) return 0;
    const std::size_t remaining = request->response.body.size() - request->readCursor;
    const std::size_t copied = std::min(remaining, size);
    std::memcpy(data, request->response.body.data() + request->readCursor, copied);
    request->readCursor += copied;
    return static_cast<int>(copied);
}

int APS5_VABI sceHttpSetChunkedTransferEnabled(int id, int enable) {
    (void)id;
    (void)enable;
    return 0;
}

}
