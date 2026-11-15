#include "pixel_perfect_crosshair.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pixel_perfect_crosshair_math.h"

namespace pixel_perfect_crosshair {
namespace {
constexpr uintptr_t kCrosshairDrawRva = 0x00102840;
constexpr uintptr_t kScaledBoundsRva = 0x00102942;
constexpr size_t kHookLength = 5;

const unsigned char kCrosshairDrawSignature[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00,
};
const unsigned char kScaledBoundsSignature[kHookLength] = {
    0xDB, 0x45, 0xF8,  // fild dword ptr [ebp-8]
    0x33, 0xC0,        // xor eax,eax
};

bool g_installed = false;

extern "C" void __cdecl RestoreCrosshairBounds(int32_t* frame) {
    if (frame == nullptr) return;
    // At VA 0x00502942 the native routine has already converted both corners
    // from its 1024x768 HUD space to screen pixels. Preserve their midpoint,
    // then restore the texture width/height supplied to the routine.
    RestoreNativeBounds(&frame[-2], &frame[-1], &frame[3], &frame[2],
                        frame[4], frame[5]);
}

bool AppendRelativeBranch(unsigned char*& output, const void* target,
                          unsigned char opcode) {
    if (!EncodeRelativeBranch(output, reinterpret_cast<uintptr_t>(output),
                              reinterpret_cast<uintptr_t>(target), opcode)) {
        return false;
    }
    output += 5;
    return true;
}

bool InstallHook(unsigned char* hook) {
    auto* stub = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (stub == nullptr) return false;

    unsigned char* output = stub;
    *output++ = 0x9C;  // pushfd
    *output++ = 0x60;  // pushad
    *output++ = 0x55;  // push ebp: frame argument
    if (!AppendRelativeBranch(output,
                              reinterpret_cast<const void*>(&RestoreCrosshairBounds), 0xE8)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    const unsigned char restoreStack[] = {0x83, 0xC4, 0x04, 0x61, 0x9D};
    std::memcpy(output, restoreStack, sizeof(restoreStack));
    output += sizeof(restoreStack);
    std::memcpy(output, kScaledBoundsSignature, sizeof(kScaledBoundsSignature));
    output += sizeof(kScaledBoundsSignature);
    if (!AppendRelativeBranch(output, hook + kHookLength, 0xE9)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldStubProtection = 0;
    if (!VirtualProtect(stub, 32, PAGE_EXECUTE_READ, &oldStubProtection)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }

    unsigned char patch[kHookLength] = {};
    if (!EncodeRelativeBranch(patch, reinterpret_cast<uintptr_t>(hook),
                              reinterpret_cast<uintptr_t>(stub), 0xE9)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    DWORD oldProtection = 0;
    if (!VirtualProtect(hook, kHookLength, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(hook, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), hook, sizeof(patch));
    DWORD ignored = 0;
    VirtualProtect(hook, kHookLength, oldProtection, &ignored);
    return true;
}
}  // namespace

bool Install(bool enabled) {
    if (!enabled) return false;
    if (g_installed) return true;
    static_assert(sizeof(void*) == 4,
                  "PixelPerfectCrosshair requires the 32-bit game build");

    const uintptr_t moduleBase =
        reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* draw = reinterpret_cast<const unsigned char*>(moduleBase + kCrosshairDrawRva);
    auto* hook = reinterpret_cast<unsigned char*>(moduleBase + kScaledBoundsRva);
    if (std::memcmp(draw, kCrosshairDrawSignature,
                    sizeof(kCrosshairDrawSignature)) != 0 ||
        std::memcmp(hook, kScaledBoundsSignature,
                    sizeof(kScaledBoundsSignature)) != 0) {
        return false;
    }
    if (!InstallHook(hook)) return false;
    g_installed = true;
    return true;
}

}  // namespace pixel_perfect_crosshair
