// Undoable operations and the operation stock. Mirrors the BasePaint split
// where tools produce operations (CDrawStrokeOperation, FloodFillOperation,
// EraserOperation, SelectionOperation, ...) and a stock owns undo/redo.
// Evidence: REA ev_4c7ccd8f9c9b806... (BasePaint.dll exports),
// COperationStockImpl::BoundingRect.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "image.h"

namespace pr {

enum class OperationKind {
    SetPixel,
    FillRectangle,
    Stroke,
    FloodFill,
    Erase,
    Clear,
    Crop,
    Resize,
    Rotate,
    FlipHorizontal,
    FlipVertical,
    Skew,
    InvertColors,
    Selection,
    Text
};

// Rectangular pixel delta. Exact undo/redo without a full-image snapshot.
struct PixelPatch {
    Rect rect{};
    std::vector<std::uint32_t> before;
    std::vector<std::uint32_t> after;

    void CaptureBefore(const Image& image);
    void CaptureAfter(const Image& image);
    void Apply(Image& image) const;
    void Revert(Image& image) const;
    bool Empty() const { return rect.w <= 0 || rect.h <= 0 || after.empty(); }
};

class Operation {
public:
    virtual ~Operation() = default;
    virtual OperationKind Kind() const = 0;
    virtual void Apply(Image& image) = 0;
    virtual void Revert(Image& image) = 0;
    virtual const Rect& BoundingRect() const = 0;
};

// Patch-based operation for local drawing actions.
class PatchOperation : public Operation {
public:
    PatchOperation(OperationKind kind, PixelPatch patch);
    OperationKind Kind() const override { return kind_; }
    void Apply(Image& image) override { patch_.Apply(image); }
    void Revert(Image& image) override { patch_.Revert(image); }
    const Rect& BoundingRect() const override { return patch_.rect; }
    const PixelPatch& patch() const { return patch_; }

private:
    OperationKind kind_;
    PixelPatch patch_;
};

// Full-snapshot operation for whole-image transforms (crop/resize/rotate).
class SnapshotOperation : public Operation {
public:
    SnapshotOperation(OperationKind kind, std::unique_ptr<Image> before,
                      std::unique_ptr<Image> after);
    OperationKind Kind() const override { return kind_; }
    void Apply(Image& image) override;
    void Revert(Image& image) override;
    const Rect& BoundingRect() const override { return bounds_; }

private:
    OperationKind kind_;
    std::unique_ptr<Image> before_;
    std::unique_ptr<Image> after_;
    Rect bounds_{};
};

// Undo/redo stock with dirty tracking.
class COperationStock {
public:
    void Commit(std::unique_ptr<Operation> operation);
    bool Undo(Image& image);
    bool Redo(Image& image);
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    bool IsDirty() const { return undo_.size() != savedAt_; }
    void MarkSaved() { savedAt_ = undo_.size(); }
    void Clear();

private:
    std::deque<std::unique_ptr<Operation>> undo_;
    std::deque<std::unique_ptr<Operation>> redo_;
    std::size_t savedAt_ = 0;
};

// Convenience factories: capture "before" from the live image, then mutate.
std::unique_ptr<Operation> MakeSetPixelOperation(Image& image, const Point& p, Color c);
std::unique_ptr<Operation> MakeFillRectOperation(Image& image, const Rect& r, Color c);
std::unique_ptr<Operation> MakeClearOperation(Image& image, Color c);

}  // namespace pr
