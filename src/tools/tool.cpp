#include "tool.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <queue>
#include <vector>

namespace pr {

void CPaintTool::OnCancel() {
    if (drawing_) RestoreOriginal();
    drawing_ = false;
    modified_ = false;
}

void CPaintTool::BeginStroke() {
    patch_ = PixelPatch{};
    patch_.rect = Rect{0, 0, context_.image.width(), context_.image.height()};
    patch_.CaptureBefore(context_.image);
    drawing_ = true;
    modified_ = false;
}

void CPaintTool::RestoreOriginal() {
    patch_.Revert(context_.image);
}

void CPaintTool::EndStroke(OperationKind kind) {
    patch_.CaptureAfter(context_.image);
    if (!patch_.Empty()) {
        context_.stock.Commit(std::make_unique<PatchOperation>(kind, std::move(patch_)));
        modified_ = true;
    }
    drawing_ = false;
}

CStrokeTool::CStrokeTool(ToolContext& context, PaintToolKind kind, bool useBackground,
                         bool square, bool airbrush)
    : CPaintTool(context, kind), useBackground_(useBackground), square_(square),
      airbrush_(airbrush) {}

void CStrokeTool::Stamp(const Point& p) {
    const int radius = static_cast<int>(std::max(1u, thickness_) / 2u);
    Color c = useBackground_ ? context_.background : context_.foreground;
    if (airbrush_) c = WithAlpha(c, static_cast<std::uint8_t>(255 * opacity_));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (!square_ && dx * dx + dy * dy > radius * radius + radius) continue;
            context_.image.SetPixel(Point{p.x + dx, p.y + dy}, c);
        }
    }
}

