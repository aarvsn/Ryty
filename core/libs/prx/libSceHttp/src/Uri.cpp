#include <cctype>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libSceHttp/src/HttpErrors.hpp"

namespace {

constexpr uint32_t URI_BUILD_WITH_SCHEME = 0x01;
constexpr uint32_t URI_BUILD_WITH_HOSTNAME = 0x02;
constexpr uint32_t URI_BUILD_WITH_PORT = 0x04;
constexpr uint32_t URI_BUILD_WITH_PATH = 0x08;
constexpr uint32_t URI_BUILD_WITH_USERNAME = 0x10;
constexpr uint32_t URI_BUILD_WITH_PASSWORD = 0x20;
constexpr uint32_t URI_BUILD_WITH_QUERY = 0x40;
constexpr uint32_t URI_BUILD_WITH_FRAGMENT = 0x80;

struct UriParts {
    bool opaque = false;
    std::string_view scheme;
    std::string_view username;
    std::string_view password;
    std::string_view hostname;
    std::string path;
    std::string_view query;
    std::string_view fragment;
    uint16_t port = 0;
};

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) != std::tolower(static_cast<unsigned char>(right[i]))) return false;
    }
    return true;
}

uint16_t defaultPort(std::string_view scheme) {
    if (equalsIgnoreCase(scheme, "http")) return 80;
    if (equalsIgnoreCase(scheme, "https")) return 443;
    return 0;
}

bool isHostCharacter(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == '_';
}

std::string removeDotSegments(std::string_view input) {
    std::string output;
    while (!input.empty()) {
        if (input.starts_with("../")) input.remove_prefix(3);
        else if (input.starts_with("./")) input.remove_prefix(2);
        else if (input.starts_with("/./")) input.remove_prefix(2);
        else if (input == "/.") input = "/";
        else if (input.starts_with("/../") || input == "/..") {
            input = input.size() == 3 ? std::string_view("/") : input.substr(3);
            const size_t slash = output.rfind('/');
            output.erase(slash == std::string::npos ? 0 : slash);
        } else if (input == "." || input == "..") input = {};
        else {
            const size_t end = input.find('/', 1);
            const size_t length = end == std::string_view::npos ? input.size() : end;
            output.append(input.substr(0, length));
            input.remove_prefix(length);
        }
    }
    return output;
}

int parseUri(std::string_view uri, UriParts& parts) {
    size_t schemeEnd = 0;
    if (!uri.empty() && std::isalpha(static_cast<unsigned char>(uri[0]))) {
        schemeEnd = 1;
        while (schemeEnd < uri.size() && (std::isalnum(static_cast<unsigned char>(uri[schemeEnd])) || uri[schemeEnd] == '+'
            || uri[schemeEnd] == '-' || uri[schemeEnd] == '.')) ++schemeEnd;
    }
    if (schemeEnd > 0 && schemeEnd < uri.size() && uri[schemeEnd] == ':') {
        parts.scheme = uri.substr(0, schemeEnd);
        uri.remove_prefix(schemeEnd + 1);
    }

    parts.opaque = !uri.starts_with("//");
    if (!parts.opaque) uri.remove_prefix(2);

    std::string_view authority = uri.substr(0, uri.find_first_of("/?#"));
    uri.remove_prefix(authority.size());

    const size_t at = authority.rfind('@');
    if (at != std::string_view::npos) {
        const std::string_view userinfo = authority.substr(0, at);
        const size_t colon = userinfo.find(':');
        parts.username = userinfo.substr(0, colon);
        if (colon != std::string_view::npos) parts.password = userinfo.substr(colon + 1);
        authority.remove_prefix(at + 1);
    }

    std::string_view port;
    if (authority.starts_with('[')) {
        const size_t close = authority.find(']');
        if (close == std::string_view::npos) return ERROR_INVALID_URL;
        parts.hostname = authority.substr(1, close - 1);
        authority.remove_prefix(close + 1);
        if (!authority.empty() && authority[0] != ':') return ERROR_INVALID_URL;
        if (!authority.empty()) port = authority.substr(1);
        for (const char c : parts.hostname) {
            if (!isHostCharacter(c) && c != ':') return ERROR_INVALID_URL;
        }
    } else {
        const size_t colon = authority.find(':');
        parts.hostname = authority.substr(0, colon);
        if (colon != std::string_view::npos) port = authority.substr(colon + 1);
        for (const char c : parts.hostname) {
            if (!isHostCharacter(c)) return ERROR_INVALID_URL;
        }
    }

    if (port.size() > 5) return ERROR_INVALID_URL;
    uint32_t portValue = 0;
    for (const char c : port) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return ERROR_INVALID_URL;
        portValue = portValue * 10 + static_cast<uint32_t>(c - '0');
    }
    if (portValue > 0xFFFF) return ERROR_INVALID_URL;
    parts.port = port.empty() ? defaultPort(parts.scheme) : static_cast<uint16_t>(portValue);

    const std::string_view path = uri.substr(0, uri.find_first_of("?#"));
    parts.path = removeDotSegments(path);
    uri.remove_prefix(path.size());

    if (uri.starts_with('?')) {
        parts.query = uri.substr(0, uri.find('#'));
        uri.remove_prefix(parts.query.size());
    }
    parts.fragment = uri;
    return 0;
}

