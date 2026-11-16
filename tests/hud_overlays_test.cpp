#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
uintptr_t image = 0;
uintptr_t failTarget = 0;
HMODULE WINAPI TestModule(LPCWSTR) { return reinterpret_cast<HMODULE>(image); }
BOOL WINAPI TestProtect(LPVOID p, SIZE_T n, DWORD mode, PDWORD old) {
    if (reinterpret_cast<uintptr_t>(p) == failTarget && mode == PAGE_EXECUTE_READWRITE) {
        failTarget = 0; // A single write fails; rollback must still work.
        return FALSE;
    }
    return VirtualProtect(p, n, mode, old);
}
}
#define GetModuleHandleW TestModule
#define VirtualProtect TestProtect
#include "../src/hud_overlays.cpp"
#undef VirtualProtect
#undef GetModuleHandleW

namespace {
using namespace hud_overlays;
void Require(bool ok, int line) {
    if (!ok) { std::fprintf(stderr, "HUD overlays regression at line %d\n", line); std::exit(1); }
}
#define Check(x) Require((x), __LINE__)
bool Near(float a, float b) { return std::fabs(a - b) < 0.0001f; }
struct Site { uintptr_t rva; std::vector<uint8_t> bytes; };
const Site sites[] = {
    {0x1AA0D0, {0x83,0xEC,0x08,0x53,0x8B,0x5C,0x24,0x18}},
    {0x1AA320, {0x83,0xEC,0x0C,0x53,0x8B,0x5C,0x24,0x1C}},
    {0x1AA550, {0xA0,0x90,0x48,0xE9,0x00}},
    {0x1A9ED0, {0x83,0xEC,0x08,0x56,0x8D,0x44,0x24,0x04}},
    {0x16F960, {0x8B,0x0D,0x00,0x63,0xF6,0x00}},
    {0x16F9A0, {0xFF,0x05,0xB4,0x63,0xF6,0x00}},
    {0xB79CC, {0xE8,0x4F,0x52,0x06,0x00}},
    {0xB7A3F, {0xE8,0xEC,0xDE,0xFF,0xFF}},
    {0xB7A50, {0xE8,0xAB,0xF3,0xFF,0xFF}},
    {0xB7A55, {0xE8,0x06,0x47,0x02,0x00}},
    {0x253A2, {0xE8,0xE9,0xF0,0xFF,0xFF}},
};
void SeedSites() {
    for (const auto& site : sites)
        std::memcpy(reinterpret_cast<void*>(image + site.rva), site.bytes.data(), site.bytes.size());
}
void CheckUnmodified() {
    for (const auto& site : sites)
        Check(std::memcmp(reinterpret_cast<void*>(image + site.rva), site.bytes.data(), site.bytes.size()) == 0);
}

std::vector<Vertex> submitted;
std::array<float, 4> quad{};
std::array<int32_t, 4> line{};
uint32_t __cdecl CaptureVertices(int type, const Vertex* vertices, uint32_t count) {
    Check(type == 5);
    submitted.assign(vertices, vertices + count);
    return 123;
}
uint32_t __cdecl CaptureIndexed(int type, const Vertex* vertices, uint32_t count,
                                const uint16_t* indices, uint32_t indexCount) {
    Check(indexCount == 3 && indices[2] == 2);
    return CaptureVertices(type, vertices, count);
}
uint32_t __cdecl CaptureQuad(const float* uv, const float* rectangle) {
    Check(uv[2] == 1.0f);
    std::memcpy(quad.data(), rectangle, sizeof(float) * 4);
    return 456;
}
uint32_t __cdecl CaptureLine(const int32_t* from, const int32_t* to) {
    line = {from[0], from[1], to[0], to[1]};
    At<int32_t>(0xA96828) = to[0];
    At<int32_t>(0xA9682C) = to[1];
    return 789;
}

void CheckDrawState() {
    Check(At<int32_t>(0x5F72C0) == 1440);
    Check(At<int32_t>(0x5F72C4) == 1080 && g_offsetX == 240);
    for (int i = 0; i < 4; ++i) {
        Check(Near(At<float>(0x55B9B4 + i * 12), 1.40625f));
        Check(Near(At<float>(0x55B9B8 + i * 12), 1.40625f));
    }
}
int nativeCalls = 0;
float summaryFontScale = 0;
void __cdecl NativeCompactSummary(const int32_t* size) {
    // Actual logical coordinates and integer rounding from 00424490 ->
    // 0052DD90, exercising final submission as well as the scoped layout.
    Vertex rows[3]{};
    const int y[] = {0x177, 0x18B, 0x19A};
    for (int i = 0; i < 3; ++i) {
        rows[i].x = static_cast<float>((size[0] * 0x34D + 0x180) / 1024);
        rows[i].y = static_cast<float>((size[1] * y[i] + 0x180) / 768);
    }
    summaryFontScale = At<float>(0x55B9C0);
    DrawVertices(5, rows, 3);
}
void __cdecl NativeOverlay(const int32_t* size) {
    ++nativeCalls;
    Check(size[0] == 1440 && size[1] == 1080);
    CheckDrawState();
    DrawCompactPlayerSummary(size);
    Check(submitted[0].x == 1668 && submitted[1].x == 1668 && submitted[2].x == 1668);
    Check(submitted[0].y == 527 && submitted[1].y == 555 && submitted[2].y == 577);
    Check(Near(summaryFontScale, 1.40625f));
    // Following windows must recover their centered offset, not inherit
    // the summary's right-edge anchor.
    CheckDrawState();
    // A 100x100 native logical rectangle is square after uniform scaling.
    const float square[4] = {100.0f * size[0] / 1024, 100.0f * size[1] / 768,
                             200.0f * size[0] / 1024, 200.0f * size[1] / 768};
    const float uv[4] = {0,0,1,1};
    DrawQuad(uv, square);
    Check(Near(quad[2] - quad[0], quad[3] - quad[1]));
    Check(Near(quad[0], 380.625f));
    // Nested drawing must neither shrink fonts twice nor lose the offset.
    { DrawingScope nested; CheckDrawState(); }
    CheckDrawState();
}
void __cdecl NativeFrame(const int32_t* size, int left, int top, int right,
                         int bottom, const char* title, uint32_t alpha) {
    Check(left == 256 && top == 64 && right == 768 && bottom == 704);
    Check(std::strcmp(title, "Armory") == 0 && alpha == 255);
    NativeOverlay(size);
}
void __cdecl NativeRoster() { NativeOverlay(&At<int32_t>(0x5F72C0)); }

bool expectGameGui = false;
void CheckGuiState(bool drawing) {
    const float expectedScale = expectGameGui ? 1.8f : 2.4f;
    Check(Near(At<float>(0xB663AC), expectedScale));
    Check(Near(At<float>(0xB662F8), 1.0f / expectedScale));
    Check(Near(At<float>(0xB663B0), 1.8f));
    Check(At<int32_t>(0xB663B8) == (expectGameGui ? 1440 : 1920));
    Check(At<int32_t>(0xB655E0) == (expectGameGui ? 720 : 960));
    if (expectGameGui) {
        // The center of the physical display selects logical x=400 in the
        // menu library, and the drawn cursor returns to physical x=960.
        const float logical = At<int32_t>(0xB655E0) * At<float>(0xB662F8);
        Check(Near(logical, 400));
        if (drawing) Check(Near(logical * At<float>(0xB663AC) + g_offsetX, 960));
    }
    if (drawing && expectGameGui) CheckDrawState();
    else Check(At<int32_t>(0x5F72C0) == 1920 && g_offsetX == 0);
}
void __cdecl NativeGuiUpdate() { CheckGuiState(false); }
void __cdecl NativeGuiDraw() { CheckGuiState(true); }
void CheckRestored() {
    Check(At<int32_t>(0x5F72C0) == 1920 && g_drawDepth == 0 && g_offsetX == 0 && g_canvasMargin == 0);
    Check(At<int32_t>(0xB655E0) == 960 && Near(At<float>(0xB663AC), 2.4f));
    Check(Near(At<float>(0xB662F8), 800.0f / 1920));
    Check(At<int32_t>(0xB663B8) == 1920);
    for (int i = 0; i < 4; ++i) {
        Check(Near(At<float>(0x55B9B4 + i * 12), 1.875f));
        Check(Near(At<float>(0x55B9B8 + i * 12), 1.875f));
    }
}
void __cdecl NativeGameMenu() {
    Check(g_gameMenuDepth == 1);
    UpdateGui();
    CheckRestored();
    DrawGui();
    CheckRestored();
}
void Redirect(uintptr_t rva, const void* destination) {
    Branch(reinterpret_cast<uint8_t*>(image + rva), image + rva,
           reinterpret_cast<uintptr_t>(destination), 0xE9);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(image + rva), 5);
}
}

