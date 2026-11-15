#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace crosshair_tga {

struct Pixel {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t alpha = 0;
};

using Image = std::array<Pixel, 64 * 64>;

std::vector<uint8_t> Encode(const Image& image);
bool Decode(const std::vector<uint8_t>& bytes, Image& image, std::wstring& error);

}  // namespace crosshair_tga
