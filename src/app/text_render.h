// GDI text rendering into the image model (TextTool commit).
#pragma once

#include <string>

#include "../core/image.h"

namespace pr {

bool RenderTextIntoImage(Image& image, const Point& at, const std::wstring& text,
                         Color color, int fontHeightPx);

}  // namespace pr
