#include "shapes.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pr {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Deterministic coordinate noise: the same shape always produces the same
// pixels, so brush textures remain exactly undoable/redoable.
unsigned BrushNoise(int x, int y, unsigned seed) {
    unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u ^
                 seed * 83492791u;
    h ^= h >> 13;
    h *= 1274126177u;
    return h ^ (h >> 16);
}

void BlendPixel(Image& image, const Point& p, Color c, int alpha) {
    if (p.x < 0 || p.y < 0 || p.x >= image.width() || p.y >= image.height()) return;
    alpha = std::clamp(alpha, 0, 255);
    if (alpha == 255) { image.SetPixel(p, c); return; }
    const Color old = image.GetPixel(p);
    if (Alpha(old) == 0) {
        image.SetPixel(p, MakeColor(Red(c), Green(c), Blue(c), alpha));
        return;
    }
    const unsigned r = (Red(c) * alpha + Red(old) * (255 - alpha)) / 255;
    const unsigned g = (Green(c) * alpha + Green(old) * (255 - alpha)) / 255;
    const unsigned b = (Blue(c) * alpha + Blue(old) * (255 - alpha)) / 255;
    image.SetPixel(p, MakeColor(r, g, b, Alpha(old)));
}

void BrushPixel(Image& image, const Point& p, Color c, ShapeBrushStyle style) {
    if (style == ShapeBrushStyle::None) return;
    const unsigned n = BrushNoise(p.x, p.y, 7u);
    int alpha = 255;
    switch (style) {
        case ShapeBrushStyle::Crayon:
            if (n % 100 < 28) return;       // wax grain: occasional gaps
            alpha = 205;
            break;
        case ShapeBrushStyle::Marker:
            alpha = 175;                    // translucent chisel
            break;
        case ShapeBrushStyle::Oil:
            alpha = 200 + static_cast<int>(n % 56);
            break;
        case ShapeBrushStyle::NaturalPencil:
            if (n % 100 < 42) return;       // graphite tooth
            alpha = 150 + static_cast<int>(n % 50);
            break;
        case ShapeBrushStyle::Watercolor:
            alpha = 80 + static_cast<int>(n % 60);  // translucent wash
            break;
        default:
            break;
    }
    BlendPixel(image, p, c, alpha);
}

void AddThickLine(Image& image, Point a, Point b, Color c, int thickness,
                  ShapeBrushStyle style = ShapeBrushStyle::Color) {
    if (style == ShapeBrushStyle::None) return;
    const int radius = std::max(0, (thickness - 1) / 2);
    int x0 = a.x;
    int y0 = a.y;
    const int dx = std::abs(b.x - x0);
    const int dy = -std::abs(b.y - y0);
    const int sx = x0 < b.x ? 1 : -1;
    const int sy = y0 < b.y ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (radius == 0) {
            BrushPixel(image, Point{x0, y0}, c, style);
        } else {
            for (int oy = -radius; oy <= radius; ++oy) {
                // Marker uses a flat chisel nib instead of a round brush.
                if (style == ShapeBrushStyle::Marker && std::abs(oy) > (radius + 1) / 2) continue;
                for (int ox = -radius; ox <= radius; ++ox) {
                    if (ox * ox + oy * oy > radius * radius + radius) continue;
                    BrushPixel(image, Point{x0 + ox, y0 + oy}, c, style);
                }
            }
        }
        if (x0 == b.x && y0 == b.y) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

std::vector<Point> RegularPolygon(double cx, double cy, double rx, double ry, int sides,
                                 double startAngle) {
    std::vector<Point> points;
    for (int i = 0; i < sides; ++i) {
        const double angle = startAngle + 2.0 * kPi * i / sides;
        points.push_back(Point{static_cast<int>(std::lround(cx + rx * std::cos(angle))),
                               static_cast<int>(std::lround(cy + ry * std::sin(angle)))});
    }
    return points;
}

std::vector<Point> RoundedCorners(const Rect& r, double radius) {
    std::vector<Point> points;
    const double cx[4] = {r.x + radius, r.x + r.w - radius, r.x + r.w - radius, r.x + radius};
    const double cy[4] = {r.y + radius, r.y + radius, r.y + r.h - radius, r.y + r.h - radius};
    for (int corner = 0; corner < 4; ++corner) {
        const double base = -kPi / 2.0 + corner * kPi / 2.0;
        for (int step = 0; step <= 6; ++step) {
            const double angle = base + step * (kPi / 2.0) / 6.0;
            points.push_back(Point{
                static_cast<int>(std::lround(cx[corner] + radius * std::cos(angle))),
                static_cast<int>(std::lround(cy[corner] + radius * std::sin(angle)))});
        }
    }
    return points;
}

}  // namespace

