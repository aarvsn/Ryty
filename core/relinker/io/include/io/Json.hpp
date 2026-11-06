#ifndef IO_JSON_HPP
#define IO_JSON_HPP

#include <string>
#include <string_view>

namespace Io {

inline std::string EscapeJsonString(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    const size_t len = value.size();
    size_t i = 0;

    while (i < len) {
        const unsigned char c0 = static_cast<unsigned char>(value[i]);

        // Check for valid multi-byte UTF-8 sequences
        size_t utf8_len = 0;
        if (c0 >= 0xC2 && c0 <= 0xDF && i + 1 < len) {
            const unsigned char c1 = static_cast<unsigned char>(value[i + 1]);
            if (c1 >= 0x80 && c1 <= 0xBF) utf8_len = 2;
        } else if (i + 2 < len &&
                   ((c0 == 0xE0 && static_cast<unsigned char>(value[i + 1]) >= 0xA0 && static_cast<unsigned char>(value[i + 1]) <= 0xBF && static_cast<unsigned char>(value[i + 2]) >= 0x80 && static_cast<unsigned char>(value[i + 2]) <= 0xBF) ||
                    (c0 >= 0xE1 && c0 <= 0xEC && static_cast<unsigned char>(value[i + 1]) >= 0x80 && static_cast<unsigned char>(value[i + 1]) <= 0xBF && static_cast<unsigned char>(value[i + 2]) >= 0x80 && static_cast<unsigned char>(value[i + 2]) <= 0xBF) ||
                    (c0 == 0xED && static_cast<unsigned char>(value[i + 1]) >= 0x80 && static_cast<unsigned char>(value[i + 1]) <= 0x9F && static_cast<unsigned char>(value[i + 2]) >= 0x80 && static_cast<unsigned char>(value[i + 2]) <= 0xBF) ||
                    (c0 >= 0xEE && c0 <= 0xEF && static_cast<unsigned char>(value[i + 1]) >= 0x80 && static_cast<unsigned char>(value[i + 1]) <= 0xBF && static_cast<unsigned char>(value[i + 2]) >= 0x80 && static_cast<unsigned char>(value[i + 2]) <= 0xBF))) {
            utf8_len = 3;
        } else if (i + 3 < len &&
                   ((c0 == 0xF0 && static_cast<unsigned char>(value[i + 1]) >= 0x90 && static_cast<unsigned char>(value[i + 1]) <= 0xBF && static_cast<unsigned char>(value[i + 2]) >= 0x80 && static_cast<unsigned char>(value[i + 2]) <= 0xBF && static_cast<unsigned char>(value[i + 3]) >= 0x80 && static_cast<unsigned char>(value[i + 3]) <= 0xBF) ||
                    (c0 >= 0xF1 && c0 <= 0xF3 && static_cast<unsigned char>(value[i + 1]) >= 0x80 && static_cast<unsigned char>(value[i + 1]) <= 0xBF && static_cast<unsigned char>(value[i + 2]) >= 0x80 && static_cast<unsigned char>(value[i + 2]) <= 0xBF && static_cast<unsigned char>(value[i + 3]) >= 0x80 && static_cast<unsigned char>(value[i + 3]) <= 0xBF) ||
                    (c0 == 0xF4 && static_cast<unsigned char>(value[i + 1]) >= 0x80 && static_cast<unsigned char>(value[i + 1]) <= 0x8F && static_cast<unsigned char>(value[i + 2]) >= 0x80 && static_cast<unsigned char>(value[i + 2]) <= 0xBF && static_cast<unsigned char>(value[i + 3]) >= 0x80 && static_cast<unsigned char>(value[i + 3]) <= 0xBF))) {
            utf8_len = 4;
        }

        if (utf8_len > 0) {
            for (size_t k = 0; k < utf8_len; ++k) {
                result += value[i + k];
            }
            i += utf8_len;
            continue;
        }

        switch (c0) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c0 < 0x20 || c0 >= 0x7f) {
                result += "\\u00";
                result += digits[c0 >> 4];
                result += digits[c0 & 0x0f];
            } else {
                result += static_cast<char>(c0);
            }
        }
        ++i;
    }
    return result;
}

}

#endif