int copyOut(const std::string& value, char* out, size_t* require, size_t prepare) {
    if (require) *require = value.size() + 1;
    if (!out) return 0;
    if (prepare < value.size() + 1) return ERROR_OUT_OF_MEMORY;
    std::memcpy(out, value.c_str(), value.size() + 1);
    return 0;
}

}

extern "C" {

int APS5_VABI sceHttpUriBuild(char* out, size_t* require, size_t prepare, const SceHttpUriElement* src_element, uint32_t option) {
    if (!src_element) return ERROR_INVALID_URL;
    if (!out && !require) return ERROR_INVALID_VALUE;

    auto field = [](const char* value) { return value ? std::string_view(value) : std::string_view(); };
    const std::string_view scheme = field(src_element->scheme);
    const std::string_view username = option & URI_BUILD_WITH_USERNAME ? field(src_element->username) : std::string_view();
    const std::string_view password = option & URI_BUILD_WITH_PASSWORD ? field(src_element->password) : std::string_view();
    const std::string_view hostname = option & URI_BUILD_WITH_HOSTNAME ? field(src_element->hostname) : std::string_view();

    std::string uri;
    if (option & URI_BUILD_WITH_SCHEME && !scheme.empty()) uri.append(scheme).push_back(':');
    if (!src_element->opaque && option & URI_BUILD_WITH_HOSTNAME) uri.append("//");
    uri.append(username);
    if (!password.empty()) uri.append(":").append(password);
    if (!username.empty() || !password.empty()) uri.push_back('@');
    if (hostname.find(':') != std::string_view::npos) uri.append("[").append(hostname).append("]");
    else uri.append(hostname);
    if (option & URI_BUILD_WITH_PORT && src_element->port != 0 && src_element->port != defaultPort(scheme)) {
        uri.append(":").append(std::to_string(src_element->port));
    }
    if (option & URI_BUILD_WITH_PATH) uri.append(field(src_element->path));
    if (option & URI_BUILD_WITH_QUERY) uri.append(field(src_element->query));
    if (option & URI_BUILD_WITH_FRAGMENT) uri.append(field(src_element->fragment));
    return copyOut(uri, out, require, prepare);
}

int APS5_VABI sceHttpUriEscape(char* out, size_t* require, size_t prepare, const char* in) {
    if (!in) return ERROR_INVALID_VALUE;
    if (!out && !require) return ERROR_INVALID_VALUE;

    static constexpr char hex[] = "0123456789ABCDEF";
    std::string escaped;
    for (const char* c = in; *c; ++c) {
        const auto value = static_cast<unsigned char>(*c);
        if (std::isalnum(value) || value == '-' || value == '_' || value == '.' || value == '~') {
            escaped.push_back(*c);
        } else {
            escaped.push_back('%');
            escaped.push_back(hex[value >> 4]);
            escaped.push_back(hex[value & 0xF]);
        }
    }
    return copyOut(escaped, out, require, prepare);
}

int APS5_VABI sceHttpUriParse(SceHttpUriElement* out, const char* src_url, void* pool, size_t* require, size_t prepare) {
    if (!src_url) return ERROR_INVALID_URL;
    const bool write = out && pool;
    if (!write && !require) return ERROR_INVALID_VALUE;

    UriParts parts;
    if (const int result = parseUri(src_url, parts); result != 0) return result;

    const std::string_view fields[] = {parts.scheme, parts.username, parts.password, parts.hostname, parts.path, parts.query, parts.fragment};
    size_t size = 0;
    for (const std::string_view field : fields) size += field.size() + 1;
    if (require) *require = size;
    if (!write) return 0;
    if (prepare < size) return ERROR_OUT_OF_MEMORY;

    char* next = static_cast<char*>(pool);
    char* strings[7];
    for (size_t i = 0; i < 7; ++i) {
        strings[i] = next;
        std::memcpy(next, fields[i].data(), fields[i].size());
        next[fields[i].size()] = '\0';
        next += fields[i].size() + 1;
    }
    *out = {};
    out->opaque = parts.opaque;
    out->scheme = strings[0];
    out->username = strings[1];
    out->password = strings[2];
    out->hostname = strings[3];
    out->path = strings[4];
    out->query = strings[5];
    out->fragment = strings[6];
    out->port = parts.port;
    return 0;
}

}