std::vector<Point> ShapePoints(ShapeKind kind, const Rect& input) {
    const Rect r = NormalizeRect(input);
    if (Empty(r)) return {};
    const double cx = r.x + r.w / 2.0;
    const double cy = r.y + r.h / 2.0;
    const double rx = r.w / 2.0;
    const double ry = r.h / 2.0;
    const auto at = [&](double fx, double fy) {
        return Point{static_cast<int>(std::lround(r.x + fx * r.w)),
                     static_cast<int>(std::lround(r.y + fy * r.h))};
    };

    switch (kind) {
        case ShapeKind::Rectangle:
            return {at(0, 0), at(1, 0), at(1, 1), at(0, 1)};
        case ShapeKind::RoundedRectangle:
        case ShapeKind::RoundedRectangularCallout: {
            const double radius = std::min({6.0, r.w / 5.0, r.h / 5.0});
            auto points = RoundedCorners(r, radius);
            if (kind == ShapeKind::RoundedRectangularCallout)
                points.push_back(at(0.35, 1.0)), points.push_back(at(0.5, 1.25)),
                points.push_back(at(0.65, 1.0));
            return points;
        }
        case ShapeKind::Ellipse:
        case ShapeKind::OvalCallout: {
            auto points = RegularPolygon(cx, cy, rx, ry, 48, 0.0);
            if (kind == ShapeKind::OvalCallout)
                points.push_back(at(0.55, 0.85)), points.push_back(at(0.85, 1.2)),
                points.push_back(at(0.7, 0.8));
            return points;
        }
        case ShapeKind::Triangle:
            return {at(0.5, 0), at(1, 1), at(0, 1)};
        case ShapeKind::RightTriangle:
            return {at(0, 0), at(1, 1), at(0, 1)};
        case ShapeKind::Diamond:
            return {at(0.5, 0), at(1, 0.5), at(0.5, 1), at(0, 0.5)};
        case ShapeKind::Pentagon:
            return RegularPolygon(cx, cy, rx, ry, 5, -kPi / 2.0);
        case ShapeKind::Hexagon:
            return RegularPolygon(cx, cy, rx, ry, 6, 0.0);
        case ShapeKind::FivePointedStar:
        case ShapeKind::FourPointedStar:
        case ShapeKind::SixPointedStar: {
            const int points = kind == ShapeKind::FivePointedStar ? 5
                             : kind == ShapeKind::FourPointedStar ? 4 : 6;
            std::vector<Point> result;
            for (int i = 0; i < points * 2; ++i) {
                const double angle = -kPi / 2.0 + kPi * i / points;
                const double scale = (i % 2 == 0) ? 0.5 : 0.22;
                result.push_back(Point{
                    static_cast<int>(std::lround(cx + rx * scale * std::cos(angle))),
                    static_cast<int>(std::lround(cy + ry * scale * std::sin(angle)))});
            }
            return result;
        }
        case ShapeKind::Heart: {
            std::vector<Point> points;
            for (int i = 0; i < 64; ++i) {
                const double t = 2.0 * kPi * i / 64.0;
                const double hx = 16.0 * std::pow(std::sin(t), 3);
                const double hy = 13.0 * std::cos(t) - 5.0 * std::cos(2 * t) -
                                  2.0 * std::cos(3 * t) - std::cos(4 * t);
                points.push_back(Point{static_cast<int>(std::lround(cx + hx * rx / 17.0)),
                                       static_cast<int>(std::lround(cy - hy * ry / 17.0))});
            }
            return points;
        }
        case ShapeKind::Lightning:
            return {at(0.55, 0), at(0.15, 0.55), at(0.45, 0.55), at(0.3, 1),
                    at(0.9, 0.4), at(0.55, 0.4), at(0.8, 0)};
        case ShapeKind::LeftArrow:
            return {at(0.35, 0), at(0.35, 0.3), at(1, 0.3), at(1, 0.7), at(0.35, 0.7),
                    at(0.35, 1), at(0, 0.5)};
        case ShapeKind::RightArrow:
            return {at(0.65, 0), at(0.65, 0.3), at(0, 0.3), at(0, 0.7), at(0.65, 0.7),
                    at(0.65, 1), at(1, 0.5)};
        case ShapeKind::UpArrow:
            return {at(0.5, 0), at(1, 0.35), at(0.65, 0.35), at(0.65, 1), at(0.35, 1),
                    at(0.35, 0.35), at(0, 0.35)};
        case ShapeKind::DownArrow:
            return {at(0.5, 1), at(1, 0.65), at(0.65, 0.65), at(0.65, 0), at(0.35, 0),
                    at(0.35, 0.65), at(0, 0.65)};
        case ShapeKind::CloudCallout: {
            std::vector<Point> points;
            for (int i = 0; i < 32; ++i) {
                const double angle = 2.0 * kPi * i / 32.0;
                const double wave = 0.82 + 0.18 * std::sin(5.0 * angle);
                points.push_back(Point{
                    static_cast<int>(std::lround(cx + rx * wave * std::cos(angle))),
                    static_cast<int>(std::lround(cy + ry * wave * std::sin(angle)))});
            }
            points.push_back(at(0.4, 0.9));
            points.push_back(at(0.25, 1.2));
            points.push_back(at(0.55, 0.95));
            return points;
        }
    }
    return {};
}