int main(int argc, char** argv) {
    using namespace hud_overlays;
    image = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, 0xB67000,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Check(image != 0);
    SeedSites();
    Check(Install(0) && Install(1));
    CheckUnmodified();
    if (argc > 1 && std::strcmp(argv[1], "signature") == 0) {
        *reinterpret_cast<uint8_t*>(image + 0x253A2) = 0x90;
        Check(!Install(2));
        Check(g_drawVertices == nullptr && !g_installed);
        *reinterpret_cast<uint8_t*>(image + 0x253A2) = 0xE8;
        CheckUnmodified();
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "rollback") == 0) {
        // Fail after renderer hooks and gameplay call sites have been written.
        failTarget = image + 0x253A2;
        Check(!Install(2));
        CheckUnmodified();
        Check(g_drawVertices == nullptr && g_guiUpdate == nullptr && !g_installed);
        return 0;
    }
    Check(Install(2));
    const void* targets[] = {reinterpret_cast<const void*>(&DrawVertices),
        reinterpret_cast<const void*>(&DrawIndexed), reinterpret_cast<const void*>(&DrawQuad),
        reinterpret_cast<const void*>(&DrawLine), reinterpret_cast<const void*>(&UpdateGui),
        reinterpret_cast<const void*>(&DrawGui), reinterpret_cast<const void*>(&DrawOverlays),
        reinterpret_cast<const void*>(&DrawArmoryFrame), reinterpret_cast<const void*>(&DrawArmoryRoster),
        reinterpret_cast<const void*>(&InGameMenu), reinterpret_cast<const void*>(&DrawCompactPlayerSummary)};
    for (size_t i = 0; i < std::size(sites); ++i) {
        const auto address = image + sites[i].rva;
        Check(*reinterpret_cast<uint8_t*>(address) == (i < 6 ? 0xE9 : 0xE8));
        Check(address + 5 + *reinterpret_cast<uint32_t*>(address + 1) == reinterpret_cast<uintptr_t>(targets[i]));
    }
    At<int32_t>(0x5F72C0) = 1920;
    At<int32_t>(0x5F72C4) = 1080;
    At<float>(0xB663AC) = 2.4f;
    At<float>(0xB663B0) = 1.8f;
    At<float>(0xB662F8) = 800.0f / 1920;
    At<int32_t>(0xB663B8) = 1920;
    At<int32_t>(0xB655E0) = 960;
    for (int i = 0; i < 4; ++i) {
        At<float>(0x55B9B4 + i * 12) = 1.875f;
        At<float>(0x55B9B8 + i * 12) = 1.875f;
    }
    g_drawVertices = CaptureVertices;
    g_drawIndexed = CaptureIndexed;
    g_drawQuad = CaptureQuad;
    g_drawLine = CaptureLine;
    g_guiUpdate = NativeGuiUpdate;
    g_guiDraw = NativeGuiDraw;
    Redirect(0x11CC20, reinterpret_cast<const void*>(&NativeOverlay));
    Redirect(0xB5930, reinterpret_cast<const void*>(&NativeFrame));
    Redirect(0xB6E00, reinterpret_cast<const void*>(&NativeRoster));
    Redirect(0xDC160, reinterpret_cast<const void*>(&NativeGameMenu));
    Redirect(0x24490, reinterpret_cast<const void*>(&NativeCompactSummary));

    for (int mode : {2, 3}) {
        g_mode = mode;
        DrawOverlays(&At<int32_t>(0x5F72C0));
        CheckRestored();
        DrawArmoryFrame(&At<int32_t>(0x5F72C0), 256,64,768,704,"Armory",255);
        CheckRestored();
        DrawArmoryRoster();
        CheckRestored();
        expectGameGui = true;
        InGameMenu();
        CheckRestored();
        Check(g_gameMenuDepth == 0);
        expectGameGui = false;
        UpdateGui(); // front-end GUI must remain native
        DrawGui();
        CheckRestored();
    }
    Check(nativeCalls == 6);
    const Vertex vertices[3] = {{100,200,.5f,1,0x12345678,0,0,0,0,0},
                                {200,200,.5f,1,0,0,1,0,0,0}, {200,300,.5f,1,0,0,1,1,0,0}};
    const uint16_t indices[3] = {0,1,2};
    {
        DrawingScope scope;
        Check(DrawVertices(5, vertices, 3) == 123);
        Check(Near(submitted[0].x, 340) && Near(submitted[0].y, 200));
        Check(submitted[0].diffuse == 0x12345678 && Near(submitted[0].z, .5f));
        Check(DrawIndexed(5, vertices, 3, indices, 3) == 123);
        Check(Near(submitted[2].x, 440) && Near(submitted[2].v, 1));
        const int32_t from[] = {10,20}, to[] = {30,40};
        Check(DrawLine(from, to) == 789);
        Check(line[0] == 250 && line[2] == 270 && line[3] == 40);
        Check(At<int32_t>(0xA96828) == 30 && At<int32_t>(0xA9682C) == 40);
    }
    Check(vertices[0].x == 100 && vertices[2].x == 200);
    DrawVertices(5, vertices, 3); // no translation in the 3D/front-end pass
    Check(std::memcmp(submitted.data(), vertices, sizeof(vertices)) == 0);
    CheckRestored();
    // Without an overlay draw scope, preserve original positions/scales.
    DrawCompactPlayerSummary(&At<int32_t>(0x5F72C0));
    Check(submitted[0].x == 1584 && Near(summaryFontScale, 1.875f));
    for (int mode : {2, 3}) {
        g_mode = mode;
        At<int32_t>(0x5F72C0) = 3440;
        At<int32_t>(0x5F72C4) = 1440;
        {
            DrawingScope scope;
            DrawCompactPlayerSummary(&At<int32_t>(0x5F72C0));
            Check(submitted[0].x == 3104 && submitted[0].y == 703);
            Check(g_offsetX == 760); // Full windows still centered on ultrawide.
        }
        Check(At<int32_t>(0x5F72C0) == 3440 && g_canvasMargin == 0);
    }
    At<int32_t>(0x5F72C0) = 1024;
    At<int32_t>(0x5F72C4) = 768;
    {
        DrawingScope scope;
        DrawCompactPlayerSummary(&At<int32_t>(0x5F72C0));
        Check(submitted[0].x == 845 && submitted[0].y == 375);
        Check(g_offsetX == 0 && At<int32_t>(0x5F72C0) == 1024);
    }
    VirtualFree(reinterpret_cast<void*>(image), 0, MEM_RELEASE);
    return 0;
}
