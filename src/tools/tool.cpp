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

namespace {

void BlendPixel(Image& image, Point p, Color c, double alphaScale) {
    const double a = (Alpha(c) / 255.0) * std::max(0.0, std::min(1.0, alphaScale));
    if (a <= 0.0) return;
    if (a >= 0.999) {
        image.SetPixel(p, WithAlpha(c, 255));
        return;
    }
    const Color dst = image.GetPixel(p);
    const auto mix = [a](std::uint8_t s, std::uint8_t d) -> std::uint8_t {
        return static_cast<std::uint8_t>(std::lround(s * a + d * (1.0 - a)));
    };
    image.SetPixel(p, MakeColor(mix(Red(c), Red(dst)), mix(Green(c), Green(dst)),
                                mix(Blue(c), Blue(dst)), std::max(Alpha(dst), static_cast<std::uint8_t>(std::lround(a * 255)))));
}

std::uint32_t Hash2D(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                      static_cast<std::uint32_t>(y) * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

}  // namespace

void CStrokeTool::Stamp(const Point& p) {
    const int radius = static_cast<int>(std::max(1u, thickness_) / 2u);
    const Color c = useBackground_ ? context_.background : context_.foreground;
    const double baseAlpha = opacity_;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (kind_ == PaintToolKind::Calligraphic) {
                if (std::abs(dx - dy) > std::max(1, radius / 2)) continue;
            } else if (!square_ && dx * dx + dy * dy > radius * radius + radius) {
                continue;
            }

            const Point pt{p.x + dx, p.y + dy};
            const std::uint32_t h = Hash2D(pt.x, pt.y, static_cast<std::uint32_t>(p.x * 31 + p.y));
            double alpha = baseAlpha;
            if (airbrush_ || kind_ == PaintToolKind::Airbrush) {
                if ((h % 100u) > 28u) continue;
                alpha *= 0.65;
            } else if (kind_ == PaintToolKind::Crayon) {
                if ((h % 100u) > 58u) continue;
            } else if (kind_ == PaintToolKind::NaturalPencil) {
                if ((h % 100u) > 72u) continue;
                alpha *= 0.85;
            } else if (kind_ == PaintToolKind::Marker) {
                alpha *= 0.65;
            } else if (kind_ == PaintToolKind::Watercolor) {
                const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy)) /
                                    std::max(1, radius + 1);
                alpha *= 0.45 * (1.0 - 0.5 * dist);
            } else if (kind_ == PaintToolKind::OilBrush) {
                const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy)) /
                                    std::max(1, radius + 1);
                alpha *= (1.0 - 0.35 * dist);
            }
            BlendPixel(context_.image, pt, c, alpha);
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
    if (p.x != last_.x || p.y != last_.y) DrawSegment(last_, p);
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

CRectTool::CRectTool(ToolContext& context, PaintToolKind kind, bool filled, ShapeKind shape)
    : CPaintTool(context, kind), filled_(filled), shape_(shape) {}

