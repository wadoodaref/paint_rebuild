#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "clipboard_win.h"

namespace {

struct DibInfo {
    std::size_t size = 0;
    BITMAPINFOHEADER header{};
    const std::uint8_t* pixels = nullptr;
};

// BI_RGB 32bpp; B,G,R,A byte order matches our ARGB uint32 in memory.

bool ParseDib(const void* data, std::size_t bytes, DibInfo& out) {
    if (!data || bytes < sizeof(BITMAPINFOHEADER)) return false;
    BITMAPINFOHEADER header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biWidth <= 0 || header.biPlanes != 1)
        return false;
    const bool topDown = header.biHeight < 0;
    const int width = static_cast<int>(header.biWidth);
    const int height = topDown ? -static_cast<int>(header.biHeight) : static_cast<int>(header.biHeight);
    if (height <= 0 || width > 65536 || height > 65536) return false;
    const int bpp = header.biBitCount;
    if (bpp != 24 && bpp != 32) return false;
    const std::size_t stride = ((static_cast<std::size_t>(width) * bpp + 31u) / 32u) * 4u;
    const std::size_t offset = (header.biSize >= 40) ? header.biSize : 40;
    if (offset + stride * static_cast<std::size_t>(height) > bytes) return false;
    out.size = bytes;
    out.header = header;
    out.pixels = static_cast<const std::uint8_t*>(data) + offset;
    return true;
}

}  // namespace

bool CopyImageToClipboard(void* hwnd, const pr::Image& image, const pr::Rect& selection) {
    const pr::Rect r = pr::Intersect(pr::NormalizeRect(selection),
                                     pr::Rect{0, 0, image.width(), image.height()});
    if (pr::Empty(r)) return false;
    const std::size_t stride = static_cast<std::size_t>(r.w) * 4u;
    const std::size_t bytes = sizeof(BITMAPINFOHEADER) + stride * r.h;
    HANDLE memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    auto* raw = static_cast<std::uint8_t*>(GlobalLock(memory));
    if (!raw) {
        GlobalFree(memory);
        return false;
    }
    BITMAPINFOHEADER header{};
    header.biSize = sizeof(header);
    header.biWidth = r.w;
    header.biHeight = r.h;  // positive => bottom-up rows
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    header.biSizeImage = static_cast<DWORD>(stride * r.h);
    std::memcpy(raw, &header, sizeof(header));
    std::uint8_t* rows = raw + sizeof(header);
    for (int y = 0; y < r.h; ++y) {
        for (int x = 0; x < r.w; ++x) {
            const pr::Color c = image.GetPixel(pr::Point{r.x + x, r.y + y});
            std::uint8_t* p = rows + static_cast<std::size_t>(r.h - 1 - y) * stride +
                              static_cast<std::size_t>(x) * 4;
            p[0] = pr::Blue(c);
            p[1] = pr::Green(c);
            p[2] = pr::Red(c);
            p[3] = pr::Alpha(c);
        }
    }
    GlobalUnlock(memory);
    if (!OpenClipboard(static_cast<HWND>(hwnd))) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    HANDLE result = SetClipboardData(CF_DIB, memory);
    if (!result) GlobalFree(memory);
    CloseClipboard();
    return result != nullptr;
}

std::unique_ptr<pr::Image> PasteImageFromClipboard() {
    if (!IsClipboardFormatAvailable(CF_DIB) || !OpenClipboard(nullptr)) return nullptr;
    HANDLE data = GetClipboardData(CF_DIB);
    if (!data) {
        CloseClipboard();
        return nullptr;
    }
    void* locked = GlobalLock(data);
    std::size_t bytes = GlobalSize(data);
    DibInfo dib;
    std::unique_ptr<pr::Image> result;
    if (ParseDib(locked, bytes, dib)) {
        const bool topDown = dib.header.biHeight < 0;
        const int w = static_cast<int>(dib.header.biWidth);
        const int h = topDown ? -static_cast<int>(dib.header.biHeight) : dib.header.biHeight;
        const std::size_t stride = ((static_cast<std::size_t>(w) * dib.header.biBitCount + 31u) / 32u) * 4u;
        result = pr::Image::Create(pr::Size{w, h});
        for (int y = 0; y < h; ++y) {
            const int sourceRow = topDown ? y : h - 1 - y;
            for (int x = 0; x < w; ++x) {
                const std::uint8_t* p = dib.pixels + static_cast<std::size_t>(sourceRow) * stride +
                                        static_cast<std::size_t>(x) * (dib.header.biBitCount / 8u);
                const std::uint8_t a = (dib.header.biBitCount == 32) ? p[3] : 255;
                result->SetPixel(pr::Point{x, y}, pr::MakeColor(p[2], p[1], p[0], a));
            }
        }
    }
    GlobalUnlock(data);
    CloseClipboard();
    return result;
}
