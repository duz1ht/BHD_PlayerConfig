#include "pff_archive.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace pff_archive {
namespace {
constexpr uint32_t kHeaderSize = 20;
constexpr uint32_t kBhdEntrySize = 36;
constexpr size_t kNameOffset = 16;
constexpr size_t kNameSize = 16;
constexpr size_t kCrcOffset = 32;

uint32_t ReadU32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

void WriteU32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

std::string EntryName(const std::array<uint8_t, kBhdEntrySize>& record) {
    size_t length = 0;
    while (length < kNameSize && record[kNameOffset + length] != 0) ++length;
    return std::string(reinterpret_cast<const char*>(record.data() + kNameOffset), length);
}

bool EqualsIgnoreCase(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) return false;
    }
    return true;
}

uint32_t ComputeCrc(const std::vector<uint8_t>& data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> result{};
        for (uint32_t value = 0; value < 256; ++value) {
            uint32_t reg = value << 24;
            for (int bit = 0; bit < 8; ++bit)
                reg = (reg & 0x80000000u) ? (reg << 1) ^ 0x04C11DB7u : reg << 1;
            result[value] = reg;
        }
        return result;
    }();

    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) crc = (crc << 8) ^ table[(crc >> 24) ^ byte];
    return crc ^ 0xFFFFFFFFu;
}

struct ArchiveInfo {
    std::array<uint8_t, kHeaderSize> header{};
    uint32_t entryCount = 0;
    uint32_t tableOffset = 0;
    uint64_t fileSize = 0;
    std::vector<std::array<uint8_t, kBhdEntrySize>> records;
};

bool ReadArchiveInfo(std::ifstream& stream, ArchiveInfo& info, std::wstring& error) {
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < static_cast<std::streamoff>(kHeaderSize)) {
        error = L"localres.pff is too small to contain a PFF header.";
        return false;
    }
    info.fileSize = static_cast<uint64_t>(length);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(info.header.data()), info.header.size());
    if (!stream) {
        error = L"Could not read the PFF header.";
        return false;
    }

    info.entryCount = ReadU32(info.header.data() + 8);
    const uint32_t entrySize = ReadU32(info.header.data() + 12);
    info.tableOffset = ReadU32(info.header.data() + 16);
    const bool pff3 = std::memcmp(info.header.data() + 4, "PFF3", 4) == 0;
    const bool bhdF4 = info.header[4] == 0x01 && info.header[5] == 0x00 &&
                       info.header[6] == 'F' && info.header[7] == '4';
    if ((!pff3 && !bhdF4) || entrySize != kBhdEntrySize) {
        error = L"localres.pff is not a supported BHD PFF3 archive with 36-byte entries.";
        return false;
    }

    const uint64_t tableEnd = static_cast<uint64_t>(info.tableOffset) +
                              static_cast<uint64_t>(info.entryCount) * kBhdEntrySize;
    if (info.tableOffset < kHeaderSize || tableEnd > info.fileSize) {
        error = L"The PFF entry table is outside the archive bounds.";
        return false;
    }

    info.records.resize(info.entryCount);
    stream.seekg(info.tableOffset);
    for (auto& record : info.records) {
        stream.read(reinterpret_cast<char*>(record.data()), record.size());
        if (!stream) {
            error = L"Could not read the complete PFF entry table.";
            return false;
        }
        const uint64_t dataEnd = static_cast<uint64_t>(ReadU32(record.data() + 4)) +
                                 ReadU32(record.data() + 8);
        if (dataEnd > info.fileSize) {
            error = L"A PFF entry points outside the archive bounds.";
            return false;
        }
    }
    return true;
}

int FindEntry(const ArchiveInfo& info, const std::string& name) {
    for (size_t i = 0; i < info.records.size(); ++i) {
        const auto& record = info.records[i];
        const bool dead = (ReadU32(record.data()) & 1u) != 0 ||
                          EqualsIgnoreCase(EntryName(record), "<DEAD SPACE>");
        if (!dead && EqualsIgnoreCase(EntryName(record), name)) return static_cast<int>(i);
    }
    return -1;
}

bool CopyBytes(std::ifstream& input, std::ofstream& output, uint64_t offset,
               uint64_t size, std::wstring& error) {
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset));
    std::array<char, 64 * 1024> buffer{};
    while (size > 0) {
        const auto amount = static_cast<std::streamsize>(std::min<uint64_t>(size, buffer.size()));
        input.read(buffer.data(), amount);
        if (input.gcount() != amount) {
            error = L"Could not read an entry while rebuilding localres.pff.";
            return false;
        }
        output.write(buffer.data(), amount);
        if (!output) {
            error = L"Could not write the temporary PFF archive.";
            return false;
        }
        size -= static_cast<uint64_t>(amount);
    }
    return true;
}
}  // namespace