void CRectTool::DrawShape() {
    const Rect r = NormalizeRect(Rect{start_.x, start_.y, last_.x - start_.x,
                                      last_.y - start_.y});
    pr::DrawShape(context_.image, r, shape_, context_.foreground, filled_,
                  static_cast<int>(std::max(1u, thickness_)), context_.background,
                  outlineStyle_, fillStyle_);
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

void FreeFormSelectionTool::OnStylusDown(const Point& p) {
    points_.clear();
    points_.push_back(p);
    start_ = last_ = p;
    selection_ = Rect{p.x, p.y, 0, 0};
}

void FreeFormSelectionTool::OnStylusMove(const Point& p) {
    points_.push_back(p);
    last_ = p;
    int minX = points_[0].x, maxX = points_[0].x;
    int minY = points_[0].y, maxY = points_[0].y;
    for (const auto& pt : points_) {
        minX = std::min(minX, pt.x);
        maxX = std::max(maxX, pt.x);
        minY = std::min(minY, pt.y);
        maxY = std::max(maxY, pt.y);
    }
    selection_ = Rect{minX, minY, maxX - minX, maxY - minY};
}

void FreeFormSelectionTool::OnStylusUp(const Point& p) {
    OnStylusMove(p);
    modified_ = !Empty(selection_);
}

namespace {
void DrawSegmentThick(Image& img, Point a, Point b, Color c, int thickness) {
    const int radius = std::max(0, (thickness - 1) / 2);
    int x0 = a.x, y0 = a.y;
    const int dx = std::abs(b.x - x0), dy = -std::abs(b.y - y0);
    const int sx = x0 < b.x ? 1 : -1, sy = y0 < b.y ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        for (int oy = -radius; oy <= radius; ++oy)
            for (int ox = -radius; ox <= radius; ++ox)
                img.SetPixel(Point{x0 + ox, y0 + oy}, c);
        if (x0 == b.x && y0 == b.y) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
}  // namespace

void CBezierTool::DrawCurve() {
    const int t = static_cast<int>(std::max(1u, thickness_));
    if (stage_ == 0) {
        DrawSegmentThick(context_.image, p0_, p1_, context_.foreground, t);
        return;
    }
    const int steps = 48;
    Point prev = p0_;
    for (int i = 1; i <= steps; ++i) {
        const double u = static_cast<double>(i) / steps;
        const double v = 1.0 - u;
        const double x = v * v * v * p0_.x + 3 * v * v * u * c1_.x +
                         3 * v * u * u * c2_.x + u * u * u * p1_.x;
        const double y = v * v * v * p0_.y + 3 * v * v * u * c1_.y +
                         3 * v * u * u * c2_.y + u * u * u * p1_.y;
        const Point cur{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
        DrawSegmentThick(context_.image, prev, cur, context_.foreground, t);
        prev = cur;
    }
}

void CBezierTool::OnStylusDown(const Point& p) {
    if (stage_ == 0) {
        BeginStroke();
        p0_ = p1_ = c1_ = c2_ = p;
    } else if (stage_ == 1) {
        c1_ = c2_ = p;
        RestoreOriginal();
        DrawCurve();
    } else {
        c2_ = p;
        RestoreOriginal();
        DrawCurve();
    }
}

void CBezierTool::OnStylusMove(const Point& p) {
    if (!drawing_) return;
    if (stage_ == 0) { p1_ = p; c1_ = p0_; c2_ = p1_; }
    else if (stage_ == 1) { c1_ = p; c2_ = p; }
    else { c2_ = p; }
    RestoreOriginal();
    DrawCurve();
}

void CBezierTool::OnStylusUp(const Point& p) {
    if (!drawing_) return;
    OnStylusMove(p);
    if (stage_ < 2) {
        ++stage_;
    } else {
        stage_ = 0;
        EndStroke(OperationKind::Stroke);
    }
}

void CBezierTool::OnCancel() {
    stage_ = 0;
    CPaintTool::OnCancel();
}

void CPaintPolygonTool::DrawPolygon(bool close) {
    if (vertices_.empty()) return;
    const int t = static_cast<int>(std::max(1u, thickness_));
    for (std::size_t i = 0; i + 1 < vertices_.size(); ++i)
        DrawSegmentThick(context_.image, vertices_[i], vertices_[i + 1], context_.foreground, t);
    if (close && vertices_.size() >= 2)
        DrawSegmentThick(context_.image, vertices_.back(), vertices_.front(), context_.foreground, t);
    else if (!close)
        DrawSegmentThick(context_.image, vertices_.back(), previewEnd_, context_.foreground, t);
}

void CPaintPolygonTool::CommitPolygon() {
    RestoreOriginal();
    DrawPolygon(true);
    vertices_.clear();
    EndStroke(OperationKind::Stroke);
}

void CPaintPolygonTool::OnStylusDown(const Point& p) {
    if (vertices_.empty()) {
        BeginStroke();
        vertices_.push_back(p);
        previewEnd_ = p;
        return;
    }
    if (vertices_.size() >= 3 &&
        std::abs(p.x - vertices_.front().x) <= 5 &&
        std::abs(p.y - vertices_.front().y) <= 5) {
        CommitPolygon();
        return;
    }
    vertices_.push_back(p);
    previewEnd_ = p;
    RestoreOriginal();
    DrawPolygon(false);
}

void CPaintPolygonTool::OnStylusMove(const Point& p) {
    if (!drawing_ || vertices_.empty()) return;
    previewEnd_ = p;
    RestoreOriginal();
    DrawPolygon(false);
}

void CPaintPolygonTool::OnStylusUp(const Point& p) {
    if (!drawing_ || vertices_.empty()) return;
    if (vertices_.size() == 1 && (p.x != vertices_[0].x || p.y != vertices_[0].y))
        vertices_.push_back(p);
    previewEnd_ = p;
    RestoreOriginal();
    DrawPolygon(false);
}

void CPaintPolygonTool::OnKeyDown(unsigned virtualKey) {
    if (drawing_ && vertices_.size() >= 3 && virtualKey == 13)  // VK_RETURN
        CommitPolygon();
}

void CPaintPolygonTool::OnCancel() {
    vertices_.clear();
    CPaintTool::OnCancel();
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

std::unique_ptr<CPaintTool> CreateAutoShapeTool(ToolContext& context, ShapeKind shape, bool filled) {
    auto tool = std::make_unique<CRectTool>(context, PaintToolKind::AutoShape, filled);
    tool->SetShape(shape);
    return tool;
}

std::unique_ptr<CPaintTool> CreatePaintTool(ToolContext& context, PaintToolKind kind, bool filled) {
    switch (kind) {
        case PaintToolKind::Pencil:
            return std::make_unique<CStrokeTool>(context, kind, false);
        case PaintToolKind::Brush:
            return std::make_unique<CStrokeTool>(context, kind, false, false, false);
        case PaintToolKind::Airbrush:
            return std::make_unique<CStrokeTool>(context, kind, false, false, true);
        case PaintToolKind::Calligraphic:
            return std::make_unique<CStrokeTool>(context, kind, false, true, false);
        case PaintToolKind::OilBrush:
        case PaintToolKind::Watercolor:
        case PaintToolKind::NaturalPencil:
            return std::make_unique<CStrokeTool>(context, kind, false, false, false);
        case PaintToolKind::Marker:
        case PaintToolKind::Crayon:
            return std::make_unique<CStrokeTool>(context, kind, false, true, false);
        case PaintToolKind::Eraser:
            return std::make_unique<CStrokeTool>(context, kind, true, true, false);
        case PaintToolKind::FloodFill:
            return std::make_unique<FloodTool>(context);
        case PaintToolKind::PickColor:
            return std::make_unique<PickColorTool>(context);
        case PaintToolKind::Line:
            return std::make_unique<CLineTool>(context);
        case PaintToolKind::Curve:
            return std::make_unique<CBezierTool>(context);
        case PaintToolKind::Polygon:
            return std::make_unique<CPaintPolygonTool>(context, filled);
        case PaintToolKind::Rectangle:
            return std::make_unique<CRectTool>(context, kind, filled, ShapeKind::Rectangle);
        case PaintToolKind::RoundedRectangle:
            return std::make_unique<CRectTool>(context, kind, filled, ShapeKind::RoundedRectangle);
        case PaintToolKind::AutoShape:
            return std::make_unique<CRectTool>(context, kind, filled);
        case PaintToolKind::Ellipse:
            return std::make_unique<CRectTool>(context, kind, filled, ShapeKind::Ellipse);
        case PaintToolKind::Selection:
            return std::make_unique<SelectionTool>(context);
        case PaintToolKind::FreeFormSelection:
            return std::make_unique<FreeFormSelectionTool>(context);
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
        case PaintToolKind::Calligraphic: return "Calligraphy pen";
        case PaintToolKind::OilBrush: return "Oil brush";
        case PaintToolKind::Watercolor: return "Watercolor";
        case PaintToolKind::Marker: return "Marker";
        case PaintToolKind::Crayon: return "Crayon";
        case PaintToolKind::NaturalPencil: return "Natural pencil";
        case PaintToolKind::Eraser: return "Eraser";
        case PaintToolKind::FloodFill: return "Fill with color";
        case PaintToolKind::PickColor: return "Color picker";
        case PaintToolKind::Line: return "Line";
        case PaintToolKind::Curve: return "Curve";
        case PaintToolKind::Polygon: return "Polygon";
        case PaintToolKind::Rectangle: return "Rectangle";
        case PaintToolKind::Ellipse: return "Oval";
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
