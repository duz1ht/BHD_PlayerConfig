#include <windows.h>
#include <shlwapi.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "bhd_patches.h"

#include "borderless_fullscreen.h"
#include "borderless_gamma.h"
#include "cursor_clip.h"
#include "game_window.h"
#include "raw_input.h"
#include "mouse_scaling_fix.h"
#include "mousescale_fix.h"
#include "camera_fov.h"
#include "camera_position.h"
#include "high_frequency_viewmodel.h"
#include "remote_player_interpolation.h"
#include "allow_high_resolution_fonts.h"
#include "hud_layout.h"

namespace {
constexpr uintptr_t kImageBase = 0x400000;
constexpr uintptr_t kLegacyCursorRecenter = 0x00567958;
constexpr uintptr_t kLegacyCursorEpilogue = 0x005679BA;
constexpr uintptr_t kGameWindow = 0x00F654FC;
constexpr uintptr_t kLegacyMouseX = 0x00F655E0;
constexpr uintptr_t kLegacyMouseY = 0x00F655E4;
constexpr uintptr_t kRelativeMouseX = 0x00F655EC;
constexpr uintptr_t kRelativeMouseY = 0x00F655F0;
constexpr uintptr_t kPatchAddressInitializeInGameSystemsClipCursor = 0x4623D2;
constexpr uintptr_t kPatchAddressClipCursorToViewPort = 0x4628D3;
constexpr uintptr_t kInitialClipCursorCodeCave = 0x5E4912;
constexpr uintptr_t kClipCursorCodeCave = 0x5E492B;

volatile LONG g_initialized = 0;
struct PatchConfig {
    bool adaptiveScreenCenter = true;
    bool scaleCursorClipToResolution = true;
    bool rawMouseInput = true;
    bool restoreCursorClip = true;
    bool mouseScalingFix = true;
    bool mousescaleFix = true;
    bool useCorrectAspectFov = true;
    bool highFrequencyCamera = true;
    bool highFrequencyCameraPosition = true;
    bool highFrequencyCameraPositionExtrapolation = false;
    bool highFrequencyViewmodel = true;
    bool remotePlayerInterpolation = false;
    bool remotePlayerAnimationInterpolation = false;
    bool borderlessFullscreen = true;
    bool forceDesktopResolution = true;
    bool allowHighResolutionFonts = true;
    int hudLayoutMode = 1;
};

struct ByteSpan {
    const unsigned char* bytes;
    size_t size;
};

struct Patch {
    uintptr_t virtualAddress;
    ByteSpan expected;
    ByteSpan replacement;
    bool allowZeroFilledExpected;
};

#define BYTE_SPAN(name) ByteSpan{name, sizeof(name)}

// PollMouseInput tail after draining mouse messages, through its original return.
const unsigned char kLegacyCursorExpected[] = {
    0x8B, 0x15, 0xFC, 0x54, 0xF6, 0x00,             // mov edx,[gameWindow]
    0x8D, 0x4C, 0x24, 0x04,                         // lea ecx,[esp+4]
    0x51, 0x52,                                     // push ecx; push edx
    0xFF, 0x15, 0x20, 0x13, 0x61, 0x00,             // call [ClientToScreen]
    0x8B, 0x44, 0x24, 0x08, 0x8B, 0x4C, 0x24, 0x04, // Load screen origin.
    0x05, 0xF0, 0x00, 0x00, 0x00, 0x50,             // Push origin.y + 240.
    0x81, 0xC1, 0x40, 0x01, 0x00, 0x00, 0x51,       // Push origin.x + 320.
    0xFF, 0x15, 0xD8, 0x12, 0x61, 0x00,             // call [SetCursorPos]
    0x8B, 0x15, 0xE0, 0x55, 0xF6, 0x00,             // mov edx,[mouseX]
    0xA1, 0xE4, 0x55, 0xF6, 0x00,                   // mov eax,[mouseY]
    0x81, 0xC2, 0xC0, 0xFE, 0xFF, 0xFF,             // add edx,-320
    0x05, 0x10, 0xFF, 0xFF, 0xFF,                   // add eax,-240
    0x89, 0x15, 0xEC, 0x55, 0xF6, 0x00,             // mov [relativeX],edx
    0xA3, 0xF0, 0x55, 0xF6, 0x00,                   // mov [relativeY],eax
    0xC7, 0x05, 0xE0, 0x55, 0xF6, 0x00, 0x40, 0x01, 0x00, 0x00,
    0xC7, 0x05, 0xE4, 0x55, 0xF6, 0x00, 0xF0, 0x00, 0x00, 0x00,
    0x5E, 0x83, 0xC4, 0x24, 0xC3,                   // pop esi; add esp,24h; ret
};

const unsigned char kInitialClipCursorCodeCaveBytes[] = {
    0x8B, 0x05, 0xC0,
    0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xF8, 0x8B, 0x05, 0xC4, 0x72, 0x9F,
    0x00, 0x48, 0x89, 0x45, 0xFC, 0xE9, 0xAC, 0xDA, 0xE7, 0xFF,
};

const unsigned char kClipCursorCodeCaveBytes[] = {
    0x8B, 0x05, 0xC0, 0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xF8, 0x8B, 0x05,
    0xC4, 0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xFC, 0xE9, 0x9D, 0xDF, 0xE7,
    0xFF,
};

const unsigned char kInitialClipCursorExpected[] = {0xC7, 0x45, 0xF8, 0x7F, 0x02, 0x00, 0x00};
const unsigned char kInitialClipCursorReplacement[] = {0xE9, 0x3B, 0x25, 0x18, 0x00, 0x90, 0x90};
const unsigned char kClipCursorExpected[] = {0xC7, 0x45, 0xF8, 0x7F, 0x02, 0x00, 0x00};
const unsigned char kClipCursorReplacement[] = {0xE9, 0x53, 0x20, 0x18, 0x00, 0x90, 0x90};

const Patch kInitialClipCursorCodeCavePatch = {
    kInitialClipCursorCodeCave,
    {nullptr, sizeof(kInitialClipCursorCodeCaveBytes)},
    BYTE_SPAN(kInitialClipCursorCodeCaveBytes),
    true,
};

const Patch kClipCursorCodeCavePatch = {
    kClipCursorCodeCave,
    {nullptr, sizeof(kClipCursorCodeCaveBytes)},
    BYTE_SPAN(kClipCursorCodeCaveBytes),
    true,
};

const Patch kClipCursorPatches[] = {
    {kPatchAddressInitializeInGameSystemsClipCursor, BYTE_SPAN(kInitialClipCursorExpected),
     BYTE_SPAN(kInitialClipCursorReplacement), false},
    {kPatchAddressClipCursorToViewPort, BYTE_SPAN(kClipCursorExpected),
     BYTE_SPAN(kClipCursorReplacement), false},
};

bool BytesEqual(const unsigned char* current, ByteSpan expected) {
    if (expected.bytes == nullptr) {
        return false;
    }
    for (size_t i = 0; i < expected.size; ++i) {
        if (current[i] != expected.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool BytesAreZeroFilled(const unsigned char* current, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (current[i] != 0x00) {
            return false;
        }
    }
    return true;
}

bool WriteBytes(uintptr_t virtualAddress, ByteSpan replacement) {
    auto* address = reinterpret_cast<unsigned char*>(virtualAddress);

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, replacement.size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    for (size_t i = 0; i < replacement.size; ++i) {
        address[i] = replacement.bytes[i];
    }
    FlushInstructionCache(GetCurrentProcess(), address, replacement.size);

    DWORD ignored = 0;
    VirtualProtect(address, replacement.size, oldProtect, &ignored);
    return true;
}

bool ApplyPatch(const Patch& patch) {
    auto* address = reinterpret_cast<unsigned char*>(patch.virtualAddress);
    if (BytesEqual(address, patch.replacement)) {
        return true;
    }

    const bool expectedMatches = BytesEqual(address, patch.expected);
    const bool zeroFilledMatches = patch.allowZeroFilledExpected && BytesAreZeroFilled(address, patch.expected.size);
    if (!expectedMatches && !zeroFilledMatches) {
        return false;
    }

    return WriteBytes(patch.virtualAddress, patch.replacement);
}

void __cdecl RecenterLegacyCursor() {
    static HWND previousWindow = nullptr;
    static POINT previousCenter = {};

    const HWND window = *reinterpret_cast<HWND*>(kGameWindow);
    auto& mouseX = *reinterpret_cast<LONG*>(kLegacyMouseX);
    auto& mouseY = *reinterpret_cast<LONG*>(kLegacyMouseY);
    auto& relativeX = *reinterpret_cast<LONG*>(kRelativeMouseX);
    auto& relativeY = *reinterpret_cast<LONG*>(kRelativeMouseY);
    const LONG polledX = mouseX;
    const LONG polledY = mouseY;
    relativeX = 0;
    relativeY = 0;

    RECT client = {};
    if (!GetClientRect(window, &client) || IsRectEmpty(&client)) {
        previousWindow = nullptr;
        return;
    }
    const POINT center = {(client.left + client.right) / 2,
                          (client.top + client.bottom) / 2};
    POINT screen = center;
    if (!ClientToScreen(window, &screen) || !SetCursorPos(screen.x, screen.y)) {
        previousWindow = nullptr;
        return;
    }

    // The first poll at a new center may still contain coordinates from the old one.
    if (window == previousWindow && center.x == previousCenter.x && center.y == previousCenter.y) {
        relativeX = polledX - center.x;
        relativeY = polledY - center.y;
    }
    mouseX = center.x;
    mouseY = center.y;
    previousWindow = window;
    previousCenter = center;
}

void InstallAdaptiveScreenCenter() {
    // Replace the first two whole instructions with a call and a jump to the
    // original epilogue. The Raw Input hook is at the separate function entry.
    unsigned char replacement[] = {0xE8, 0, 0, 0, 0, 0xE9, 0, 0, 0, 0};
    const uint32_t callOffset = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&RecenterLegacyCursor) - (kLegacyCursorRecenter + 5));
    const uint32_t jumpOffset = static_cast<uint32_t>(
        kLegacyCursorEpilogue - (kLegacyCursorRecenter + sizeof(replacement)));
    std::memcpy(replacement + 1, &callOffset, sizeof(callOffset));
    std::memcpy(replacement + 6, &jumpOffset, sizeof(jumpOffset));
    ApplyPatch({kLegacyCursorRecenter, BYTE_SPAN(kLegacyCursorExpected), BYTE_SPAN(replacement), false});
}

template <size_t Count>
bool ApplyPatchGroup(const Patch (&patches)[Count]) {
    bool appliedAll = true;
    for (const auto& patch : patches) {
        appliedAll = ApplyPatch(patch) && appliedAll;
    }
    return appliedAll;
}

bool BoolFromIni(const wchar_t* path, const wchar_t* key, bool defaultValue) {
    return GetPrivateProfileIntW(L"PatchGroups", key, defaultValue ? 1 : 0, path) != 0;
}

int IntFromIni(const wchar_t* path, const wchar_t* key, int defaultValue) {
    return GetPrivateProfileIntW(L"PatchGroups", key, defaultValue, path);
}

void BuildIniPath(wchar_t* iniPath, DWORD size) {
    iniPath[0] = L'\0';
    const DWORD length = GetModuleFileNameW(nullptr, iniPath, size);
    if (length == 0 || length >= size) {
        lstrcpynW(iniPath, L".\\bhd_player_config.ini", size);
        return;
    }

    PathRemoveFileSpecW(iniPath);
    PathAppendW(iniPath, L"bhd_player_config.ini");
}

PatchConfig LoadPatchConfig() {
    wchar_t iniPath[MAX_PATH] = {};
    BuildIniPath(iniPath, MAX_PATH);

    PatchConfig config = {};
    config.adaptiveScreenCenter =
        BoolFromIni(iniPath, L"AdaptiveScreenCenter", config.adaptiveScreenCenter);
    config.scaleCursorClipToResolution = BoolFromIni(
        iniPath, L"ScaleCursorClipToResolution", config.scaleCursorClipToResolution);
    config.rawMouseInput = BoolFromIni(iniPath, L"RawMouseInput", config.rawMouseInput);
    config.restoreCursorClip =
        BoolFromIni(iniPath, L"RestoreCursorClip", config.restoreCursorClip);
    config.mouseScalingFix = BoolFromIni(iniPath, L"MouseScalingFix", config.mouseScalingFix);
    config.mousescaleFix =
        BoolFromIni(iniPath, L"MousescaleFix", config.mousescaleFix);
    config.useCorrectAspectFov =
        BoolFromIni(iniPath, L"UseCorrectAspectFOV", config.useCorrectAspectFov);
    config.highFrequencyCamera =
        BoolFromIni(iniPath, L"HighFrequencyCamera", config.highFrequencyCamera);
    config.highFrequencyCameraPosition = BoolFromIni(
        iniPath, L"HighFrequencyCameraPosition", config.highFrequencyCameraPosition);
    config.highFrequencyCameraPositionExtrapolation = BoolFromIni(
        iniPath, L"HighFrequencyCameraPositionExtrapolation", config.highFrequencyCameraPositionExtrapolation);
    config.highFrequencyViewmodel = BoolFromIni(
        iniPath, L"HighFrequencyViewmodel", config.highFrequencyViewmodel);
    config.remotePlayerInterpolation =
        BoolFromIni(iniPath, L"RemotePlayerInterpolation", config.remotePlayerInterpolation);
    config.remotePlayerAnimationInterpolation = BoolFromIni(
        iniPath, L"RemotePlayerAnimationInterpolation", config.remotePlayerAnimationInterpolation);
    config.borderlessFullscreen =
        BoolFromIni(iniPath, L"BorderlessFullscreen", config.borderlessFullscreen);
    config.forceDesktopResolution =
        BoolFromIni(iniPath, L"ForceDesktopResolution", config.forceDesktopResolution);
    config.allowHighResolutionFonts =
        BoolFromIni(iniPath, L"AllowHighResolutionFonts", config.allowHighResolutionFonts);
    const int legacyCrosshair = BoolFromIni(iniPath, L"PixelPerfectCrosshair", true) ? 1 : 0;
    config.hudLayoutMode = IntFromIni(iniPath, L"HUDLayoutMode", legacyCrosshair);
    if (config.hudLayoutMode < 0 || config.hudLayoutMode > 3) config.hudLayoutMode = 1;
    return config;
}

void ApplyBhdPatches() {
    const PatchConfig config = LoadPatchConfig();
    const uintptr_t imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (imageBase != kImageBase) {
        return;
    }
    mouse_scaling_fix::Install(config.mouseScalingFix);
    remote_player_interpolation::Install(config.remotePlayerInterpolation,
                                         config.remotePlayerAnimationInterpolation);
    mousescale_fix::Install(config.mousescaleFix);
    allow_high_resolution_fonts::Install(config.allowHighResolutionFonts);
    hud_layout::Install(config.hudLayoutMode);
    const bool borderlessInitialized =
        borderless_fullscreen::Initialize(config.borderlessFullscreen,
                                          config.forceDesktopResolution);
    const bool gammaInitialized = borderless_gamma::Initialize(
        config.borderlessFullscreen && borderlessInitialized);

    if (config.adaptiveScreenCenter) {
        InstallAdaptiveScreenCenter();
    }
    if (config.scaleCursorClipToResolution) {
        if (ApplyPatch(kInitialClipCursorCodeCavePatch) && ApplyPatch(kClipCursorCodeCavePatch)) {
            ApplyPatchGroup(kClipCursorPatches);
        }
    }
    if (config.restoreCursorClip) {
        cursor_clip::InstallGameHook();
    }
    const bool rawInstalled = config.rawMouseInput && raw_input::Install(true);
    const bool cameraRotation = config.highFrequencyCamera && rawInstalled;
    auto positionMode = camera_position::Mode::Disabled;
    if (config.highFrequencyCameraPositionExtrapolation) {
        positionMode = camera_position::Mode::Extrapolation;
    } else if (config.highFrequencyCameraPosition) {
        positionMode = camera_position::Mode::Interpolation;
    }
    const bool cameraInstalled = camera_fov::Install(
        config.useCorrectAspectFov, cameraRotation, positionMode);
    high_frequency_viewmodel::Install(config.highFrequencyViewmodel && cameraInstalled &&
                                      (cameraRotation || camera_position::IsEnabled()));
    game_window::Configure(
        {rawInstalled, config.restoreCursorClip,
         config.borderlessFullscreen && borderlessInitialized,
         config.borderlessFullscreen && gammaInitialized,
         config.mouseScalingFix});
}

}  // namespace

namespace bhd_patches {
void InitializeForWinmmCall(uintptr_t returnAddress) {
    // These are the two timer calls in the supported executable's startup path.
    // Calls from DLL initialization, timer callbacks and unrelated executables
    // must only forward to WinMM, without installing patches or starting threads.
    if (returnAddress != 0x00569F7A && returnAddress != 0x00569FC5) return;
    const auto* image = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (reinterpret_cast<uintptr_t>(image) != kImageBase) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x1000) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.SizeOfImage < 0x00B65600) return;
    const uintptr_t importAddress = returnAddress == 0x00569F7A ? 0x00611374 : 0x00611370;
    const auto* call = reinterpret_cast<const unsigned char*>(returnAddress - 6);
    if (call[0] != 0xFF || call[1] != 0x15 ||
        *reinterpret_cast<const uint32_t*>(call + 2) != importAddress) return;
    if (InterlockedCompareExchange(&g_initialized, 1, 0) != 0) return;
    ApplyBhdPatches();
    InterlockedExchange(&g_initialized, 2);
}
}  // namespace bhd_patches
