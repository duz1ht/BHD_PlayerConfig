#pragma once

#include <cstddef>

#include "crosshair_tga.h"

namespace crosshair_presets {

constexpr int kStyleCount = 8;
constexpr int kColorCount = 10;

const wchar_t* StyleName(int style);
const wchar_t* ColorName(int color);
crosshair_tga::Image Generate(int style, int color);
bool FindMatch(const crosshair_tga::Image& image, int& style, int& color);

}  // namespace crosshair_presets
