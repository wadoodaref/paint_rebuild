// Selection clipboard. Headless counterpart of the copy/cut/paste workflow;
// the Win32 stage maps this to the system clipboard in CF_DIB/CF_BITMAP form.
#pragma once

#include <memory>

#include "image.h"

namespace pr {

class SelectionClipboard {
public:
    void Copy(const Image& source, const Rect& rect) {
        const Rect r = Intersect(NormalizeRect(rect), Rect{0, 0, source.width(), source.height()});
        if (Empty(r)) return;
        content_ = Image::Create(Size{r.w, r.h});
        for (int y = 0; y < r.h; ++y) {
            for (int x = 0; x < r.w; ++x)
                content_->SetPixel(Point{x, y}, source.GetPixel(Point{r.x + x, r.y + y}));
        }
        rect_ = r;
    }

    void Cut(Image& source, const Rect& rect, Color background) {
        Copy(source, rect);
        if (HasContent()) source.FillRect(rect_, background);
    }

    bool Paste(Image& destination, const Point& at, bool transparent = false, Color key = Color{0}) const {
        if (!content_) return false;
        destination.Blit(*content_, at, transparent, key);
        return true;
    }

    bool HasContent() const { return content_ != nullptr; }
    const Image& Content() const { return *content_; }
    Rect SourceRect() const { return rect_; }
    void Clear() { content_.reset(); rect_ = Rect{}; }

private:
    std::unique_ptr<Image> content_;
    Rect rect_{};
};

}  // namespace pr
