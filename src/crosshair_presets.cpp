#include "crosshair_presets.h"

#include <array>
#include <cstring>

namespace crosshair_presets {
namespace {
using Pixel = crosshair_tga::Pixel;
using Image = crosshair_tga::Image;

struct PointAlpha { uint8_t x, y, alpha; };

constexpr std::array<const wchar_t*, kStyleCount> kStyles{{
    L"Cross 2px", L"Cross 5px", L"Cross 6px", L"Default",
    L"Dot 1px", L"Dot 2px", L"No-gap Cross 3px", L"T-shaped 4px"
}};
constexpr std::array<const wchar_t*, kColorCount> kColors{{
    L"Green", L"Light Blue", L"Red", L"White", L"Yellow",
    L"Orange", L"Purple", L"Pink", L"Blue", L"Black"
}};

Pixel BaseColor(int style, int color, uint8_t alpha = 255) {
    switch (color) {
        case 0: return {0, 255, 0, alpha};
        case 1: return {0, static_cast<uint8_t>(
                            style == 1 || style == 3 || style == 6 ? 240 : 255),
                        255, alpha};
        case 2: return {255, 0, 0, alpha};
        case 3: return {255, 255, 255, alpha};
        case 4: return {255, 255, 0, alpha};
        case 5: return {255, 128, 0, alpha};
        case 6: return {160, 32, 240, alpha};
        case 7: return {255, 64, 192, alpha};
        case 8: return {0, 96, 255, alpha};
        default: return {0, 0, 0, alpha};
    }
}

void Put(Image& image, int x, int y, Pixel pixel) {
    image[static_cast<size_t>(y * 64 + x)] = pixel;
}

void DrawGappedCross(Image& image, Pixel pixel, int first, int last) {
    for (int coordinate = first; coordinate <= last; ++coordinate) {
        Put(image, 32, coordinate, pixel);
        Put(image, 32, 64 - coordinate, pixel);
        Put(image, coordinate, 32, pixel);
        Put(image, 64 - coordinate, 32, pixel);
    }
}

void DrawDefault(Image& image, int style, int color) {
    constexpr std::array<PointAlpha, 7> arm{{
        {20, 32, 255}, {21, 32, 255}, {22, 32, 127}, {23, 32, 127},
        {24, 32, 92}, {25, 32, 66}, {26, 32, 37}
    }};
    Put(image, 32, 32, BaseColor(style, color));
    for (const auto& point : arm) {
        const Pixel pixel = BaseColor(style, color, point.alpha);
        Put(image, point.x, 32, pixel);
        Put(image, 64 - point.x, 32, pixel);
        Put(image, 32, point.x, pixel);
        Put(image, 32, 64 - point.x, pixel);
    }
}

void DrawDot1(Image& image, int style, int color) {
    constexpr std::array<PointAlpha, 16> points{{
        {31,30,1}, {32,30,26}, {31,31,21}, {32,31,114}, {33,31,17}, {34,31,1},
        {29,32,2}, {30,32,24}, {31,32,121}, {32,32,255}, {33,32,126}, {34,32,24},
        {31,33,21}, {32,33,111}, {33,33,17}, {32,34,22}
    }};
    for (const auto& point : points)
        Put(image, point.x, point.y, BaseColor(style, color, point.alpha));
}

Pixel Dot2Color(int color, int kind, uint8_t alpha) {
    if (color == 2) return {255, 0, 0, alpha};
    if (color == 3) return {255, 255, 255, alpha};
    if (color >= 5) return BaseColor(5, color, alpha);
    if (kind == 2) return BaseColor(5, color, alpha);
    if (color == 0) return kind == 0 ? Pixel{64,255,64,alpha}
                                    : kind == 1 ? Pixel{11,255,11,alpha}
                                                : Pixel{6,255,6,alpha};
    if (color == 1) return kind == 0 ? Pixel{64,255,64,alpha}
                                    : kind == 1 ? Pixel{2,255,204,alpha}
                                                : Pixel{1,255,230,alpha};
    return kind == 0 ? Pixel{64,255,64,alpha}
                     : kind == 1 ? Pixel{197,255,46,alpha}
                                 : Pixel{226,255,26,alpha};
}

void DrawDot2(Image& image, int color) {
    Put(image, 31, 31, Dot2Color(color, 0, 133));
    Put(image, 32, 31, Dot2Color(color, 1, 243));
    Put(image, 33, 31, Dot2Color(color, 0, 133));
    Put(image, 31, 32, Dot2Color(color, 3, 249));
    Put(image, 32, 32, Dot2Color(color, 2, 255));
    Put(image, 33, 32, Dot2Color(color, 1, 243));
    Put(image, 31, 33, Dot2Color(color, 0, 133));
    Put(image, 32, 33, Dot2Color(color, 1, 244));
    Put(image, 33, 33, Dot2Color(color, 0, 133));
}

void DrawNoGap(Image& image, int style, int color) {
    Put(image, 32, 29, BaseColor(style, color, 240));
    Put(image, 32, 30, BaseColor(style, color));
    Put(image, 32, 31, BaseColor(style, color, 240));
    for (int x = 29; x <= 35; ++x)
        Put(image, x, 32, BaseColor(style, color, x == 29 || x == 35 ? 247 : 255));
    Put(image, 32, 33, BaseColor(style, color));
    Put(image, 32, 34, BaseColor(style, color));
    Put(image, 32, 35, BaseColor(style, color, 247));
}

void DrawTShape(Image& image, Pixel pixel) {
    for (int x = 25; x <= 28; ++x) Put(image, x, 32, pixel);
    for (int x = 36; x <= 39; ++x) Put(image, x, 32, pixel);
    for (int y = 36; y <= 39; ++y) Put(image, 32, y, pixel);
}
}  // namespace

const wchar_t* StyleName(int style) {
    return style >= 0 && style < kStyleCount ? kStyles[style] : L"Custom / Current";
}

const wchar_t* ColorName(int color) {
    return color >= 0 && color < kColorCount ? kColors[color] : L"";
}

crosshair_tga::Image Generate(int style, int color) {
    Image image{};
    if (style < 0 || style >= kStyleCount || color < 0 || color >= kColorCount) return image;
    image.fill(BaseColor(style, color, 0));
    const Pixel pixel = BaseColor(style, color);
    switch (style) {
        case 0: DrawGappedCross(image, pixel, 29, 30); break;
        case 1: DrawGappedCross(image, pixel, 25, 29); break;
        case 2: DrawGappedCross(image, pixel, 22, 27); break;
        case 3: DrawDefault(image, style, color); break;
        case 4: DrawDot1(image, style, color); break;
        case 5: DrawDot2(image, color); break;
        case 6: DrawNoGap(image, style, color); break;
        case 7: DrawTShape(image, pixel); break;
    }
    return image;
}

bool FindMatch(const crosshair_tga::Image& image, int& style, int& color) {
    for (int styleIndex = 0; styleIndex < kStyleCount; ++styleIndex) {
        for (int colorIndex = 0; colorIndex < kColorCount; ++colorIndex) {
            const auto candidate = Generate(styleIndex, colorIndex);
            if (std::memcmp(candidate.data(), image.data(), sizeof(image)) == 0) {
                style = styleIndex;
                color = colorIndex;
                return true;
            }
        }
    }
    return false;
}

}  // namespace crosshair_presets