void CStrokeTool::DrawSegment(const Point& from, const Point& to) {
    int x0 = from.x;
    int y0 = from.y;
    const int x1 = to.x;
    const int y1 = to.y;
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        Stamp(Point{x0, y0});
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void CStrokeTool::OnStylusDown(const Point& p) {
    BeginStroke();
    start_ = last_ = p;
    Stamp(p);
}

void CStrokeTool::OnStylusMove(const Point& p) {
    if (!drawing_) return;
    DrawSegment(last_, p);
    last_ = p;
}

void CStrokeTool::OnStylusUp(const Point& p) {
    if (!drawing_) return;
    DrawSegment(last_, p);
    EndStroke(OperationKind::Stroke);
}

namespace {
int ColorDistance(Color a, Color b) {
    return std::abs(static_cast<int>(Red(a)) - static_cast<int>(Red(b))) +
           std::abs(static_cast<int>(Green(a)) - static_cast<int>(Green(b))) +
           std::abs(static_cast<int>(Blue(a)) - static_cast<int>(Blue(b)));
}
}  // namespace

void FloodTool::OnStylusDown(const Point& p) {
    if (p.x < 0 || p.y < 0 || p.x >= context_.image.width() || p.y >= context_.image.height())
        return;
    BeginStroke();
    const Color target = context_.image.GetPixel(p);
    const Color replacement = context_.foreground;
    const int limit = static_cast<int>(std::lround(tolerance_));
    if (ColorDistance(target, replacement) == 0) {
        drawing_ = false;
        return;
    }
    std::vector<std::uint8_t> visited(
        static_cast<std::size_t>(context_.image.width()) * context_.image.height(), 0);
    std::queue<Point> pending;
    pending.push(p);
    visited[static_cast<std::size_t>(p.y) * context_.image.width() + p.x] = 1;
    while (!pending.empty()) {
        const Point current = pending.front();
        pending.pop();
        context_.image.SetPixel(current, replacement);
        const Point neighbours[4] = {
            Point{current.x + 1, current.y}, Point{current.x - 1, current.y},
            Point{current.x, current.y + 1}, Point{current.x, current.y - 1}};
        for (const auto& n : neighbours) {
            if (n.x < 0 || n.y < 0 || n.x >= context_.image.width() ||
                n.y >= context_.image.height())
                continue;
            const std::size_t index =
                static_cast<std::size_t>(n.y) * context_.image.width() + n.x;
            if (visited[index]) continue;
            if (ColorDistance(context_.image.GetPixel(n), target) > limit) continue;
            visited[index] = 1;
            pending.push(n);
        }
    }
    EndStroke(OperationKind::FloodFill);
}

void PickColorTool::OnStylusDown(const Point& p) {
    context_.foreground = context_.image.GetPixel(p);
    modified_ = false;
}

void CLineTool::OnStylusDown(const Point& p) {
    BeginStroke();
    start_ = last_ = p;
}

void CLineTool::OnStylusMove(const Point& p) {
    if (!drawing_) return;
    RestoreOriginal();
    const int radius = static_cast<int>(std::max(1u, thickness_) / 2u);
    // Preview uses a simple Bresenham + square stamp; final draw is identical.
    int x0 = start_.x;
    int y0 = start_.y;
    const int dx = std::abs(p.x - x0);
    const int dy = -std::abs(p.y - y0);
    const int sx = x0 < p.x ? 1 : -1;
    const int sy = y0 < p.y ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        for (int oy = -radius; oy <= radius; ++oy)
            for (int ox = -radius; ox <= radius; ++ox)
                context_.image.SetPixel(Point{x0 + ox, y0 + oy}, context_.foreground);
        if (x0 == p.x && y0 == p.y) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void CLineTool::OnStylusUp(const Point& p) {
    if (!drawing_) return;
    RestoreOriginal();
    const int radius = static_cast<int>(std::max(1u, thickness_) / 2u);
    int x0 = start_.x;
    int y0 = start_.y;
    const int dx = std::abs(p.x - x0);
    const int dy = -std::abs(p.y - y0);
    const int sx = x0 < p.x ? 1 : -1;
    const int sy = y0 < p.y ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        for (int oy = -radius; oy <= radius; ++oy)
            for (int ox = -radius; ox <= radius; ++ox)
                context_.image.SetPixel(Point{x0 + ox, y0 + oy}, context_.foreground);
        if (x0 == p.x && y0 == p.y) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    EndStroke(OperationKind::Stroke);
}

CRectTool::CRectTool(ToolContext& context, PaintToolKind kind, bool filled)
    : CPaintTool(context, kind), filled_(filled) {}

void CRectTool::DrawShape() {
    const Rect r = NormalizeRect(Rect{start_.x, start_.y, last_.x - start_.x,
                                      last_.y - start_.y});
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            const bool edge = x == r.x || x == r.x + r.w - 1 || y == r.y ||
                              y == r.y + r.h - 1;
            if (filled_ || edge)
                context_.image.SetPixel(Point{x, y}, context_.foreground);
        }
    }
    // Ellipses are rectangular bounds with corners removed.
    if (kind_ == PaintToolKind::Ellipse && !filled_ && r.w > 2 && r.h > 2) {
        const double cx = r.x + r.w / 2.0;
        const double cy = r.y + r.h / 2.0;
        for (int y = r.y; y < r.y + r.h; ++y) {
            for (int x = r.x; x < r.x + r.w; ++x) {
                const double nx = (x - cx) / (r.w / 2.0);
                const double ny = (y - cy) / (r.h / 2.0);
                if (nx * nx + ny * ny > 1.05)
                    context_.image.SetPixel(Point{x, y}, context_.background);
            }
        }
    }
}

void CRectTool::OnStylusDown(const Point& p) {
    BeginStroke();
    start_ = last_ = p;
}

void CRectTool::OnStylusMove(const Point& p) {
    if (!drawing_) return;
    RestoreOriginal();
    last_ = p;
    DrawShape();
}

void CRectTool::OnStylusUp(const Point& p) {
    if (!drawing_) return;
    RestoreOriginal();
    last_ = p;
    DrawShape();
    EndStroke(OperationKind::FillRectangle);
}

