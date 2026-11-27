#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "print_win.h"

namespace {

HWND g_preview = nullptr;
pr::Image* g_previewImage = nullptr;

void DrawImageToDC(HDC hdc, const pr::Image& image, int x, int y, int w, int h) {
    if (image.width() <= 0 || image.height() <= 0 || w <= 0 || h <= 0) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width();
    info.bmiHeader.biHeight = -image.height();  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(hdc, x, y, w, h, 0, 0, image.width(), image.height(), image.Bitmap(), &info,
                  DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK PreviewProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            FillRect(hdc, &client, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            const int margin = 24;
            const RECT page{margin, margin, client.right - margin, client.bottom - margin};
            FillRect(hdc, const_cast<RECT*>(&page),
                     static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            Rectangle(hdc, page.left, page.top, page.right, page.bottom);
            if (g_previewImage) {
                const int pw = page.right - page.left - 40;
                const int ph = page.bottom - page.top - 40;
                const double scale = std::min(static_cast<double>(pw) / g_previewImage->width(),
                                              static_cast<double>(ph) / g_previewImage->height());
                const int w = std::max(1, static_cast<int>(g_previewImage->width() * scale));
                const int h = std::max(1, static_cast<int>(g_previewImage->height() * scale));
                DrawImageToDC(hdc, *g_previewImage, page.left + 20, page.top + 20, w, h);
            }
            const wchar_t* label = L"Page 1 of 1";
            TextOutW(hdc, (client.right - 80) / 2, client.bottom - 18, label,
                     static_cast<int>(wcslen(label)));
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            g_preview = nullptr;
            g_previewImage = nullptr;
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

}  // namespace

bool PrintImage(HWND owner, const pr::Image& image) {
    PRINTDLGW print{};
    print.lStructSize = sizeof(print);
    print.hwndOwner = owner;
    print.Flags = PD_RETURNDC;
    if (!PrintDlgW(&print) || !print.hDC) return false;
    const HDC hdc = print.hDC;
    const int pageW = GetDeviceCaps(hdc, HORZRES);
    const int pageH = GetDeviceCaps(hdc, VERTRES);
    const int marginX = std::max(GetDeviceCaps(hdc, LOGPIXELSX) / 4, 1);
    const int marginY = std::max(GetDeviceCaps(hdc, LOGPIXELSY) / 4, 1);
    const int availW = std::max(1, pageW - 2 * marginX);
    const int availH = std::max(1, pageH - 2 * marginY);
    const double scale = std::min(static_cast<double>(availW) / image.width(),
                                  static_cast<double>(availH) / image.height());
    const int w = std::max(1, static_cast<int>(image.width() * scale));
    const int h = std::max(1, static_cast<int>(image.height() * scale));

    DOCINFOW doc{};
    doc.lpszDocName = L"PaintRebuild";
    if (StartDocW(hdc, &doc) <= 0) {
        if (print.hDevMode) GlobalFree(print.hDevMode);
        if (print.hDevNames) GlobalFree(print.hDevNames);
        DeleteDC(hdc);
        return false;
    }
    StartPage(hdc);
    DrawImageToDC(hdc, image, (pageW - w) / 2, (pageH - h) / 2, w, h);
    EndPage(hdc);
    EndDoc(hdc);
    if (print.hDevMode) GlobalFree(print.hDevMode);
    if (print.hDevNames) GlobalFree(print.hDevNames);
    DeleteDC(hdc);
    return true;
}

void TogglePrintPreview(HWND owner, pr::Image* image) {
    if (g_preview) {
        DestroyWindow(g_preview);
        g_preview = nullptr;
        g_previewImage = nullptr;
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PreviewProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"PaintRebuildPrintPreview";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }
    RECT ownerRect;
    GetWindowRect(owner, &ownerRect);
    g_previewImage = image;
    g_preview = CreateWindowExW(WS_EX_TOOLWINDOW, L"PaintRebuildPrintPreview", L"Print Preview",
                                WS_OVERLAPPEDWINDOW, ownerRect.left + 40, ownerRect.top + 40,
                                640, 520, owner, nullptr, GetModuleHandle(nullptr), nullptr);
    if (g_preview) ShowWindow(g_preview, SW_SHOW);
}

void UpdatePrintPreviewImage(pr::Image* image) {
    g_previewImage = image;
    if (g_preview) InvalidateRect(g_preview, nullptr, TRUE);
}

bool IsPrintPreviewVisible() { return g_preview != nullptr; }
