#pragma once

#include <windows.h>

namespace crosshair_editor {

HWND Create(HWND parent, HFONT font, HFONT boldFont);
void LoadCurrentCrosshair();
void SaveCrosshair();
void Destroy();

}  // namespace crosshair_editor
