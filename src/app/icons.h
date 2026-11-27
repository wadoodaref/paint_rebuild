// Source-derived tool-box icons (PNG rendered from the vendored SVG assets).
#pragma once

#include <windows.h>

#include "../core/image.h"

namespace pr {

constexpr const char* kToolIconFiles[18] = {
    "select.png", "freeform.png", "eraser.png", "fill.png", "picker.png", "zoom.png",
    "pencil.png", "brush.png", "airbrush.png", "calligraphy.png", "text.png", "line.png",
    "curve.png", "polygon.png", "rectangle.png", "oval.png", "rounded_rect.png", "tools.png"};

// Lazily loads and caches the icon; returns nullptr when the asset is absent.
Image* GetToolIcon(int index);
void DrawToolIcon(HDC hdc, const Image& icon, int x, int y, int size);

}  // namespace pr
