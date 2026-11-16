#include "hud_layout.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "hud_layout_math.h"
#include "hud_overlays.h"
#include "pixel_perfect_crosshair.h"
#include "pixel_perfect_crosshair_math.h"

namespace hud_layout {
namespace {
constexpr uintptr_t kHudPassRva = 0x00103230;
constexpr uintptr_t kDrawTextureRva = 0x00102A60;
constexpr uintptr_t kScalePointRva = 0x0012CED0;
constexpr uintptr_t kWidthRva = 0x005F72C0;
constexpr uintptr_t kHeightRva = 0x005F72C4;
constexpr size_t kHudHookLength = 6;
constexpr size_t kDrawTextureHookLength = 9;
constexpr size_t kScaleHookLength = 6;

const uint8_t kHudSignature[kHudHookLength] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44};
const uint8_t kDrawTextureSignature[kDrawTextureHookLength] =
    {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA4, 0x00, 0x00, 0x00};
const uint8_t kScaleSignature[kScaleHookLength] = {0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x08};

using HudPassFn = void(__cdecl*)(uint32_t);
using DrawTextureFn = void(__cdecl*)(int, int, int, int, int, int, uint32_t, uint32_t);
using ScalePointFn = void(__cdecl*)(const int32_t*, int32_t*, int32_t*);

HudPassFn g_hudPass = nullptr;
DrawTextureFn g_drawTexture = nullptr;
ScalePointFn g_scalePoint = nullptr;
uintptr_t g_base = 0;
int g_mode = 0;
int g_depth = 0;
int g_textureAnchor = -1;

bool Detour(uint8_t* target, size_t length, const void* replacement, void** original) {
    auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) return false;
    std::memcpy(trampoline, target, length);
    if (!pixel_perfect_crosshair::EncodeRelativeBranch(
            trampoline + length, reinterpret_cast<uintptr_t>(trampoline + length),
            reinterpret_cast<uintptr_t>(target + length), 0xE9)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    uint8_t jump[5] = {};
    if (!pixel_perfect_crosshair::EncodeRelativeBranch(
            jump, reinterpret_cast<uintptr_t>(target),
            reinterpret_cast<uintptr_t>(replacement), 0xE9)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    DWORD protection = 0;
    if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(target, jump, sizeof(jump));
    std::memset(target + sizeof(jump), 0x90, length - sizeof(jump));
    FlushInstructionCache(GetCurrentProcess(), target, length);
    DWORD ignored = 0;
    VirtualProtect(target, length, protection, &ignored);
    *original = trampoline;
    return true;
}

void __cdecl DrawHud(uint32_t player) {
    ++g_depth;
    g_hudPass(player);
    --g_depth;
}

void __cdecl DrawTexture(int x, int y, int sourceWidth, int sourceHeight,
                         int width, int height, uint32_t texture, uint32_t color) {
    const int previousAnchor = g_textureAnchor;
    g_textureAnchor = width <= 341 ? x + width / 2 : -1;
    g_drawTexture(x, y, sourceWidth, sourceHeight, width, height, texture, color);
    g_textureAnchor = previousAnchor;
}

void __cdecl ScalePoint(const int32_t* size, int32_t* x, int32_t* y) {
    const int width = *reinterpret_cast<const volatile int32_t*>(g_base + kWidthRva);
    const int height = *reinterpret_cast<const volatile int32_t*>(g_base + kHeightRva);
    if (g_depth == 0 || g_mode < 2 ||
        static_cast<int64_t>(width) * 3 <= static_cast<int64_t>(height) * 4) {
        g_scalePoint(size, x, y);
        return;
    }
    TransformPoint(g_mode, width, height, g_textureAnchor < 0 ? *x : g_textureAnchor, x, y);
}
}  // namespace

bool Install(int mode) {
    g_mode = mode < 0 || mode > 3 ? 1 : mode;
    if (g_mode == 0) return true;
    const bool crosshair = pixel_perfect_crosshair::Install(true);
    if (g_mode == 1) return crosshair;

    static_assert(sizeof(void*) == 4, "HUDLayoutMode requires the 32-bit game build");
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* scale = reinterpret_cast<uint8_t*>(g_base + kScalePointRva);
    auto* hud = reinterpret_cast<uint8_t*>(g_base + kHudPassRva);
    auto* texture = reinterpret_cast<uint8_t*>(g_base + kDrawTextureRva);
    if (std::memcmp(scale, kScaleSignature, sizeof(kScaleSignature)) != 0 ||
        std::memcmp(hud, kHudSignature, sizeof(kHudSignature)) != 0 ||
        std::memcmp(texture, kDrawTextureSignature, sizeof(kDrawTextureSignature)) != 0) return false;
    return crosshair && hud_overlays::Install(g_mode) &&
        Detour(texture, kDrawTextureHookLength, reinterpret_cast<const void*>(&DrawTexture),
               reinterpret_cast<void**>(&g_drawTexture)) &&
        Detour(scale, kScaleHookLength, reinterpret_cast<const void*>(&ScalePoint),
               reinterpret_cast<void**>(&g_scalePoint)) &&
        Detour(hud, kHudHookLength, reinterpret_cast<const void*>(&DrawHud),
               reinterpret_cast<void**>(&g_hudPass));
}

}  // namespace hud_layout
