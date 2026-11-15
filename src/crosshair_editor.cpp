#include "crosshair_editor.h"

#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "crosshair_presets.h"
#include "crosshair_tga.h"
#include "pff_archive.h"
#include "player_config_resource.h"

namespace crosshair_editor {
namespace {
constexpr wchar_t kPreviewClass[] = L"BhdCrosshairSelectionPreview";
constexpr int kStyleComboId = 430;
constexpr int kColorComboId = 431;

HWND gPage = nullptr;
HWND gStyleLabel = nullptr;
HWND gStyleCombo = nullptr;
HWND gColorLabel = nullptr;
HWND gColorCombo = nullptr;
HWND gPreview = nullptr;
HWND gStatus = nullptr;
HFONT gFont = nullptr;
crosshair_tga::Image gSelectedImage{};
HBITMAP gBackgroundImage = nullptr;
ULONG_PTR gGdiplusToken = 0;

HWND MakeControl(const wchar_t* className, const wchar_t* text, DWORD style, int id) {
    HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                                   0, 0, 0, 0, gPage,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
    return control;
}

std::wstring AppPath(const wchar_t* fileName) {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return fileName;
    std::filesystem::path result(path);
    result.replace_filename(fileName);
    return result.wstring();
}

void SetStatus(const std::wstring& text) {
    SetWindowTextW(gStatus, text.c_str());
}

void DrawCrosshair(HDC dc, int x, int y) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 64;
    info.bmiHeader.biHeight = -64;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        return;
    }
    auto* destination = static_cast<uint8_t*>(bits);
    for (size_t i = 0; i < gSelectedImage.size(); ++i) {
        const auto& pixel = gSelectedImage[i];
        destination[i * 4] = static_cast<uint8_t>(pixel.blue * pixel.alpha / 255);
        destination[i * 4 + 1] = static_cast<uint8_t>(pixel.green * pixel.alpha / 255);
        destination[i * 4 + 2] = static_cast<uint8_t>(pixel.red * pixel.alpha / 255);
        destination[i * 4 + 3] = pixel.alpha;
    }
    HDC source = CreateCompatibleDC(dc);
    const auto old = SelectObject(source, bitmap);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, 64, 64, source, 0, 0, 64, 64, blend);
    SelectObject(source, old);
    DeleteDC(source);
    DeleteObject(bitmap);
}

void PaintPreview(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    if (gBackgroundImage) {
        BITMAP bitmap{};
        GetObjectW(gBackgroundImage, sizeof(bitmap), &bitmap);
        HDC source = CreateCompatibleDC(dc);
        const auto old = SelectObject(source, gBackgroundImage);
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, 0, 0, client.right, client.bottom,
                   source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
        SelectObject(source, old);
        DeleteDC(source);
    }
    DrawCrosshair(dc, (client.right - 64) / 2, (client.bottom - 64) / 2);
    EndPaint(window, &paint);
}

LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) { PaintPreview(window); return 0; }
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcW(window, message, wParam, lParam);
}

void LoadBackground() {
    HRSRC resource = FindResourceW(GetModuleHandleW(nullptr),
                                   MAKEINTRESOURCEW(IDR_CROSSHAIR_PREVIEW), RT_RCDATA);
    if (!resource) return;
    HGLOBAL loaded = LoadResource(GetModuleHandleW(nullptr), resource);
    const DWORD size = SizeofResource(GetModuleHandleW(nullptr), resource);
    const void* source = loaded ? LockResource(loaded) : nullptr;
    if (!source || size == 0) return;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return;
    void* destination = GlobalLock(memory);
    if (!destination) { GlobalFree(memory); return; }
    CopyMemory(destination, source, size);
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK) {
        GlobalFree(memory);
        return;
    }
    {
        Gdiplus::Bitmap image(stream);
        if (image.GetLastStatus() == Gdiplus::Ok)
            image.GetHBITMAP(Gdiplus::Color(255, 0, 0, 0), &gBackgroundImage);
    }
    stream->Release();
}

void UpdateSelectedFromCombos() {
    const int style = static_cast<int>(SendMessageW(gStyleCombo, CB_GETCURSEL, 0, 0));
    const int color = static_cast<int>(SendMessageW(gColorCombo, CB_GETCURSEL, 0, 0));
    if (style >= crosshair_presets::kStyleCount) return;
    EnableWindow(gColorCombo, TRUE);
    gSelectedImage = crosshair_presets::Generate(style, color);
    SetStatus(L"");
    InvalidateRect(gPreview, nullptr, FALSE);
}

