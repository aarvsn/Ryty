#ifndef CORE_RELINKER_CLI_INCLUDE_CONFIGKEYS_HPP
#define CORE_RELINKER_CLI_INCLUDE_CONFIGKEYS_HPP

// Internal shared helpers for the CLI option parser and the config file
// reader. Not part of the public Cli.hpp surface.

#include <Cli.hpp>
#include <string>

namespace Cli::Internal {

// True for "true", "1", "on", "yes" and the empty value (a bare key means on).
inline bool IsTruth(const std::string& value) {
    return value.empty() || value == "true" || value == "1" || value == "on" || value == "yes";
}

// Applies one config "key = value" pair onto args. Accepts the long CLI flag
// names without leading dashes; underscores and uppercase are normalized.
// Returns false when the key is unknown or the value is invalid.
bool ApplyConfigKey(const std::string& rawKey, const std::string& value, Args& args);

}

#endif