const char* ShapeName(ShapeKind kind) {
    switch (kind) {
        case ShapeKind::Rectangle: return "Rectangle";
        case ShapeKind::RoundedRectangle: return "Rounded rectangle";
        case ShapeKind::Ellipse: return "Oval";
        case ShapeKind::Triangle: return "Triangle";
        case ShapeKind::RightTriangle: return "Right triangle";
        case ShapeKind::Diamond: return "Diamond";
        case ShapeKind::Pentagon: return "Pentagon";
        case ShapeKind::Hexagon: return "Hexagon";
        case ShapeKind::FivePointedStar: return "5-point star";
        case ShapeKind::FourPointedStar: return "4-point star";
        case ShapeKind::SixPointedStar: return "6-point star";
        case ShapeKind::Heart: return "Heart";
        case ShapeKind::Lightning: return "Lightning";
        case ShapeKind::LeftArrow: return "Left arrow";
        case ShapeKind::RightArrow: return "Right arrow";
        case ShapeKind::UpArrow: return "Up arrow";
        case ShapeKind::DownArrow: return "Down arrow";
        case ShapeKind::CloudCallout: return "Cloud callout";
        case ShapeKind::OvalCallout: return "Oval callout";
        case ShapeKind::RoundedRectangularCallout: return "Rounded rectangle callout";
    }
    return "Shape";
}

void DrawShape(Image& image, const Rect& bounds, ShapeKind kind, Color color, bool filled,
               int thickness, Color fillColor, ShapeBrushStyle outlineStyle,
               ShapeBrushStyle fillStyle) {
    const auto points = ShapePoints(kind, bounds);
    if (points.empty()) return;
    const Color interior = (Alpha(fillColor) == 0 && Red(fillColor) == 0 &&
                            Green(fillColor) == 0 && Blue(fillColor) == 0)
                               ? color
                               : fillColor;
    if (filled) {
        int minY = points[0].y;
        int maxY = points[0].y;
        for (const auto& p : points) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        for (int y = minY; y <= maxY; ++y) {
            std::vector<double> crossings;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const Point a = points[i];
                const Point b = points[(i + 1) % points.size()];
                if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y))
                    crossings.push_back(a.x + (y - a.y) * static_cast<double>(b.x - a.x) /
                                                    static_cast<double>(b.y - a.y));
            }
            std::sort(crossings.begin(), crossings.end());
            for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                const int from = static_cast<int>(std::ceil(crossings[i] - 0.5));
                const int to = static_cast<int>(std::floor(crossings[i + 1] + 0.5));
                for (int x = from; x <= to; ++x) {
                    if (fillStyle == ShapeBrushStyle::None) continue;
                    if (fillStyle == ShapeBrushStyle::Color) image.SetPixel(Point{x, y}, interior);
                    else BrushPixel(image, Point{x, y}, interior, fillStyle);
                }
            }
        }
    }
    if (thickness > 0 && outlineStyle != ShapeBrushStyle::None) {
        for (std::size_t i = 0; i < points.size(); ++i)
            AddThickLine(image, points[i], points[(i + 1) % points.size()], color, thickness,
                         outlineStyle);
    }
}

}  // namespace pr
