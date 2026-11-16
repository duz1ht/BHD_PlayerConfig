#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <initializer_list>
#include <utility>

#include "hud_layout_math.h"

namespace {
void Check(bool value) { if (!value) std::abort(); }
void Point(int mode, int x, int y, int expectedX, int expectedY) {
    int32_t actualX = x, actualY = y;
    hud_layout::TransformPoint(mode, 1920, 1080, x, &actualX, &actualY);
    Check(actualX == expectedX && actualY == expectedY);
}
}  // namespace

int main() {
    Point(2, 0, 0, 240, 0);
    Point(2, 512, 384, 960, 540);
    Point(2, 1024, 768, 1680, 1080);
    Point(3, 0, 0, 0, 0);
    Point(3, 512, 384, 960, 540);
    Point(3, 1024, 768, 1920, 1080);
    Point(3, 100, 100, 188, 141);
    Point(3, 164, 164, 308, 231);
    int32_t left = 320, top = 600, right = 448, bottom = 728;
    hud_layout::TransformPoint(3, 1920, 1080, 384, &left, &top);
    hud_layout::TransformPoint(3, 1920, 1080, 384, &right, &bottom);
    Check(left == 630 && right == 810 && right - left == bottom - top);
    for (int mode : {2, 3}) {
        const auto canvas = hud_layout::GetOverlayCanvas(mode, 1920, 1080);
        Check(canvas.width == 1440 && canvas.offsetX == 240 && canvas.widthRatio == .75f);
        const auto wide = hud_layout::GetOverlayCanvas(mode, 3440, 1440);
        Check(wide.width == 1920 && wide.offsetX == 760);
        // Physical click -> virtual pixels -> logical 800x600 -> rendered pixels.
        for (int x : {0, 100, 400, 700, 800}) {
            const float rendered = canvas.offsetX + x * (canvas.width / 800.0f);
            const float logical = (rendered - canvas.offsetX) / (canvas.width / 800.0f);
            Check(std::fabs(logical - x) < .001f);
        }
    }
    for (int mode : {0, 1, -1, 4}) {
        const auto native = hud_layout::GetOverlayCanvas(mode, 1920, 1080);
        Check(native.width == 1920 && native.offsetX == 0 && native.widthRatio == 1);
    }
    for (const auto dimensions : {std::pair<int,int>{1024,768}, {1280,1024}, {0,1080}, {1920,0}}) {
        const auto native = hud_layout::GetOverlayCanvas(2, dimensions.first, dimensions.second);
        Check(native.width == dimensions.first && native.offsetX == 0 && native.widthRatio == 1);
    }
    return 0;
}
