#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "pixel_perfect_crosshair_math.h"

namespace {
void Check(bool condition) {
    if (!condition) std::abort();
}
}  // namespace

int main() {
    int32_t left = 900;
    int32_t top = 495;
    int32_t right = 1020;
    int32_t bottom = 585;
    pixel_perfect_crosshair::RestoreNativeBounds(
        &left, &top, &right, &bottom, 64, 64);
    Check(left == 928 && top == 508 && right == 992 && bottom == 572);
    Check(right - left == 64 && bottom - top == 64);

    left = 480;
    top = 352;
    right = 544;
    bottom = 416;
    pixel_perfect_crosshair::RestoreNativeBounds(
        &left, &top, &right, &bottom, 64, 64);
    Check(left == 480 && top == 352 && right == 544 && bottom == 416);

    pixel_perfect_crosshair::RestoreNativeBounds(
        &left, &top, &right, &bottom, 0, 64);
    Check(left == 480 && top == 352 && right == 544 && bottom == 416);

    uint8_t branch[5] = {};
    constexpr uintptr_t instruction = 0x00502942;
    constexpr uintptr_t target = 0x03590000;
    Check(pixel_perfect_crosshair::EncodeRelativeBranch(
        branch, instruction, target, 0xE9));
    int32_t displacement = 0;
    std::memcpy(&displacement, branch + 1, sizeof(displacement));
    Check(branch[0] == 0xE9);
    Check(instruction + 5 + displacement == target);
    return 0;
}
