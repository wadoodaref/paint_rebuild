// Geometry primitives. Mirrors the native POINT/SIZE/RECT shapes used by
// BasePaint.dll (REA ev_4c7ccd8f...: ToNative/FromNative POINT, SIZE, RECT).
#pragma once

#include <algorithm>

namespace pr {

struct Point {
    int x = 0;
    int y = 0;
};

struct Size {
    int w = 0;
    int h = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

inline int Clamp(int v, int lo, int hi) {
    return std::max(lo, std::min(v, hi));
}

// A rectangle with negative extents is normalised to positive w/h (as a
// drag from bottom-right to top-left must still select an area).
inline Rect NormalizeRect(Rect r) {
    if (r.w < 0) {
        r.x += r.w;
        r.w = -r.w;
    }
    if (r.h < 0) {
        r.y += r.h;
        r.h = -r.h;
    }
    return r;
}

inline bool Contains(const Rect& r, const Point& p) {
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

inline bool Intersects(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

inline Rect Intersect(const Rect& a, const Rect& b) {
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    const int x2 = std::min(a.x + a.w, b.x + b.w);
    const int y2 = std::min(a.y + a.h, b.y + b.h);
    return Rect{x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1)};
}

inline bool Empty(const Rect& r) {
    return r.w <= 0 || r.h <= 0;
}

}  // namespace pr
