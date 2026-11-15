#pragma once

#include <windows.h>

namespace hud_editor {

HWND Create(HWND parent, HFONT font, HFONT boldFont);
void Load(bool defaultsOnly = false);
void Save(HWND owner);

}  // namespace hud_editor
