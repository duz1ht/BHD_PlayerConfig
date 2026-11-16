#include "hud_layout_math.h"

namespace hud_layout {

OverlayCanvas GetOverlayCanvas(int mode, int width, int height) {
    if (mode < 2 || mode > 3 || width <= 0 || height <= 0 ||
        static_cast<int64_t>(width) * 3 <= static_cast<int64_t>(height) * 4)
        return {width, 0, 1.0f};
    const int canvasWidth = static_cast<int>((static_cast<int64_t>(height) * 4 + 1) / 3);
    return {canvasWidth, (width - canvasWidth) / 2,
            static_cast<float>(canvasWidth) / width};
}

void TransformPoint(int mode, int width, int height, int anchorX, int32_t* x, int32_t* y) {
    if (mode < 2 || mode > 3 || width <= 0 || height <= 0 || x == nullptr || y == nullptr ||
        static_cast<int64_t>(width) * 3 <= static_cast<int64_t>(height) * 4) return;

    const int hudWidth = static_cast<int>((static_cast<int64_t>(height) * 1024 + 384) / 768);
    const int margin = width - hudWidth;
    const int sourceX = *x;
    const int offset = mode == 2 ? margin / 2
        : static_cast<int>((static_cast<int64_t>(margin) * anchorX + 512) / 1024);
    *x = offset + static_cast<int>((static_cast<int64_t>(height) * sourceX + 384) / 768);
    *y = static_cast<int>((static_cast<int64_t>(height) * *y + 384) / 768);
}

}  // namespace hud_layout
