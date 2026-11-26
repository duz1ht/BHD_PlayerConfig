#pragma once

#include <windows.h>

namespace cursor_clip {

void InstallGameHook();
void Initialize(bool enabled, HWND window);
void HandleFocusLost();
bool HandleFocusGained();
void HandleWindowChanged();
void Shutdown();
}  // namespace cursor_clip
