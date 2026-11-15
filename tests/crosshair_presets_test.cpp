#include <array>
#include <cstdint>
#include <cstdio>

#include "crosshair_presets.h"

namespace {
uint64_t Hash(const crosshair_tga::Image& image) {
    static_assert(sizeof(crosshair_tga::Pixel) == 4, "Pixel must remain packed RGBA");
    uint64_t hash = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const uint8_t*>(image.data());
    for (size_t i = 0; i < sizeof(image); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}
}  // namespace

int main() {
    constexpr std::array<uint64_t, 40> expected{{
        0x0D422E36993ABF25ull, 0x4B019012A7BA8E3Dull, 0xFDCC216E6B78A965ull,
        0x4F4989EF88E016A5ull, 0x2B398B20C0F1862Dull,
        0xFDCF3E25BCFB9D25ull, 0xAA4648C977286B25ull, 0xE994C077118F09E5ull,
        0x0A7711CEF82FE9A5ull, 0x4429781C739341FDull,
        0xC4CC11D05E5E0725ull, 0x940804AACA12D06Dull, 0x1AB2B34940CBDEE5ull,
        0x58C1ABEE3F6947A5ull, 0x8B4AF4E702AF243Dull,
        0x02ED8F6056544BA8ull, 0xF52CEDD791B5B0FCull, 0xE8990D7E712A29CCull,
        0x389DBE73A9A27680ull, 0x671AE5055AEF180Cull,
        0xEDDD8A3B6E4DB9E4ull, 0x4ACD26AD3CCD8950ull, 0x9B9CB834F82CBBA0ull,
        0xC2EF4A7F47861D4Cull, 0xED6301F39250D338ull,
        0xB88630697F146D6Full, 0x45DD2977FE8ED33Aull, 0x0202793C3021F8BFull,
        0xFAED048C5E61252Full, 0x523E385D55B06D04ull,
        0x8CDBEBE6D639B900ull, 0xC9035502D05E4884ull, 0xC0E7514EA72B3844ull,
        0xC6FD8CADE20D2FC8ull, 0x039FBCA8486B37E4ull,
        0xEAEAA74AB8503725ull, 0x5C5FDB268B06A355ull, 0xEF756AC590807CA5ull,
        0xB0D3F624FEF06025ull, 0x5B0604B242532F35ull
    }};

    size_t index = 0;
    for (int style = 0; style < crosshair_presets::kStyleCount; ++style) {
        for (int color = 0; color < 5; ++color, ++index) {
            const auto image = crosshair_presets::Generate(style, color);
            if (Hash(image) != expected[index]) {
                std::fprintf(stderr, "Preset mismatch at style %d, color %d: %016llX != %016llX\n",
                             style, color,
                             static_cast<unsigned long long>(Hash(image)),
                             static_cast<unsigned long long>(expected[index]));
                return 1;
            }
        }
    }

    for (int style = 0; style < crosshair_presets::kStyleCount; ++style) {
        for (int color = 0; color < crosshair_presets::kColorCount; ++color) {
            const auto image = crosshair_presets::Generate(style, color);
            size_t visiblePixels = 0;
            for (const auto& pixel : image) if (pixel.alpha != 0) ++visiblePixels;
            if (visiblePixels == 0) return 2;
            int matchedStyle = -1;
            int matchedColor = -1;
            if (!crosshair_presets::FindMatch(image, matchedStyle, matchedColor) ||
                matchedStyle != style || matchedColor != color) return 3;
        }
    }
    return 0;
}
