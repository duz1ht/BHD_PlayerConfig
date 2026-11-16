#pragma once

#include <cstdint>

namespace hud_layout {

// In-game windows retain their complete layout on one centered 4:3 canvas,
// including in mode 3: anchoring individual controls would pull windows apart.
struct OverlayCanvas {
    int width;
    int offsetX;
    float widthRatio;
};
OverlayCanvas GetOverlayCanvas(int mode, int width, int height);

void TransformPoint(int mode, int width, int height, int anchorX, int32_t* x, int32_t* y);

}  // namespace hud_layout