void SelectionTool::OnStylusDown(const Point& p) {
    start_ = last_ = p;
    selection_ = Rect{p.x, p.y, 0, 0};
}

void SelectionTool::OnStylusMove(const Point& p) {
    last_ = p;
    selection_ = NormalizeRect(Rect{start_.x, start_.y, p.x - start_.x, p.y - start_.y});
}

void SelectionTool::OnStylusUp(const Point& p) {
    OnStylusMove(p);
    modified_ = !Empty(selection_);
}

void ZoomTool::OnStylusDown(const Point&) {
    zoom_ = zoom_ >= 8 ? 1 : zoom_ * 2;
    modified_ = true;
}

void TextTool::OnStylusDown(const Point& p) {
    // GAP: glyph layout happens in the Win32 stage (DirectWrite); the headless
    // model records the text frame rect only.
    selection_ = Rect{p.x, p.y, 40, 20};
    modified_ = true;
}

std::unique_ptr<CPaintTool> CreatePaintTool(ToolContext& context, PaintToolKind kind) {
    switch (kind) {
        case PaintToolKind::Pencil:
            return std::make_unique<CStrokeTool>(context, kind, false);
        case PaintToolKind::Brush:
            return std::make_unique<CStrokeTool>(context, kind, false, false, false);
        case PaintToolKind::Airbrush:
            return std::make_unique<CStrokeTool>(context, kind, false, false, true);
        case PaintToolKind::Calligraphic:
            return std::make_unique<CStrokeTool>(context, kind, false, true, false);
        case PaintToolKind::Eraser:
            return std::make_unique<CStrokeTool>(context, kind, true, true, false);
        case PaintToolKind::FloodFill:
            return std::make_unique<FloodTool>(context);
        case PaintToolKind::PickColor:
            return std::make_unique<PickColorTool>(context);
        case PaintToolKind::Line:
        case PaintToolKind::Curve:
        case PaintToolKind::Polygon:
            return std::make_unique<CLineTool>(context);
        case PaintToolKind::Rectangle:
        case PaintToolKind::RoundedRectangle:
        case PaintToolKind::AutoShape:
            return std::make_unique<CRectTool>(context, kind, false);
        case PaintToolKind::Ellipse:
            return std::make_unique<CRectTool>(context, kind, false);
        case PaintToolKind::Selection:
        case PaintToolKind::FreeFormSelection:
            return std::make_unique<SelectionTool>(context);
        case PaintToolKind::Zoom:
            return std::make_unique<ZoomTool>(context);
        case PaintToolKind::Text:
            return std::make_unique<TextTool>(context);
        case PaintToolKind::Inactive:
        default:
            return nullptr;
    }
}

const char* ToolName(PaintToolKind kind) {
    switch (kind) {
        case PaintToolKind::Pencil: return "Pencil";
        case PaintToolKind::Brush: return "Brush";
        case PaintToolKind::Airbrush: return "Airbrush";
        case PaintToolKind::Calligraphic: return "Calligraphy";
        case PaintToolKind::Eraser: return "Eraser";
        case PaintToolKind::FloodFill: return "Fill with color";
        case PaintToolKind::PickColor: return "Pick color";
        case PaintToolKind::Line: return "Line";
        case PaintToolKind::Curve: return "Curve";
        case PaintToolKind::Polygon: return "Polygon";
        case PaintToolKind::Rectangle: return "Rectangle";
        case PaintToolKind::Ellipse: return "Ellipse";
        case PaintToolKind::RoundedRectangle: return "Rounded rectangle";
        case PaintToolKind::AutoShape: return "Auto shape";
        case PaintToolKind::Text: return "Text";
        case PaintToolKind::Selection: return "Select";
        case PaintToolKind::FreeFormSelection: return "Free-form select";
        case PaintToolKind::Zoom: return "Magnifier";
        case PaintToolKind::Inactive: return "Inactive";
        default: return "Unknown";
    }
}

}  // namespace pr
