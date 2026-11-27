#include "operation.h"

#include <algorithm>
#include <utility>

namespace pr {

void PixelPatch::CaptureBefore(const Image& image) {
    rect = Intersect(NormalizeRect(rect), Rect{0, 0, image.width(), image.height()});
    if (pr::Empty(rect)) {
        before.clear();
        return;
    }
    before.resize(static_cast<std::size_t>(rect.w) * rect.h);
    for (int y = 0; y < rect.h; ++y) {
        for (int x = 0; x < rect.w; ++x) {
            before[static_cast<std::size_t>(y) * rect.w + x] =
                image.GetPixel(Point{rect.x + x, rect.y + y}).argb;
        }
    }
}

void PixelPatch::CaptureAfter(const Image& image) {
    if (pr::Empty(rect)) return;
    after.resize(static_cast<std::size_t>(rect.w) * rect.h);
    for (int y = 0; y < rect.h; ++y) {
        for (int x = 0; x < rect.w; ++x) {
            after[static_cast<std::size_t>(y) * rect.w + x] =
                image.GetPixel(Point{rect.x + x, rect.y + y}).argb;
        }
    }
}

void PixelPatch::Apply(Image& image) const {
    if (pr::Empty(rect)) return;
    for (int y = 0; y < rect.h; ++y) {
        for (int x = 0; x < rect.w; ++x) {
            image.SetPixel(Point{rect.x + x, rect.y + y},
                           Color{after[static_cast<std::size_t>(y) * rect.w + x]});
        }
    }
}

void PixelPatch::Revert(Image& image) const {
    if (before.empty()) return;
    for (int y = 0; y < rect.h; ++y) {
        for (int x = 0; x < rect.w; ++x) {
            image.SetPixel(Point{rect.x + x, rect.y + y},
                           Color{before[static_cast<std::size_t>(y) * rect.w + x]});
        }
    }
}

PatchOperation::PatchOperation(OperationKind kind, PixelPatch patch)
    : kind_(kind), patch_(std::move(patch)) {}

SnapshotOperation::SnapshotOperation(OperationKind kind,
                                     std::unique_ptr<Image> before,
                                     std::unique_ptr<Image> after)
    : kind_(kind), before_(std::move(before)), after_(std::move(after)) {
    if (after_) bounds_ = Rect{0, 0, after_->width(), after_->height()};
}

void SnapshotOperation::Apply(Image& image) {
    if (after_) image = *after_;
}

void SnapshotOperation::Revert(Image& image) {
    if (before_) image = *before_;
}

void COperationStock::Commit(std::unique_ptr<Operation> operation) {
    if (!operation) return;
    undo_.push_back(std::move(operation));
    redo_.clear();
}

bool COperationStock::Undo(Image& image) {
    if (undo_.empty()) return false;
    std::unique_ptr<Operation> op = std::move(undo_.back());
    undo_.pop_back();
    op->Revert(image);
    redo_.push_back(std::move(op));
    return true;
}

bool COperationStock::Redo(Image& image) {
    if (redo_.empty()) return false;
    std::unique_ptr<Operation> op = std::move(redo_.back());
    redo_.pop_back();
    op->Apply(image);
    undo_.push_back(std::move(op));
    return true;
}

void COperationStock::Clear() {
    undo_.clear();
    redo_.clear();
    savedAt_ = 0;
}

std::unique_ptr<Operation> MakeSetPixelOperation(Image& image, const Point& p, Color c) {
    PixelPatch patch;
    patch.rect = Rect{p.x, p.y, 1, 1};
    patch.CaptureBefore(image);
    image.SetPixel(p, c);
    patch.CaptureAfter(image);
    return std::make_unique<PatchOperation>(OperationKind::SetPixel, std::move(patch));
}

std::unique_ptr<Operation> MakeFillRectOperation(Image& image, const Rect& r, Color c) {
    PixelPatch patch;
    patch.rect = r;
    patch.CaptureBefore(image);
    image.FillRect(r, c);
    patch.CaptureAfter(image);
    return std::make_unique<PatchOperation>(OperationKind::FillRectangle, std::move(patch));
}

std::unique_ptr<Operation> MakeClearOperation(Image& image, Color c) {
    PixelPatch patch;
    patch.rect = Rect{0, 0, image.width(), image.height()};
    patch.CaptureBefore(image);
    image.Clear(c);
    patch.CaptureAfter(image);
    return std::make_unique<PatchOperation>(OperationKind::Clear, std::move(patch));
}

}  // namespace pr
