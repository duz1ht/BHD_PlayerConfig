#include "hud_overlays.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "hud_layout_math.h"

namespace hud_overlays {
namespace {
uintptr_t g_base = 0;
int g_mode = 0;
bool g_installed = false;
int g_gameMenuDepth = 0;
int g_drawDepth = 0;
int g_offsetX = 0;
int g_canvasMargin = 0;

template <typename T> T& At(uintptr_t rva) {
    return *reinterpret_cast<T*>(g_base + rva);
}

// Only positions are translated at submission. Native layout/word wrapping
// runs at the virtual width first; squeezing completed glyphs would deform them.
struct Vertex {
    float x, y, z, rhw;
    uint32_t diffuse, specular;
    float u, v, u2, v2;
};
static_assert(sizeof(Vertex) == 40, "DFBHD FVF 0x2c4 vertex layout");
using DrawVerticesFn = uint32_t(__cdecl*)(int, const Vertex*, uint32_t);
using DrawIndexedFn = uint32_t(__cdecl*)(int, const Vertex*, uint32_t, const uint16_t*, uint32_t);
using DrawQuadFn = uint32_t(__cdecl*)(const float*, const float*);
using DrawLineFn = uint32_t(__cdecl*)(const int32_t*, const int32_t*);
using VoidFn = void(__cdecl*)();
using OverlayFn = void(__cdecl*)(const int32_t*);
using WindowFn = void(__cdecl*)(const int32_t*, int, int, int, int, const char*, uint32_t);
DrawVerticesFn g_drawVertices = nullptr;
DrawIndexedFn g_drawIndexed = nullptr;
DrawQuadFn g_drawQuad = nullptr;
DrawLineFn g_drawLine = nullptr;
VoidFn g_guiUpdate = nullptr;
VoidFn g_guiDraw = nullptr;

hud_layout::OverlayCanvas Canvas() {
    return hud_layout::GetOverlayCanvas(g_mode, At<int32_t>(0x5F72C0), At<int32_t>(0x5F72C4));
}

// The native overlay fonts cache two pixel scales at load time. Temporarily
// rescale both, so measurements, line spacing and drawing agree at 4:3.
class DrawingScope {
public:
    DrawingScope() : outer_(g_drawDepth++ == 0) {
        if (!outer_) return;
        width_ = At<int32_t>(0x5F72C0);
        const auto canvas = Canvas();
        g_offsetX = canvas.offsetX;
        g_canvasMargin = width_ - canvas.width;
        At<int32_t>(0x5F72C0) = canvas.width;
        for (size_t i = 0; i < fonts_.size(); ++i) {
            auto* scales = reinterpret_cast<float*>(g_base + 0x55B9B4 + i * 12);
            fonts_[i] = {scales[0], scales[1]};
            scales[0] *= canvas.widthRatio;
            scales[1] *= canvas.widthRatio;
        }
    }
    ~DrawingScope() {
        if (outer_) {
            At<int32_t>(0x5F72C0) = width_;
            for (size_t i = 0; i < fonts_.size(); ++i) {
                auto* scales = reinterpret_cast<float*>(g_base + 0x55B9B4 + i * 12);
                scales[0] = fonts_[i][0];
                scales[1] = fonts_[i][1];
            }
            g_offsetX = 0;
            g_canvasMargin = 0;
        }
        --g_drawDepth;
    }
    DrawingScope(const DrawingScope&) = delete;
    DrawingScope& operator=(const DrawingScope&) = delete;
private:
    bool outer_;
    int width_ = 0;
    std::array<std::array<float, 2>, 4> fonts_{};
};

// The menu library uses its own 800x600 space and cached forward/inverse
// scales. Mouse positions must use that same canvas during hit testing and
// cursor updates, after the OS event pump has refreshed them.
class GuiScope {
public:
    GuiScope() : scale_(At<float>(0xB663AC)), inverse_(At<float>(0xB662F8)),
                 width_(At<int32_t>(0xB663B8)), mouseX_(At<int32_t>(0xB655E0)) {
        const auto canvas = Canvas();
        At<float>(0xB663AC) *= canvas.widthRatio;
        At<float>(0xB662F8) /= canvas.widthRatio;
        At<int32_t>(0xB663B8) = canvas.width;
        At<int32_t>(0xB655E0) -= canvas.offsetX;
    }
    ~GuiScope() {
        At<float>(0xB663AC) = scale_;
        At<float>(0xB662F8) = inverse_;
        At<int32_t>(0xB663B8) = width_;
        At<int32_t>(0xB655E0) = mouseX_;
    }
    GuiScope(const GuiScope&) = delete;
    GuiScope& operator=(const GuiScope&) = delete;
private:
    float scale_, inverse_;
    int32_t width_, mouseX_;
};

void __cdecl DrawOverlays(const int32_t* size) {
    DrawingScope scope;
    reinterpret_cast<OverlayFn>(g_base + 0x11CC20)(size);
}

void __cdecl DrawCompactPlayerSummary(const int32_t* size) {
    // 00424490 draws "Number of players" and the team totals at logical
    // x=845. It shares the window compositor but is a right-edge HUD block.
    // Keep its existing proportional font/spacing; move the whole block to
    // the right edge instead of inheriting the centered window offset.
    const int previousOffset = g_offsetX;
    if (g_drawDepth > 0) g_offsetX = g_canvasMargin;
    reinterpret_cast<OverlayFn>(g_base + 0x24490)(size);
    g_offsetX = previousOffset;
}

void __cdecl DrawArmoryFrame(const int32_t* size, int left, int top, int right,
                             int bottom, const char* title, uint32_t alpha) {
    DrawingScope scope;
    reinterpret_cast<WindowFn>(g_base + 0xB5930)(size, left, top, right, bottom, title, alpha);
}

void __cdecl DrawArmoryRoster() {
    DrawingScope scope;
    reinterpret_cast<VoidFn>(g_base + 0xB6E00)();
}

void __cdecl InGameMenu() {
    // This call site is exclusive to gameplay. Front-end menus use the same
    // GUI library but must not inherit gameplay HUD settings.
    ++g_gameMenuDepth;
    reinterpret_cast<VoidFn>(g_base + 0xDC160)();
    --g_gameMenuDepth;
}

void __cdecl UpdateGui() {
    if (g_gameMenuDepth == 0) { g_guiUpdate(); return; }
    GuiScope scope;
    g_guiUpdate();
}

void __cdecl DrawGui() {
    if (g_gameMenuDepth == 0) { g_guiDraw(); return; }
    GuiScope gui;  // Compute ratio before DrawingScope changes the native width.
    DrawingScope drawing;
    g_guiDraw();
}

template <typename Draw> uint32_t TranslateVertices(const Vertex* vertices, uint32_t count, Draw draw) {
    if (g_offsetX == 0 || vertices == nullptr || count == 0) return draw(vertices);
    // Copy rather than edit the game's font/sprite buffers: they can be reused.
    std::vector<Vertex> copy(vertices, vertices + count);
    for (auto& vertex : copy) vertex.x += static_cast<float>(g_offsetX);
    return draw(copy.data());
}

uint32_t __cdecl DrawVertices(int type, const Vertex* vertices, uint32_t count) {
    return TranslateVertices(vertices, count, [=](const Vertex* translated) {
        return g_drawVertices(type, translated, count);
    });
}

uint32_t __cdecl DrawIndexed(int type, const Vertex* vertices, uint32_t count,
                            const uint16_t* indices, uint32_t indexCount) {
    return TranslateVertices(vertices, count, [=](const Vertex* translated) {
        return g_drawIndexed(type, translated, count, indices, indexCount);
    });
}

uint32_t __cdecl DrawQuad(const float* uv, const float* rectangle) {
    if (g_offsetX == 0 || rectangle == nullptr) return g_drawQuad(uv, rectangle);
    const float translated[4] = {rectangle[0] + g_offsetX, rectangle[1],
                                 rectangle[2] + g_offsetX, rectangle[3]};
    return g_drawQuad(uv, translated);
}

uint32_t __cdecl DrawLine(const int32_t* from, const int32_t* to) {
    if (g_offsetX == 0 || from == nullptr || to == nullptr) return g_drawLine(from, to);
    const int32_t translatedFrom[2] = {from[0] + g_offsetX, from[1]};
    const int32_t translatedTo[2] = {to[0] + g_offsetX, to[1]};
    const uint32_t result = g_drawLine(translatedFrom, translatedTo);
    // The native line routine stores the endpoint for the next line segment.
    At<int32_t>(0xA96828) = to[0];
    At<int32_t>(0xA9682C) = to[1];
    return result;
}

struct Hook {
    uintptr_t rva;
    size_t length;
    uint8_t expected[8];
    const void* replacement;
    void** original;
    uint8_t* trampoline = nullptr;
};

void Branch(uint8_t* output, uintptr_t instruction, uintptr_t target, uint8_t opcode) {
    // x86 rel32 is modular, including DLLs loaded above 0x80000000.
    const uint32_t relative = static_cast<uint32_t>(target - instruction - 5);
    output[0] = opcode;
    std::memcpy(output + 1, &relative, sizeof(relative));
}

bool Write(uint8_t* target, const uint8_t* bytes, size_t length) {
    DWORD protection = 0;
    if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(target, bytes, length);
    FlushInstructionCache(GetCurrentProcess(), target, length);
    DWORD ignored = 0;
    VirtualProtect(target, length, protection, &ignored);
    return true;
}

bool InstallHooks() {
    Hook hooks[] = {
        {0x1AA0D0, 8, {0x83,0xEC,0x08,0x53,0x8B,0x5C,0x24,0x18},
         reinterpret_cast<const void*>(&DrawVertices), reinterpret_cast<void**>(&g_drawVertices)},
        {0x1AA320, 8, {0x83,0xEC,0x0C,0x53,0x8B,0x5C,0x24,0x1C},
         reinterpret_cast<const void*>(&DrawIndexed), reinterpret_cast<void**>(&g_drawIndexed)},
        {0x1AA550, 5, {0xA0,0x90,0x48,0xE9,0x00},
         reinterpret_cast<const void*>(&DrawQuad), reinterpret_cast<void**>(&g_drawQuad)},
        {0x1A9ED0, 8, {0x83,0xEC,0x08,0x56,0x8D,0x44,0x24,0x04},
         reinterpret_cast<const void*>(&DrawLine), reinterpret_cast<void**>(&g_drawLine)},
        {0x16F960, 6, {0x8B,0x0D,0x00,0x63,0xF6,0x00},
         reinterpret_cast<const void*>(&UpdateGui), reinterpret_cast<void**>(&g_guiUpdate)},
        {0x16F9A0, 6, {0xFF,0x05,0xB4,0x63,0xF6,0x00},
         reinterpret_cast<const void*>(&DrawGui), reinterpret_cast<void**>(&g_guiDraw)},
        {0xB79CC, 5, {0xE8,0x4F,0x52,0x06,0x00}, reinterpret_cast<const void*>(&DrawOverlays), nullptr},
        {0xB7A3F, 5, {0xE8,0xEC,0xDE,0xFF,0xFF}, reinterpret_cast<const void*>(&DrawArmoryFrame), nullptr},
        {0xB7A50, 5, {0xE8,0xAB,0xF3,0xFF,0xFF}, reinterpret_cast<const void*>(&DrawArmoryRoster), nullptr},
        {0xB7A55, 5, {0xE8,0x06,0x47,0x02,0x00}, reinterpret_cast<const void*>(&InGameMenu), nullptr},
        // Only the compact branch of 00425380; the full K list stays centered.
        {0x253A2, 5, {0xE8,0xE9,0xF0,0xFF,0xFF}, reinterpret_cast<const void*>(&DrawCompactPlayerSummary), nullptr},
    };
    // All stolen instructions are complete and contain no relative branches.
    // Validate and allocate everything before publishing any entry point.
    for (const auto& hook : hooks)
        if (std::memcmp(reinterpret_cast<const void*>(g_base + hook.rva),
                        hook.expected, hook.length) != 0) return false;
    auto cleanup = [&]() {
        for (auto& hook : hooks) {
            if (hook.original) *hook.original = nullptr;
            if (hook.trampoline) VirtualFree(hook.trampoline, 0, MEM_RELEASE);
        }
    };
    for (auto& hook : hooks) {
        if (!hook.original) continue;
        hook.trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, hook.length + 5,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!hook.trampoline) { cleanup(); return false; }
        std::memcpy(hook.trampoline, hook.expected, hook.length);
        Branch(hook.trampoline + hook.length,
               reinterpret_cast<uintptr_t>(hook.trampoline + hook.length),
               g_base + hook.rva + hook.length, 0xE9);
        FlushInstructionCache(GetCurrentProcess(), hook.trampoline, hook.length + 5);
        *hook.original = hook.trampoline;
    }
    size_t installed = 0;
    for (const auto& hook : hooks) {
        uint8_t patch[8];
        std::memset(patch, 0x90, sizeof(patch));
        Branch(patch, g_base + hook.rva, reinterpret_cast<uintptr_t>(hook.replacement),
               hook.original ? 0xE9 : 0xE8);
        if (!Write(reinterpret_cast<uint8_t*>(g_base + hook.rva), patch, hook.length)) {
            bool restored = true;
            while (installed != 0) {
                const auto& previous = hooks[--installed];
                restored &= Write(reinterpret_cast<uint8_t*>(g_base + previous.rva),
                                  previous.expected, previous.length);
            }
            // Keep callable trampolines if the OS refuses a rollback write.
            if (restored) cleanup();
            return false;
        }
        ++installed;
    }
    return true;
}
}  // namespace

bool Install(int mode) {
    if (mode < 2 || mode > 3) return true;
    if (g_installed) return true;
    static_assert(sizeof(void*) == 4, "HUD overlays require the 32-bit game build");
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_mode = mode;
    g_installed = InstallHooks();
    return g_installed;
}
}  // namespace hud_overlays
