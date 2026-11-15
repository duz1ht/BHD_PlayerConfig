#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "crosshair_tga.h"
#include "pff_archive.h"

#define CHECK(condition) \
    do { if (!(condition)) { std::fwprintf(stderr, L"Check failed at line %d: %hs\n", \
                                            __LINE__, #condition); return 1; } } while (false)

namespace {
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

uint32_t Crc(const std::vector<uint8_t>& data) {
    std::array<uint32_t, 256> table{};
    for (uint32_t value = 0; value < 256; ++value) {
        uint32_t reg = value << 24;
        for (int bit = 0; bit < 8; ++bit)
            reg = (reg & 0x80000000u) ? (reg << 1) ^ 0x04C11DB7u : reg << 1;
        table[value] = reg;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t value : data) crc = (crc << 8) ^ table[(crc >> 24) ^ value];
    return crc ^ 0xFFFFFFFFu;
}

std::array<uint8_t, 36> Record(const char* name, uint32_t offset,
                               const std::vector<uint8_t>& data, uint32_t timestamp) {
    std::array<uint8_t, 36> record{};
    WriteU32(record.data() + 4, offset);
    WriteU32(record.data() + 8, static_cast<uint32_t>(data.size()));
    WriteU32(record.data() + 12, timestamp);
    std::memcpy(record.data() + 16, name, std::min<size_t>(std::strlen(name), 15));
    WriteU32(record.data() + 32, Crc(data));
    return record;
}

void CreateFixture(const std::filesystem::path& path,
                   const std::vector<uint8_t>& originalCrosshair,
                   const std::vector<uint8_t>& otherData) {
    const uint32_t firstOffset = 20;
    const uint32_t secondOffset = firstOffset + static_cast<uint32_t>(otherData.size());
    const uint32_t tableOffset = secondOffset + static_cast<uint32_t>(originalCrosshair.size());
    std::array<uint8_t, 20> header{};
    WriteU32(header.data(), 20);
    header[4] = 0x01; header[5] = 0x00; header[6] = 'F'; header[7] = '4';
    WriteU32(header.data() + 8, 2);
    WriteU32(header.data() + 12, 36);
    WriteU32(header.data() + 16, tableOffset);
    const auto first = Record("other.bin", firstOffset, otherData, 10);
    const auto second = Record("coma20mm.tga", secondOffset, originalCrosshair, 20);
    const std::array<uint8_t, 12> footer{{1, 2, 3, 4, 0, 0, 0, 0, 'K', 'I', 'N', 'G'}};

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(header.data()), header.size());
    output.write(reinterpret_cast<const char*>(otherData.data()), otherData.size());
    output.write(reinterpret_cast<const char*>(originalCrosshair.data()), originalCrosshair.size());
    output.write(reinterpret_cast<const char*>(first.data()), first.size());
    output.write(reinterpret_cast<const char*>(second.data()), second.size());
    output.write(reinterpret_cast<const char*>(footer.data()), footer.size());
}
}  // namespace

int main() {
    const auto path = std::filesystem::current_path() / "pff_archive_test_fixture.pff";
    std::filesystem::remove(path);

    crosshair_tga::Image originalImage{};
    originalImage[32 * 64 + 32] = {0, 255, 0, 255};
    const auto originalTga = crosshair_tga::Encode(originalImage);
    const std::vector<uint8_t> otherData{1, 3, 5, 7, 9};
    CreateFixture(path, originalTga, otherData);

    std::wstring error;
    std::vector<uint8_t> loaded;
    CHECK(pff_archive::LoadEntry(path.wstring(), "COMA20MM.TGA", loaded, error));
    CHECK(loaded == originalTga);

    crosshair_tga::Image replacementImage{};
    replacementImage[31 * 64 + 31] = {255, 32, 16, 200};
    replacementImage[32 * 64 + 32] = {10, 20, 30, 40};
    const auto replacementTga = crosshair_tga::Encode(replacementImage);
    CHECK(pff_archive::ReplaceEntry(path.wstring(), "coma20mm.tga", replacementTga, error));
    CHECK(pff_archive::LoadEntry(path.wstring(), "coma20mm.tga", loaded, error));
    CHECK(loaded == replacementTga);

    crosshair_tga::Image decoded{};
    CHECK(crosshair_tga::Decode(loaded, decoded, error));
    CHECK(decoded[31 * 64 + 31].red == 255);
    CHECK(decoded[31 * 64 + 31].alpha == 200);
    CHECK(decoded[32 * 64 + 32].blue == 30);

    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> archive((std::istreambuf_iterator<char>(input)), {});
    CHECK(archive.size() > 100);
    CHECK(archive[4] == 0x01 && archive[6] == 'F' && archive[7] == '4');
    const uint32_t tableOffset = ReadU32(archive.data() + 16);
    CHECK(tableOffset == 20 + otherData.size() + replacementTga.size());
    CHECK(std::equal(otherData.begin(), otherData.end(), archive.begin() + 20));
    const uint8_t* secondRecord = archive.data() + tableOffset + 36;
    CHECK(ReadU32(secondRecord + 4) == 20 + otherData.size());
    CHECK(ReadU32(secondRecord + 8) == replacementTga.size());
    CHECK(ReadU32(secondRecord + 32) == Crc(replacementTga));
    CHECK(std::memcmp(archive.data() + archive.size() - 4, "KING", 4) == 0);

    input.close();
    std::filesystem::remove(path);
    return 0;
}
