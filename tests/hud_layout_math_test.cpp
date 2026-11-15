#include <cstdint>
#include <cstdlib>

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
    return 0;
}
