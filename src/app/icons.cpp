#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

#include "../core/codec.h"
#include "icons.h"

#pragma comment(lib, "msimg32.lib")

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

void PremultiplyAlpha(Image& img) {
    uint8_t* p = reinterpret_cast<uint8_t*>(img.Bitmap());
    const std::size_t n = static_cast<std::size_t>(img.width()) * img.height();
    for (std::size_t i = 0; i < n; ++i, p += 4) {
        const uint8_t a = p[3];
        if (a == 255) continue;
        if (a == 0) { p[0] = p[1] = p[2] = 0; continue; }
        p[0] = static_cast<uint8_t>((p[0] * a + 127) / 255);
        p[1] = static_cast<uint8_t>((p[1] * a + 127) / 255);
        p[2] = static_cast<uint8_t>((p[2] * a + 127) / 255);
    }
}

}  // namespace

Image* GetIcon(const char* fileName) {
    if (!fileName) return nullptr;
    static std::unordered_map<std::string, std::unique_ptr<Image>> cache;
    static std::wstring directory = AssetDirectory();
    auto it = cache.find(fileName);
    if (it != cache.end()) return it->second.get();
    if (directory.empty()) { cache[fileName] = nullptr; return nullptr; }
    std::wstring path = directory + L"\\";
    for (const char* c = fileName; *c; ++c) path.push_back(static_cast<wchar_t>(*c));
    std::unique_ptr<Image> img = LoadImageWic(path);
    if (img) PremultiplyAlpha(*img);
    Image* raw = img.get();
    cache[fileName] = std::move(img);
    return raw;
}

Image* GetToolIcon(int index) {
    if (index < 0 || index >= 18) return nullptr;
    return GetIcon(kToolIconFiles[index]);
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
    HDC mem = CreateCompatibleDC(hdc);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { if (dib) DeleteObject(dib); DeleteDC(mem); return; }
    const std::size_t bytes = static_cast<std::size_t>(icon.width()) * icon.height() * 4;
    std::copy(icon.Bitmap(), icon.Bitmap() + bytes, static_cast<uint8_t*>(bits));
    HGDIOBJ old = SelectObject(mem, dib);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(hdc, x, y, size, size, mem, 0, 0, icon.width(), icon.height(), bf);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
}

}  // namespace pr