bool LoadEntry(const std::wstring& archivePath, const std::string& entryName,
               std::vector<uint8_t>& data, std::wstring& error) {
    std::ifstream input(std::filesystem::path(archivePath), std::ios::binary);
    if (!input) {
        error = L"Could not open localres.pff. Keep it beside BHD Player Config.exe.";
        return false;
    }
    ArchiveInfo info;
    if (!ReadArchiveInfo(input, info, error)) return false;
    const int index = FindEntry(info, entryName);
    if (index < 0) {
        error = L"coma20mm.tga was not found inside localres.pff.";
        return false;
    }
    const auto& record = info.records[static_cast<size_t>(index)];
    const uint32_t offset = ReadU32(record.data() + 4);
    const uint32_t size = ReadU32(record.data() + 8);
    data.resize(size);
    input.seekg(offset);
    input.read(reinterpret_cast<char*>(data.data()), size);
    if (!input && size != 0) {
        error = L"Could not read coma20mm.tga from localres.pff.";
        return false;
    }
    return true;
}

bool ReplaceEntry(const std::wstring& archivePath, const std::string& entryName,
                  const std::vector<uint8_t>& replacement, std::wstring& error) {
    std::ifstream input(std::filesystem::path(archivePath), std::ios::binary);
    if (!input) {
        error = L"Could not open localres.pff. Keep it beside BHD Player Config.exe.";
        return false;
    }
    ArchiveInfo info;
    if (!ReadArchiveInfo(input, info, error)) return false;
    const int targetIndex = FindEntry(info, entryName);
    if (targetIndex < 0) {
        error = L"coma20mm.tga was not found inside localres.pff.";
        return false;
    }

    const std::wstring temporaryPath = archivePath + L".bhd-player-config.tmp";
    DeleteFileW(temporaryPath.c_str());
    std::ofstream output(std::filesystem::path(temporaryPath),
                         std::ios::binary | std::ios::trunc);
    if (!output) {
        error = L"Could not create a temporary archive beside localres.pff.";
        return false;
    }

    output.write(reinterpret_cast<const char*>(info.header.data()), info.header.size());
    uint64_t nextOffset = kHeaderSize;
    for (size_t i = 0; i < info.records.size(); ++i) {
        auto& record = info.records[i];
        if (nextOffset > std::numeric_limits<uint32_t>::max()) {
            error = L"The rebuilt PFF would exceed the supported 4 GB size.";
            output.close();
            DeleteFileW(temporaryPath.c_str());
            return false;
        }
        const uint32_t oldOffset = ReadU32(record.data() + 4);
        const uint32_t oldSize = ReadU32(record.data() + 8);
        WriteU32(record.data() + 4, static_cast<uint32_t>(nextOffset));
        if (static_cast<int>(i) == targetIndex) {
            WriteU32(record.data() + 8, static_cast<uint32_t>(replacement.size()));
            WriteU32(record.data() + 12, static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()));
            WriteU32(record.data() + kCrcOffset, ComputeCrc(replacement));
            output.write(reinterpret_cast<const char*>(replacement.data()),
                         static_cast<std::streamsize>(replacement.size()));
            nextOffset += replacement.size();
        } else {
            if (!CopyBytes(input, output, oldOffset, oldSize, error)) {
                output.close();
                DeleteFileW(temporaryPath.c_str());
                return false;
            }
            nextOffset += oldSize;
        }
    }

    if (nextOffset > std::numeric_limits<uint32_t>::max()) {
        error = L"The rebuilt PFF would exceed the supported 4 GB size.";
        output.close();
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    const uint32_t newTableOffset = static_cast<uint32_t>(nextOffset);
    for (const auto& record : info.records)
        output.write(reinterpret_cast<const char*>(record.data()), record.size());

    const uint64_t oldTableEnd = static_cast<uint64_t>(info.tableOffset) +
                                 static_cast<uint64_t>(info.entryCount) * kBhdEntrySize;
    if (!CopyBytes(input, output, oldTableEnd, info.fileSize - oldTableEnd, error)) {
        output.close();
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    output.seekp(16);
    std::array<uint8_t, 4> offsetBytes{};
    WriteU32(offsetBytes.data(), newTableOffset);
    output.write(reinterpret_cast<const char*>(offsetBytes.data()), offsetBytes.size());
    output.flush();
    output.close();
    input.close();

    std::vector<uint8_t> validation;
    if (!LoadEntry(temporaryPath, entryName, validation, error) || validation != replacement) {
        if (error.empty()) error = L"The temporary PFF failed validation.";
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    if (!MoveFileExW(temporaryPath.c_str(), archivePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = L"Could not replace localres.pff. Close the game and check folder permissions.";
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    return true;
}

}  // namespace pff_archive
