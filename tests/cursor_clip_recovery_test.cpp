#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {
const HWND kTestWindow = reinterpret_cast<HWND>(1);
HWND foreground = kTestWindow;
POINT clientOrigin = {-1600, 80};
const RECT kDesktop = {-1920, 0, 1920, 1080};
const RECT kNativeClip = {0, 0, 639, 479};
RECT currentClip = kDesktop;
unsigned int clipCalls = 0;

BOOL WINAPI TestIsWindow(HWND window) { return window == kTestWindow; }
BOOL WINAPI TestIsIconic(HWND) { return FALSE; }
HWND WINAPI TestGetForegroundWindow() { return foreground; }
HWND WINAPI TestGetFocus() { return foreground; }

BOOL WINAPI TestGetClientRect(HWND, RECT* rect) {
    *rect = {0, 0, 1280, 720};
    return TRUE;
}

BOOL WINAPI TestClientToScreen(HWND, POINT* point) {
    point->x += clientOrigin.x;
    point->y += clientOrigin.y;
    return TRUE;
}

BOOL WINAPI TestClipCursor(const RECT* rect) {
    currentClip = rect == nullptr ? kDesktop : *rect;
    ++clipCalls;
    return TRUE;
}

BOOL WINAPI TestGetClipCursor(RECT* rect) {
    *rect = currentClip;
    return TRUE;
}

void Check(bool passed, int line) {
    if (passed) return;
    std::fprintf(stderr, "Cursor recovery failed at line %d\n", line);
    std::exit(1);
}
#define CheckThat(condition) Check((condition), __LINE__)
}  // namespace

#define IsWindow TestIsWindow
#define IsWindowVisible TestIsWindow
#define IsIconic TestIsIconic
#define GetForegroundWindow TestGetForegroundWindow
#define GetFocus TestGetFocus
#define GetClientRect TestGetClientRect
#define ClientToScreen TestClientToScreen
#define ClipCursor TestClipCursor
#define GetClipCursor TestGetClipCursor
#include "../src/cursor_clip.cpp"

int main() {
    using namespace cursor_clip;
    Initialize(true, kTestWindow);

    // The native request happens while the game window is already active.
    const RECT initialClient = {-1600, 80, -320, 800};
    CheckThat(ApplyGameClip(kTestWindow, &kNativeClip));
    CheckThat(EqualRect(&currentClip, &initialClient));

    // The next request uses the live window geometry.
    clientOrigin = {20, 40};
    const RECT movedClient = {20, 40, 1300, 760};
    CheckThat(ApplyGameClip(kTestWindow, &kNativeClip));
    CheckThat(EqualRect(&currentClip, &movedClient));

    // The game releases the cursor before a window event and regains focus.
    CheckThat(ApplyGameClip(kTestWindow, nullptr));
    const unsigned int callsAfterRelease = clipCalls;
    HandleWindowChanged();
    CheckThat(HandleFocusGained());
    CheckThat(clipCalls == callsAfterRelease && EqualRect(&currentClip, &kDesktop));

    // A new native request enables confinement and normal Alt+Tab recovery.
    CheckThat(ApplyGameClip(kTestWindow, &kNativeClip));
    HandleFocusLost();
    CheckThat(EqualRect(&currentClip, &kDesktop));
    CheckThat(HandleFocusGained());
    CheckThat(EqualRect(&currentClip, &movedClient));

    foreground = nullptr;
    CheckThat(ApplyGameClip(kTestWindow, &kNativeClip));
    CheckThat(EqualRect(&currentClip, &kNativeClip));
}
