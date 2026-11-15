#pragma once

#include <cstdint>

namespace pixel_perfect_crosshair {

void RestoreNativeBounds(int32_t* left, int32_t* top, int32_t* right,
                         int32_t* bottom, int32_t width, int32_t height);
bool EncodeRelativeBranch(uint8_t output[5], uintptr_t instructionAddress,
                          uintptr_t targetAddress, uint8_t opcode);

}  // namespace pixel_perfect_crosshair
