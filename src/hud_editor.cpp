#include "hud_editor.h"

#include <commctrl.h>
#include <shlwapi.h>

namespace hud_editor {
namespace {
constexpr wchar_t kSection[] = L"PatchGroups";
constexpr int kFirstModeId = 5000;
const wchar_t* kNames[] = {
    L"Original",
    L"Original + pixel-perfect crosshair",
    L"Centered 4:3 HUD",
    L"Widescreen anchored HUD",
};
const wchar_t* kDescriptions[] = {
    L"Uses the game's original HUD and crosshair scaling.",
    L"Keeps the original HUD and draws the crosshair at its native pixel size.",
    L"Scales the 1024x768 HUD uniformly and centers it on widescreen displays.",
    L"Keeps HUD proportions while anchoring left, center and right groups across the screen.",
};

HWND g_page = nullptr;
HWND g_radios[4] = {};
HWND g_descriptions[4] = {};
HWND g_status = nullptr;
HFONT g_font = nullptr;
wchar_t g_iniPath[MAX_PATH] = {};

void Layout() {
    RECT rect{};
    GetClientRect(g_page, &rect);
    for (int i = 0; i < 4; ++i) {
        const int y = 24 + i * 74;
        MoveWindow(g_radios[i], 28, y, rect.right - 56, 22, TRUE);
        MoveWindow(g_descriptions[i], 52, y + 25, rect.right - 80, 38, TRUE);
    }
    MoveWindow(g_status, 28, 326, rect.right - 56, 22, TRUE);
}

LRESULT CALLBACK PageProc(HWND page, UINT message, WPARAM wParam, LPARAM lParam,
                          UINT_PTR, DWORD_PTR) {
    if (message == WM_SIZE) { Layout(); return 0; }
    return DefSubclassProc(page, message, wParam, lParam);
}

HWND Control(const wchar_t* className, const wchar_t* text, DWORD style, int id) {
    HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, g_page, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return control;
}
}  // namespace

HWND Create(HWND parent, HFONT font, HFONT boldFont) {
    g_font = font;
    g_page = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0,
                             parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(g_page, PageProc, 1, 0);
    for (int i = 0; i < 4; ++i) {
        g_radios[i] = Control(L"BUTTON", kNames[i],
            BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0), kFirstModeId + i);
        SendMessageW(g_radios[i], WM_SETFONT, reinterpret_cast<WPARAM>(boldFont), TRUE);
        g_descriptions[i] = Control(L"STATIC", kDescriptions[i], SS_LEFT, 0);
    }
    g_status = Control(L"STATIC", L"", SS_LEFT, 0);
    GetModuleFileNameW(nullptr, g_iniPath, MAX_PATH);
    PathRemoveFileSpecW(g_iniPath);
    PathAppendW(g_iniPath, L"bhd_player_config.ini");
    Load(false);
    Layout();
    return g_page;
}

void Load(bool defaultsOnly) {
    int mode = 1;
    if (!defaultsOnly) {
        mode = GetPrivateProfileIntW(kSection, L"HUDLayoutMode", -1, g_iniPath);
        if (mode < 0) mode = GetPrivateProfileIntW(kSection, L"PixelPerfectCrosshair", 1,
                                                   g_iniPath) ? 1 : 0;
    }
    if (mode < 0 || mode > 3) mode = 1;
    CheckRadioButton(g_page, kFirstModeId, kFirstModeId + 3, kFirstModeId + mode);
    SetWindowTextW(g_status, defaultsOnly ? L"Default mode selected. Click Save to write it."
                                         : L"HUD layout loaded.");
}

void Save(HWND owner) {
    int mode = 1;
    for (int i = 0; i < 4; ++i)
        if (IsDlgButtonChecked(g_page, kFirstModeId + i) == BST_CHECKED) mode = i;
    wchar_t value[2] = {static_cast<wchar_t>(L'0' + mode), L'\0'};
    const bool saved = WritePrivateProfileStringW(kSection, L"HUDLayoutMode", value, g_iniPath) &&
        WritePrivateProfileStringW(kSection, L"PixelPerfectCrosshair", nullptr, g_iniPath);
    if (saved) {
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_iniPath);
        SetWindowTextW(g_status, L"Saved. Restart the game to apply changes.");
    } else {
        MessageBoxW(owner, L"Could not save bhd_player_config.ini.", L"Save failed",
                    MB_OK | MB_ICONERROR);
    }
}

}  // namespace hud_editor
