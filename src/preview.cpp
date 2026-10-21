#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <memory>
#include <string>
#include "companion/localization.h"

// Standalone UI prototype. No injection, hooking, Vulkan calls, or game access.
namespace {
constexpr int kLanguage = 101;
constexpr int kCoordinates = 102;
constexpr int kMinimap = 103;
constexpr int kSearch = 104;
constexpr int kStatus = 105;
constexpr int kTitle = 106;
constexpr int kLanguageLabel = 107;
constexpr int kSearchLabel = 108;
constexpr int kMapLabel = 109;
std::unique_ptr<companion::Localization> locale;
HWND window = nullptr;
HWND language = nullptr;
HWND coordinates = nullptr;
HWND minimap = nullptr;
HWND search = nullptr;
HWND status = nullptr;
HWND title = nullptr;
HWND language_label = nullptr;
HWND search_label = nullptr;
HWND map_label = nullptr;
HFONT ui_font = nullptr;

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                   static_cast<int>(value.size()), nullptr, 0);
    if (!size) return L"[invalid UTF-8]";
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}
std::wstring tr(const char* key) { return wide(locale->get(key)); }
HWND control(const wchar_t* kind, const wchar_t* value, DWORD style,
             int x, int y, int w, int h, int id) {
    HWND child = CreateWindowExW(0, kind, value, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
    return child;
}
void update_labels() {
    SetWindowTextW(window, tr("app.title").c_str());
    SetWindowTextW(title, tr("app.title").c_str());
    SetWindowTextW(language_label, tr("settings.language").c_str());
    SetWindowTextW(map_label, tr("map.title").c_str());
    SetWindowTextW(coordinates, tr("map.coordinates").c_str());
    SetWindowTextW(minimap, tr("map.minimap").c_str());
    SetWindowTextW(search_label, tr("map.search").c_str());
    SetWindowTextW(status, L"Preview only - not connected to Enshrouded");
}
LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: {
        window = hwnd;
        ui_font = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        title = control(L"STATIC", L"", 0, 24, 18, 490, 34, kTitle);
        language_label = control(L"STATIC", L"", 0, 24, 69, 110, 28, kLanguageLabel);
        language = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                           150, 64, 230, 150, kLanguage);
        SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English (US)"));
        SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Portugu\u00EAs (Brasil)"));
        SendMessageW(language, CB_SETCURSEL, locale->language() == "pt-BR" ? 1 : 0, 0);
        map_label = control(L"STATIC", L"", 0, 24, 118, 250, 28, kMapLabel);
        coordinates = control(L"BUTTON", L"", BS_AUTOCHECKBOX,
                              24, 156, 370, 28, kCoordinates);
        minimap = control(L"BUTTON", L"", BS_AUTOCHECKBOX,
                          24, 192, 370, 28, kMinimap);
        search_label = control(L"STATIC", L"", 0, 24, 244, 340, 28, kSearchLabel);
        search = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL,
                         24, 277, 480, 32, kSearch);
        status = control(L"STATIC", L"", 0, 24, 342, 490, 42, kStatus);
        update_labels();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == kLanguage && HIWORD(wp) == CBN_SELCHANGE) {
            int index = static_cast<int>(SendMessageW(language, CB_GETCURSEL, 0, 0));
            locale->set_language(index == 1 ? "pt-BR" : "en-US");
            update_labels();
        }
        return 0;
    case WM_DESTROY:
        if (ui_font) DeleteObject(ui_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    wchar_t exe_path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return 1;
    locale = std::make_unique<companion::Localization>(
        std::filesystem::path(exe_path).parent_path() / L"locales");
    locale->set_language(companion::Localization::system_language());
    WNDCLASSW klass = {};
    klass.lpfnWndProc = window_proc;
    klass.hInstance = instance;
    klass.lpszClassName = L"EmbervaleCompanionPreviewWindow";
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&klass)) return 2;
    HWND hwnd = CreateWindowExW(0, klass.lpszClassName, L"Embervale Companion",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        560, 450, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 3;
    ShowWindow(hwnd, show);
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
