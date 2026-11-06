#ifndef IO_ZIPWRITER_HPP
#define IO_ZIPWRITER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace Io {

// 2.0.0: minimal ZIP (store, no compression) writer used to package iOS .ipa
// archives. Produces spec-conformant archives readable by every unzip tool
// and by Python's zipfile module. UTF-8 names, no encryption, no ZIP64.
class ZipWriter {
public:
    void AddFile(const std::string& name, const std::vector<std::uint8_t>& bytes);
    void AddFile(const std::string& name, const std::string& bytes);

    // Finishes the central directory and returns the archive bytes. The
    // writer must not be used afterwards.
    [[nodiscard]] std::vector<std::uint8_t> Finish();

private:
    struct Entry {
        std::string name;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t localHeaderOffset = 0;
    };

    std::vector<std::uint8_t> archive_;
    std::vector<Entry> entries_;
};

}

#endif
