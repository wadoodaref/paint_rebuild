// Auto-shape factory. Mirrors BasePaint.dll ShapeFactory exports (REA
// ev_4c7ccd8f...): rectangle, rounded rectangle, ellipse, triangle, diamond,
// pentagon, hexagon, 4/5/6-pointed stars, heart, lightning, four arrows and
// three callouts. Fresh geometry code, no Microsoft assets.
#pragma once

#include <vector>

#include "color.h"
#include "geometry.h"
#include "image.h"

namespace pr {

enum class ShapeKind {
    Rectangle,
    RoundedRectangle,
    Ellipse,
    Triangle,
    Diamond,
    Pentagon,
    Hexagon,
    FivePointedStar,
    FourPointedStar,
    SixPointedStar,
    Heart,
    Lightning,
    LeftArrow,
    RightArrow,
    UpArrow,
    DownArrow,
    CloudCallout,
    OvalCallout,
    RoundedRectangularCallout
};

std::vector<Point> ShapePoints(ShapeKind kind, const Rect& bounds);
const char* ShapeName(ShapeKind kind);

// Draws an outline (always) and optionally fills the interior. The outline
// uses Bresenham segments; the fill uses an even-odd scanline rule.
void DrawShape(Image& image, const Rect& bounds, ShapeKind kind, Color color, bool filled);

}  // namespace pr