void LayoutControls(HWND page) {
    RECT client{};
    GetClientRect(page, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int margin = 14;
    MoveWindow(gStyleLabel, margin, 15, 40, 22, TRUE);
    MoveWindow(gStyleCombo, margin + 44, 10, 205, 300, TRUE);
    MoveWindow(gColorLabel, margin + 267, 15, 40, 22, TRUE);
    MoveWindow(gColorCombo, margin + 311, 10, 150, 300, TRUE);

    const int availableHeight = std::max(180, height - 92);
    const int availableWidth = std::max(320, width - margin * 2);
    const int previewWidth = std::min(availableWidth, availableHeight * 16 / 9);
    const int previewHeight = previewWidth * 9 / 16;
    const int previewX = (width - previewWidth) / 2;
    MoveWindow(gPreview, previewX, 52, previewWidth, previewHeight, TRUE);
    MoveWindow(gStatus, margin, 58 + previewHeight, width - margin * 2, 22, TRUE);
}

LRESULT CALLBACK PageProc(HWND page, UINT message, WPARAM wParam, LPARAM lParam,
                          UINT_PTR, DWORD_PTR) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(page, &paint);
            FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
            EndPaint(page, &paint);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT client{};
            GetClientRect(page, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client,
                     GetSysColorBrush(COLOR_BTNFACE));
            return 1;
        }
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_BTNFACE));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        case WM_SIZE: LayoutControls(page); return 0;
        case WM_COMMAND:
            if ((LOWORD(wParam) == kStyleComboId || LOWORD(wParam) == kColorComboId) &&
                HIWORD(wParam) == CBN_SELCHANGE) {
                UpdateSelectedFromCombos();
                return 0;
            }
            break;
    }
    return DefSubclassProc(page, message, wParam, lParam);
}
}  // namespace

HWND Create(HWND parent, HFONT font, HFONT) {
    gFont = font;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gGdiplusToken, &input, nullptr);

    WNDCLASSEXW previewClass{sizeof(previewClass)};
    previewClass.lpfnWndProc = PreviewProc;
    previewClass.hInstance = GetModuleHandleW(nullptr);
    previewClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    previewClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    previewClass.lpszClassName = kPreviewClass;
    RegisterClassExW(&previewClass);

    gPage = CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
                            0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(gPage, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetWindowSubclass(gPage, PageProc, 2, 0);
    gStyleLabel = MakeControl(L"STATIC", L"Style:", SS_LEFT, 0);
    gStyleCombo = MakeControl(WC_COMBOBOXW, L"",
                              CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kStyleComboId);
    for (int style = 0; style < crosshair_presets::kStyleCount; ++style)
        SendMessageW(gStyleCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(crosshair_presets::StyleName(style)));
    SendMessageW(gStyleCombo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Custom / Current"));
    gColorLabel = MakeControl(L"STATIC", L"Color:", SS_LEFT, 0);
    gColorCombo = MakeControl(WC_COMBOBOXW, L"",
                              CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kColorComboId);
    for (int color = 0; color < crosshair_presets::kColorCount; ++color)
        SendMessageW(gColorCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(crosshair_presets::ColorName(color)));
    SendMessageW(gStyleCombo, CB_SETCURSEL, 3, 0);
    SendMessageW(gColorCombo, CB_SETCURSEL, 0, 0);
    gPreview = MakeControl(kPreviewClass, L"", WS_BORDER, 0);
    gStatus = MakeControl(L"STATIC", L"", SS_LEFT, 0);
    LoadBackground();
    gSelectedImage = crosshair_presets::Generate(3, 0);
    return gPage;
}

void LoadCurrentCrosshair() {
    std::vector<uint8_t> bytes;
    std::wstring error;
    crosshair_tga::Image loaded{};
    if (!pff_archive::LoadEntry(AppPath(L"localres.pff"), "coma20mm.tga", bytes, error) ||
        !crosshair_tga::Decode(bytes, loaded, error)) {
        SetStatus(error);
        MessageBoxW(gPage, error.c_str(), L"Load Current Crosshair", MB_OK | MB_ICONERROR);
        return;
    }
    gSelectedImage = loaded;
    int style = 0;
    int color = 0;
    if (crosshair_presets::FindMatch(loaded, style, color)) {
        SendMessageW(gStyleCombo, CB_SETCURSEL, style, 0);
        SendMessageW(gColorCombo, CB_SETCURSEL, color, 0);
        EnableWindow(gColorCombo, TRUE);
        SetStatus(L"Loaded the currently installed crosshair.");
    } else {
        SendMessageW(gStyleCombo, CB_SETCURSEL, crosshair_presets::kStyleCount, 0);
        EnableWindow(gColorCombo, FALSE);
        SetStatus(L"Loaded the currently installed custom crosshair.");
    }
    InvalidateRect(gPreview, nullptr, FALSE);
}

void SaveCrosshair() {
    std::wstring error;
    const auto bytes = crosshair_tga::Encode(gSelectedImage);
    if (!pff_archive::ReplaceEntry(AppPath(L"localres.pff"), "coma20mm.tga", bytes, error)) {
        SetStatus(error);
        MessageBoxW(gPage, error.c_str(), L"Apply Crosshair", MB_OK | MB_ICONERROR);
        return;
    }
    SetStatus(L"Applied the selected crosshair to localres.pff.");
    MessageBoxW(gPage, L"The selected crosshair was applied to localres.pff.",
                L"Crosshair applied", MB_OK | MB_ICONINFORMATION);
}

void Destroy() {
    if (gBackgroundImage) { DeleteObject(gBackgroundImage); gBackgroundImage = nullptr; }
    if (gGdiplusToken) { Gdiplus::GdiplusShutdown(gGdiplusToken); gGdiplusToken = 0; }
}

}  // namespace crosshair_editor
