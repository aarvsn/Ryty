#ifndef IO_JSON_HPP
#define IO_JSON_HPP

#include <string>
#include <string_view>

namespace Io {

inline std::string EscapeJsonString(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                result += "\\u00";
                result += digits[character >> 4];
                result += digits[character & 0x0f];
            } else {
                result += static_cast<char>(character);
            }
        }
    }
    return result;
}

}

#endif
