#include <Cli.hpp>
#include <ConfigKeys.hpp>
#include <io/FileReader.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace Cli {

// Applies an INI-style options file onto args (before the command line, so
// explicit CLI flags always win). Format:
//
//   # comment
//   to-intel = true
//   unused-filter = 1
//   rpath = $ORIGIN/libs
//   target = windows
//
// Lines without '=' set a bare flag ("verbose"). Sections ([batch]) are
// accepted and ignored. Unknown keys or invalid values throw, so a typo in
// the file never silently changes a port.
std::vector<std::string> ApplyConfigFile(const std::string& configPath, Args& args) {
    if (configPath.empty()) throw std::runtime_error("empty config file path");

    Io::FileReader reader;
    const auto bytes = reader.Read(configPath);
    std::string text;
    text.resize(bytes.size());
    std::transform(bytes.begin(), bytes.end(), text.begin(), [](const std::uint8_t value) { return static_cast<char>(value); });

    std::vector<std::string> applied;
    std::size_t lineNumber = 0;
    std::size_t position = 0;
    while (position <= text.size()) {
        std::size_t end = text.find('\n', position);
        if (end == std::string::npos) end = text.size();
        ++lineNumber;
        std::string line = text.substr(position, end - position);
        position = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // strip comment
        const auto comment = line.find_first_of("#;");
        if (comment != std::string::npos) line = line.substr(0, comment);
        // trim
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        const auto last = line.find_last_not_of(" \t");
        line = line.substr(first, last - first + 1);
        if (line.empty()) continue;
        if (line.front() == '[') continue; // section marker, ignored

        std::string key = line;
        std::string value;
        const auto equals = line.find('=');
        if (equals != std::string::npos) {
            key = line.substr(0, equals);
            value = line.substr(equals + 1);
            const auto valueFirst = value.find_first_not_of(" \t\"");
            if (valueFirst == std::string::npos) value.clear();
            else {
                const auto valueLast = value.find_last_not_of(" \t\"");
                value = value.substr(valueFirst, valueLast - valueFirst + 1);
            }
        }
        const auto keyLast = key.find_last_not_of(" \t");
        key = key.substr(0, keyLast + 1);

        if (!Internal::ApplyConfigKey(key, value, args))
            throw std::runtime_error("config file " + configPath + " line " + std::to_string(lineNumber) + ": unknown option or invalid value: " + key + (value.empty() ? "" : " = " + value));
        applied.push_back(key);
        if (position > text.size()) break;
    }

    return applied;
}

}
