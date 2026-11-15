#include "crosshair_tga.h"

#include <algorithm>
#include <cstring>

namespace crosshair_tga {

std::vector<uint8_t> Encode(const Image& image) {
    constexpr size_t pixelBytes = 64 * 64 * 4;
    std::vector<uint8_t> output(18 + pixelBytes + 26, 0);
    output[2] = 2;
    output[12] = 64;
    output[14] = 64;
    output[16] = 32;
    output[17] = 0x08;
    for (int y = 0; y < 64; ++y) {
        const int destinationY = 63 - y;
        for (int x = 0; x < 64; ++x) {
            const auto& pixel = image[static_cast<size_t>(y * 64 + x)];
            const size_t destination = 18 + static_cast<size_t>(destinationY * 64 + x) * 4;
            output[destination] = pixel.blue;
            output[destination + 1] = pixel.green;
            output[destination + 2] = pixel.red;
            output[destination + 3] = pixel.alpha;
        }
    }
    constexpr char signature[] = "TRUEVISION-XFILE.";
    std::memcpy(output.data() + 18 + pixelBytes + 8, signature, sizeof(signature) - 1);
    return output;
}

bool Decode(const std::vector<uint8_t>& bytes, Image& image, std::wstring& error) {
    if (bytes.size() < 18) {
        error = L"coma20mm.tga is too small.";
        return false;
    }
    const uint8_t idLength = bytes[0];
    if (bytes[1] != 0 || bytes[2] != 2) {
        error = L"Only uncompressed true-color TGA files are supported.";
        return false;
    }
    const uint16_t width = static_cast<uint16_t>(bytes[12] | (bytes[13] << 8));
    const uint16_t height = static_cast<uint16_t>(bytes[14] | (bytes[15] << 8));
    if (width != 64 || height != 64 || bytes[16] != 32) {
        error = L"coma20mm.tga must be a 64x64, 32-bit TGA image.";
        return false;
    }
    const size_t pixelOffset = 18 + idLength;
    if (pixelOffset + 64 * 64 * 4 > bytes.size()) {
        error = L"coma20mm.tga is truncated.";
        return false;
    }
    const bool topOrigin = (bytes[17] & 0x20) != 0;
    for (int y = 0; y < 64; ++y) {
        const int sourceY = topOrigin ? y : 63 - y;
        for (int x = 0; x < 64; ++x) {
            const size_t source = pixelOffset + static_cast<size_t>(sourceY * 64 + x) * 4;
            auto& pixel = image[static_cast<size_t>(y * 64 + x)];
            pixel.blue = bytes[source];
            pixel.green = bytes[source + 1];
            pixel.red = bytes[source + 2];
            pixel.alpha = bytes[source + 3];
        }
    }
    return true;
}

}  // namespace crosshair_tga
