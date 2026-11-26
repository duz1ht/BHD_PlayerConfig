#include "cursor_clip.h"

#include <cstdint>

#include "cursor_clip_math.h"

namespace cursor_clip {
namespace {
constexpr uintptr_t kGameClipCursorImport = 0x006112F4;
constexpr uintptr_t kGameWindow = 0x00F654FC;
volatile LONG g_gameReleasedClip = 0;
bool g_enabled = false;
bool g_ready = false;
bool g_recoveryPending = false;
bool g_focusSuspended = false;
HWND g_window = nullptr;
RECT g_lastValidClip = {};
bool g_hasLastValidClip = false;

bool GetClientScreenRect(HWND window, RECT* result) {
    if (window == nullptr || !IsWindow(window)) return false;
    RECT client = {};
    if (!GetClientRect(window, &client)) return false;
    POINT upperLeft = {client.left, client.top};
    POINT lowerRight = {client.right, client.bottom};
    if (!ClientToScreen(window, &upperLeft) || !ClientToScreen(window, &lowerRight)) return false;
    *result = {upperLeft.x, upperLeft.y, lowerRight.x, lowerRight.y};
    return !IsRectEmpty(result);
}

BOOL ApplyGameClip(HWND window, const RECT* requested) {
    InterlockedExchange(&g_gameReleasedClip, requested == nullptr ? 1 : 0);
    RECT client = {};
    if (requested != nullptr && window != nullptr && GetForegroundWindow() == window &&
        IsWindowVisible(window) && !IsIconic(window) && GetClientScreenRect(window, &client)) {
        return ClipCursor(&client);
    }
    return ClipCursor(requested);
}

BOOL WINAPI GameClipCursor(const RECT* requested) {
    return ApplyGameClip(*reinterpret_cast<const HWND*>(kGameWindow), requested);
}

RectEdges ToRectEdges(const RECT& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

bool RectCoversFullClient(const RECT& clip, const RECT& client) {
    return cursor_clip::CoversFullClient(ToRectEdges(clip), ToRectEdges(client));
}

bool WindowReady() {
    return g_window != nullptr && IsWindow(g_window) && IsWindowVisible(g_window) &&
           !IsIconic(g_window) && GetForegroundWindow() == g_window && GetFocus() == g_window;
}


bool TryRecover() {
    if (!g_enabled || InterlockedCompareExchange(&g_gameReleasedClip, 0, 0) != 0) return true;
    if (!g_recoveryPending && g_ready) {
        RECT client = {};
        RECT clip = {};
        if (GetClientScreenRect(g_window, &client) && GetClipCursor(&clip) &&
            RectCoversFullClient(clip, client)) return true;
        g_recoveryPending = true;
        g_ready = false;
    }
    if (!WindowReady()) {
        return false;
    }
    RECT requested = {};
    if (!GetClientScreenRect(g_window, &requested)) {
        if (!g_hasLastValidClip) return false;
        requested = g_lastValidClip;
    }
    const BOOL applied = ClipCursor(&requested);
    RECT confirmed = {};
    RECT client = {};
    const bool valid = applied && GetClipCursor(&confirmed) && GetClientScreenRect(g_window, &client) &&
                       RectCoversFullClient(confirmed, client);
    if (!valid) return false;
    g_lastValidClip = confirmed;
    g_hasLastValidClip = true;
    g_recoveryPending = false;
    g_ready = true;
    g_focusSuspended = false;
    return true;
}
}  // namespace

void InstallGameHook() {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return;
    const auto original = reinterpret_cast<void*>(GetProcAddress(user32, "ClipCursor"));
    auto* slot = reinterpret_cast<void**>(kGameClipCursorImport);
    if (original == nullptr || *slot != original) return;

    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return;
    InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(&GameClipCursor), original);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
}

void Initialize(bool enabled, HWND window) {
    g_enabled = enabled;
    g_window = window;
    g_ready = !enabled;
    g_recoveryPending = enabled;
    g_focusSuspended = false;
    if (enabled) {
        TryRecover();
    }
}

void HandleFocusLost() {
    if (!g_enabled || g_focusSuspended) return;
    g_focusSuspended = true;
    g_ready = false;
    g_recoveryPending = false;
    ClipCursor(nullptr);
}

bool HandleFocusGained() {
    if (!g_enabled) return true;
    if (!g_ready) g_recoveryPending = true;
    return TryRecover();
}

void HandleWindowChanged() {
    if (!g_enabled) return;
    TryRecover();
}

void Shutdown() {
    g_ready = false;
    g_recoveryPending = false;
    g_focusSuspended = false;
    g_window = nullptr;
}

}  // namespace cursor_clip
