#define NOMINMAX
#include <windows.h>

#include <memory>
#include <string>

#include "../core/codec.h"
#include "icons.h"

namespace pr {
namespace {

std::wstring AssetDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return std::wstring();
    std::wstring exe(path);
    const std::size_t slash = exe.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    return exe.substr(0, slash) + L"\\assets\\icons";
}

}  // namespace

Image* GetToolIcon(int index) {
    if (index < 0 || index >= 18) return nullptr;
    static std::unique_ptr<Image> icons[18];
    static std::wstring directory = AssetDirectory();
    if (!icons[index] && !directory.empty()) {
        std::wstring path = directory + L"\\";
        const char* file = kToolIconFiles[index];
        for (; *file; ++file) path.push_back(static_cast<wchar_t>(*file));
        icons[index] = LoadImageWic(path);
    }
    return icons[index].get();
}

void DrawToolIcon(HDC hdc, const Image& icon, int x, int y, int size) {
    if (icon.width() <= 0 || icon.height() <= 0) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = icon.width();
    info.bmiHeader.biHeight = -icon.height();  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(hdc, x, y, size, size, 0, 0, icon.width(), icon.height(), icon.Bitmap(), &info,
                  DIB_RGB_COLORS, SRCCOPY);
}

}  // namespace pr
