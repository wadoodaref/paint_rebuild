// GDI text rendering into the image model (TextTool commit).
#pragma once

#include <string>

#include "../core/image.h"

namespace pr {

// Faithful counterpart of the Paint 11 Text command bar controls: typeface,
// size, B/I/U/S styles and the opaque/transparent background toggle.
struct TextFormat {
    std::wstring fontFamily = L"Segoe UI";
    int heightPx = 16;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeOut = false;
    bool opaqueBackground = false;
    Color background = Color{};  // used only when opaqueBackground is true
};

bool RenderTextIntoImage(Image& image, const Point& at, const std::wstring& text,
                         Color color, const TextFormat& format);

}  // namespace pr
