#define NOMINMAX
#include <windows.h>
#include <winreg.h>

#include <algorithm>
#include <cwchar>
#include <string>

#include "../core/codec.h"
#include "extra_ui.h"

namespace {

bool g_fullScreen = false;
RECT g_savedWindow{};
LONG_PTR g_savedStyle = 0;

HWND g_thumbnail = nullptr;
pr::Image* g_thumbnailImage = nullptr;

LRESULT CALLBACK ThumbnailProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            FillRect(hdc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            if (g_thumbnailImage) {
                const int w = g_thumbnailImage->width();
                const int h = g_thumbnailImage->height();
                if (w > 0 && h > 0) {
                    const double scale = std::min(static_cast<double>(client.right) / w,
                                                  static_cast<double>(client.bottom) / h);
                    const int tw = std::max(1, static_cast<int>(w * scale));
                    const int th = std::max(1, static_cast<int>(h * scale));
                    BITMAPINFO info{};
                    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biWidth = w;
                    info.bmiHeader.biHeight = -h;  // top-down
                    info.bmiHeader.biPlanes = 1;
                    info.bmiHeader.biBitCount = 32;
                    info.bmiHeader.biCompression = BI_RGB;
                    StretchDIBits(hdc, (client.right - tw) / 2, (client.bottom - th) / 2, tw, th,
                                  0, 0, w, h, g_thumbnailImage->Bitmap(), &info,
                                  DIB_RGB_COLORS, SRCCOPY);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            g_thumbnail = nullptr;
            g_thumbnailImage = nullptr;
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

}  // namespace

bool SetAsWallpaper(pr::Image* image, bool tiled) {
    if (!image) return false;
    wchar_t localAppData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) return false;
    const std::wstring directory = std::wstring(localAppData) + L"\\PaintRebuild";
    CreateDirectoryW(directory.c_str(), nullptr);
    const std::wstring bmp = directory + L"\\wallpaper.bmp";
    if (!pr::SaveBmp(*image, bmp)) return false;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_SET_VALUE, &key) ==
        ERROR_SUCCESS) {
        const wchar_t* style = tiled ? L"0" : L"10";
        const wchar_t* tile = tiled ? L"1" : L"0";
        RegSetValueExW(key, L"WallpaperStyle", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(style),
                       static_cast<DWORD>((wcslen(style) + 1) * sizeof(wchar_t)));
        RegSetValueExW(key, L"TileWallpaper", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(tile),
                       static_cast<DWORD>((wcslen(tile) + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
    return SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(bmp.c_str()),
                                 SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) != FALSE;
}

void ToggleFullScreen(HWND hwnd) {
    if (!g_fullScreen) {
        GetWindowRect(hwnd, &g_savedWindow);
        g_savedStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, nullptr, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                     GetSystemMetrics(SM_CYSCREEN), SWP_FRAMECHANGED);
        g_fullScreen = true;
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, g_savedStyle);
        SetWindowPos(hwnd, nullptr, g_savedWindow.left, g_savedWindow.top,
                     g_savedWindow.right - g_savedWindow.left,
                     g_savedWindow.bottom - g_savedWindow.top, SWP_FRAMECHANGED);
        g_fullScreen = false;
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}

bool IsFullScreen() { return g_fullScreen; }

void ToggleThumbnail(HWND owner, pr::Image* image) {
    if (g_thumbnail) {
        DestroyWindow(g_thumbnail);
        g_thumbnail = nullptr;
        g_thumbnailImage = nullptr;
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ThumbnailProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"PaintRebuildThumbnail";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }
    RECT ownerRect;
    GetWindowRect(owner, &ownerRect);
    g_thumbnailImage = image;
    g_thumbnail = CreateWindowExW(WS_EX_TOOLWINDOW, L"PaintRebuildThumbnail", L"Thumbnail",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  std::max(0, static_cast<int>(ownerRect.right) - 280),
                                  std::max(0, static_cast<int>(ownerRect.bottom) - 240),
                                  260, 200, owner, nullptr, GetModuleHandle(nullptr), nullptr);
    if (g_thumbnail) ShowWindow(g_thumbnail, SW_SHOWNOACTIVATE);
}

void UpdateThumbnailImage(pr::Image* image) {
    g_thumbnailImage = image;
    if (g_thumbnail) InvalidateRect(g_thumbnail, nullptr, TRUE);
}

bool IsThumbnailVisible() { return g_thumbnail != nullptr; }
