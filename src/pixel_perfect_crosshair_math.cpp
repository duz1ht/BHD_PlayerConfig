#include "pixel_perfect_crosshair_math.h"

#include <cstring>
#include <limits>

namespace pixel_perfect_crosshair {

void RestoreNativeBounds(int32_t* left, int32_t* top, int32_t* right,
                         int32_t* bottom, int32_t width, int32_t height) {
    if (left == nullptr || top == nullptr || right == nullptr || bottom == nullptr ||
        width <= 0 || height <= 0 || width > 1024 || height > 768) {
        return;
    }

    const int32_t centerX = *left + (*right - *left) / 2;
    const int32_t centerY = *top + (*bottom - *top) / 2;
    *left = centerX - width / 2;
    *top = centerY - height / 2;
    *right = *left + width;
    *bottom = *top + height;
}

bool EncodeRelativeBranch(uint8_t output[5], uintptr_t instructionAddress,
                          uintptr_t targetAddress, uint8_t opcode) {
    if (output == nullptr) return false;
    const intptr_t displacement = static_cast<intptr_t>(targetAddress) -
        static_cast<intptr_t>(instructionAddress + 5);
    if (displacement < std::numeric_limits<int32_t>::min() ||
        displacement > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    output[0] = opcode;
    const int32_t relative = static_cast<int32_t>(displacement);
    std::memcpy(output + 1, &relative, sizeof(relative));
    return true;
}

}  // namespace pixel_perfect_crosshair
