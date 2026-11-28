#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "text_render.h"

namespace pr {
namespace {

struct Dib {
    std::vector<std::uint8_t> bytes;
    BITMAPINFOHEADER header{};
};

Dib MakeDib(const Image& image) {
    Dib dib;
    const std::size_t pixelBytes = static_cast<std::size_t>(image.width()) * image.height() * 4u;
    dib.bytes.resize(sizeof(BITMAPINFOHEADER) + pixelBytes);
    dib.header.biSize = sizeof(BITMAPINFOHEADER);
    dib.header.biWidth = image.width();
    dib.header.biHeight = image.height();  // positive => bottom-up
    dib.header.biPlanes = 1;
    dib.header.biBitCount = 32;
    dib.header.biCompression = BI_RGB;
    std::memcpy(dib.bytes.data(), &dib.header, sizeof(dib.header));
    std::uint8_t* rows = dib.bytes.data() + sizeof(BITMAPINFOHEADER);
    const std::size_t stride = static_cast<std::size_t>(image.width()) * 4u;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const Color c = image.GetPixel(Point{x, y});
            std::uint8_t* p = rows + static_cast<std::size_t>(image.height() - 1 - y) * stride +
                              static_cast<std::size_t>(x) * 4;
            p[0] = Blue(c);
            p[1] = Green(c);
            p[2] = Red(c);
            p[3] = Alpha(c);
        }
    }
    return dib;
}

}  // namespace

bool RenderTextIntoImage(Image& image, const Point& at, const std::wstring& text,
                         Color color, const TextFormat& format) {
    if (text.empty() || image.width() <= 0 || image.height() <= 0) return false;
    if (at.x < 0 || at.y < 0 || at.x >= image.width() || at.y >= image.height()) return false;
    const int fontHeightPx = std::max(6, format.heightPx);

    Dib dib = MakeDib(image);
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP section = CreateDIBSection(memory, reinterpret_cast<BITMAPINFO*>(&dib.header),
                                       DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memory || !section || !bits) {
        if (section) DeleteObject(section);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return false;
    }
    const std::size_t pixelBytes = static_cast<std::size_t>(image.width()) * image.height() * 4u;
    std::memcpy(bits, dib.bytes.data() + sizeof(BITMAPINFOHEADER), pixelBytes);

    HFONT font = CreateFontW(fontHeightPx, 0, 0, 0,
                             format.bold ? FW_BOLD : FW_NORMAL,
                             format.italic ? TRUE : FALSE,
                             format.underline ? TRUE : FALSE,
                             format.strikeOut ? TRUE : FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FF_DONTCARE,
                             format.fontFamily.empty() ? L"Segoe UI" : format.fontFamily.c_str());
    const HGDIOBJ oldFont = SelectObject(memory, font);
    SetTextColor(memory, RGB(Red(color), Green(color), Blue(color)));
    if (format.opaqueBackground) {
        SetBkMode(memory, OPAQUE);
        SetBkColor(memory, RGB(Red(format.background), Green(format.background),
                               Blue(format.background)));
    } else {
        SetBkMode(memory, TRANSPARENT);
    }
    RECT rect{at.x, at.y, std::min(image.width(), at.x + 1024),
              std::min(image.height(), at.y + fontHeightPx * 8)};
    DrawTextW(memory, text.c_str(), static_cast<int>(text.size()), &rect,
              DT_LEFT | DT_TOP | DT_NOPREFIX);
    SelectObject(memory, oldFont);
    DeleteObject(font);

    std::memcpy(dib.bytes.data() + sizeof(BITMAPINFOHEADER), bits, pixelBytes);
    const std::uint8_t* rows = dib.bytes.data() + sizeof(BITMAPINFOHEADER);
    const std::size_t stride = static_cast<std::size_t>(image.width()) * 4u;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const std::uint8_t* p = rows + static_cast<std::size_t>(image.height() - 1 - y) * stride +
                                    static_cast<std::size_t>(x) * 4;
            image.SetPixel(Point{x, y}, MakeColor(p[2], p[1], p[0], p[3]));
        }
    }
    DeleteObject(section);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return true;
}

}  // namespace pr
