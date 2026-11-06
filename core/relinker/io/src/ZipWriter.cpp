#include <io/ZipWriter.hpp>
#include <cstring>

namespace Io {

namespace {

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size) {
    static std::uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 1) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
            }
            table[i] = crc;
        }
        initialized = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void Append16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void Append32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

}

void ZipWriter::AddFile(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    Entry entry;
    entry.name = name;
    entry.crc = Crc32(bytes.data(), bytes.size());
    entry.size = static_cast<std::uint32_t>(bytes.size());
    entry.localHeaderOffset = static_cast<std::uint32_t>(archive_.size());

    // Local file header.
    Append32(archive_, 0x04034b50);
    Append16(archive_, 20);      // version needed
    Append16(archive_, 0x0800);  // flags: UTF-8 names
    Append16(archive_, 0);       // method: stored
    Append16(archive_, 0);       // mod time
    Append16(archive_, 0x21);    // mod date (1980-01-01)
    Append32(archive_, entry.crc);
    Append32(archive_, entry.size);
    Append32(archive_, entry.size);
    Append16(archive_, static_cast<std::uint16_t>(name.size()));
    Append16(archive_, 0);       // extra length
    archive_.insert(archive_.end(), name.begin(), name.end());
    archive_.insert(archive_.end(), bytes.begin(), bytes.end());
    entries_.push_back(entry);
}

void ZipWriter::AddFile(const std::string& name, const std::string& bytes) {
    AddFile(name, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
}

std::vector<std::uint8_t> ZipWriter::Finish() {
    const std::uint32_t centralDirectoryOffset = static_cast<std::uint32_t>(archive_.size());
    for (const Entry& entry : entries_) {
        Append32(archive_, 0x02014b50);
        Append16(archive_, 20);      // version made by
        Append16(archive_, 20);      // version needed
        Append16(archive_, 0x0800);  // flags
        Append16(archive_, 0);       // method
        Append16(archive_, 0);       // mod time
        Append16(archive_, 0x21);    // mod date
        Append32(archive_, entry.crc);
        Append32(archive_, entry.size);
        Append32(archive_, entry.size);
        Append16(archive_, static_cast<std::uint16_t>(entry.name.size()));
        Append16(archive_, 0);       // extra
        Append16(archive_, 0);       // comment
        Append16(archive_, 0);       // disk number
        Append16(archive_, 0);       // internal attrs
        Append32(archive_, 0);       // external attrs
        Append32(archive_, entry.localHeaderOffset);
        archive_.insert(archive_.end(), entry.name.begin(), entry.name.end());
    }
    const std::uint32_t centralDirectorySize = static_cast<std::uint32_t>(archive_.size()) - centralDirectoryOffset;

    // End of central directory.
    Append32(archive_, 0x06054b50);
    Append16(archive_, 0);       // disk number
    Append16(archive_, 0);       // disk with central directory
    Append16(archive_, static_cast<std::uint16_t>(entries_.size()));
    Append16(archive_, static_cast<std::uint16_t>(entries_.size()));
    Append32(archive_, centralDirectorySize);
    Append32(archive_, centralDirectoryOffset);
    Append16(archive_, 0);       // comment length

    std::vector<std::uint8_t> finished;
    finished.swap(archive_);
    entries_.clear();
    return finished;
}

}
