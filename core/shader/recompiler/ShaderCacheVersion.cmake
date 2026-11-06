file(STRINGS "${LIST_FILE}" sources)
list(SORT sources)
set(manifest "${EXTRA}\n")
set(switches)
foreach(source IN LISTS sources)
    if(NOT EXISTS "${source}")
        continue()
    endif()
    file(SHA256 "${source}" digest)
    file(RELATIVE_PATH relative "${ROOT}" "${source}")
    string(APPEND manifest "${relative} ${digest}\n")
    string(FIND "${source}" "${RECOMPILER_DIR}/" inRecompiler)
    if(inRecompiler EQUAL 0 AND source MATCHES "\\.(cpp|hpp|h)$")
        file(STRINGS "${source}" lines REGEX "\"APS5_[A-Z0-9_]+\"")
        foreach(line IN LISTS lines)
            string(REGEX MATCHALL "\"APS5_[A-Z0-9_]+\"" names "${line}")
            list(APPEND switches ${names})
        endforeach()
    endif()
endforeach()
string(SHA256 version "${manifest}")
string(SUBSTRING "${version}" 0 16 version)
list(REMOVE_DUPLICATES switches)
list(SORT switches)
list(JOIN switches ",\n    " switchList)

set(content "#ifndef CORE_SHADER_RECOMPILER_SHADERCACHEVERSION_HPP
#define CORE_SHADER_RECOMPILER_SHADERCACHEVERSION_HPP

#include <cstdint>
#include <string_view>

namespace ShaderRecompiler::ShaderDiskCache::Generated {

inline constexpr std::uint64_t SourceVersion = 0x${version}ull;

inline constexpr std::string_view RecompilerSwitches[] = {
    ${switchList}
};

}

#endif
")

if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" previous)
    if(previous STREQUAL content)
        return()
    endif()
endif()
file(WRITE "${OUTPUT}" "${content}")
