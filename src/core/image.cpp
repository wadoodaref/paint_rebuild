#include "image.h"

#include <algorithm>
#include <cstddef>

namespace pr {

namespace {
constexpr Color kCanvasBackground = MakeColor(255, 255, 255);

Color BlendOver(Color destination, Color source) {
    const int sa = Alpha(source);
    if (sa >= 255) return source;
    if (sa <= 0) return destination;
    const int da = Alpha(destination);
    const int outA = sa + (da * (255 - sa)) / 255;
    if (outA == 0) return Color{0};
    const auto mix = [sa, da, outA](int s, int d) {
        return static_cast<std::uint8_t>((s * sa + d * da * (255 - sa) / 255) / outA);
    };
    return MakeColor(mix(Red(source), Red(destination)),
                     mix(Green(source), Green(destination)),
                     mix(Blue(source), Blue(destination)),
                     static_cast<std::uint8_t>(outA));
}
}  // namespace

std::unique_ptr<Image> Image::Create(const Size& size) {
    return std::make_unique<Image>(size);
}

std::unique_ptr<Image> Image::Create(const Size& size, Color background) {
    return std::make_unique<Image>(size, background);
}

Image::Image(const Size& size) : Image(size, kCanvasBackground) {}

Image::Image(const Size& size, Color background)
    : size_{std::max(0, size.w), std::max(0, size.h)},
      pixels_(static_cast<std::size_t>(size_.w) * static_cast<std::size_t>(size_.h),
              background.argb) {}

Color Image::GetPixel(const Point& p) const {
    if (p.x < 0 || p.y < 0 || p.x >= size_.w || p.y >= size_.h) return Color{0};
    return Color{pixels_[static_cast<std::size_t>(p.y) * size_.w + p.x]};
}

void Image::SetPixel(const Point& p, Color c) {
    if (p.x < 0 || p.y < 0 || p.x >= size_.w || p.y >= size_.h) return;
    pixels_[static_cast<std::size_t>(p.y) * size_.w + p.x] = c.argb;
}

void Image::Clear(Color background) {
    std::fill(pixels_.begin(), pixels_.end(), background.argb);
}

void Image::FillRect(const Rect& rect, Color c) {
    const Rect r = Intersect(NormalizeRect(rect), Rect{0, 0, size_.w, size_.h});
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) SetPixel(Point{x, y}, c);
    }
}

void Image::Blit(const Image& source, const Point& destination, bool transparent, Color key) {
    const Rect clip{0, 0, size_.w, size_.h};
    const std::uint32_t keyRgb = key.argb & 0x00FFFFFFu;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const Point to{destination.x + x, destination.y + y};
            if (!Contains(clip, to)) continue;
            const Color src = source.GetPixel(Point{x, y});
            if (transparent && (src.argb & 0x00FFFFFFu) == keyRgb) continue;
            SetPixel(to, BlendOver(GetPixel(to), src));
        }
    }
}

bool Image::Crop(const Rect& rect) {
    Rect r = Intersect(NormalizeRect(rect), Rect{0, 0, size_.w, size_.h});
    if (Empty(r)) return false;
    std::vector<std::uint32_t> cropped(static_cast<std::size_t>(r.w) * r.h);
    for (int y = 0; y < r.h; ++y) {
        for (int x = 0; x < r.w; ++x) {
            cropped[static_cast<std::size_t>(y) * r.w + x] =
                pixels_[static_cast<std::size_t>(r.y + y) * size_.w + (r.x + x)];
        }
    }
    size_ = Size{r.w, r.h};
    pixels_ = std::move(cropped);
    return true;
}

bool Image::Resize(const Size& newSize, ResizeMode mode, ResizeDirection direction) {
    Size target = newSize;
    if (direction == ResizeDirection::HorizontalOnly) target.h = size_.h;
    if (direction == ResizeDirection::VerticalOnly) target.w = size_.w;
    if (target.w <= 0 || target.h <= 0 || size_.w <= 0 || size_.h <= 0) return false;

    Image result(target, kCanvasBackground);
    for (int y = 0; y < target.h; ++y) {
        for (int x = 0; x < target.w; ++x) {
            const double sx = (static_cast<double>(x) + 0.5) * size_.w / target.w - 0.5;
            const double sy = (static_cast<double>(y) + 0.5) * size_.h / target.h - 0.5;
            Color c = kCanvasBackground;
            if (mode == ResizeMode::Nearest) {
                const int ix = Clamp(static_cast<int>(std::lround(sx)), 0, size_.w - 1);
                const int iy = Clamp(static_cast<int>(std::lround(sy)), 0, size_.h - 1);
                c = GetPixel(Point{ix, iy});
            } else {
                const int x0 = Clamp(static_cast<int>(std::floor(sx)), 0, size_.w - 1);
                const int y0 = Clamp(static_cast<int>(std::floor(sy)), 0, size_.h - 1);
                const int x1 = Clamp(x0 + 1, 0, size_.w - 1);
                const int y1 = Clamp(y0 + 1, 0, size_.h - 1);
                const double fx = std::max(0.0, std::min(1.0, sx - x0));
                const double fy = std::max(0.0, std::min(1.0, sy - y0));
                const auto lerp = [](int a, int b, double t) {
                    return static_cast<std::uint8_t>(std::lround(a + (b - a) * t));
                };
                c = MakeColor(lerp(Red(GetPixel(Point{x0, y0})), Red(GetPixel(Point{x1, y0})), fx),
                              lerp(Green(GetPixel(Point{x0, y0})), Green(GetPixel(Point{x0, y1})), fy),
                              lerp(Blue(GetPixel(Point{x0, y0})), Blue(GetPixel(Point{x1, y1})), fx),
                              Alpha(GetPixel(Point{x0, y0})));
            }
            result.SetPixel(Point{x, y}, c);
        }
    }
    *this = std::move(result);
    return true;
}

bool Image::Rotate(double radians) {
    if (size_.w <= 0 || size_.h <= 0) return false;
    const auto geometry = ComputeRotatedDestinationSizeAndPoints(size_, radians);
    const Size target = geometry.first;
    if (target.w <= 0 || target.h <= 0) return false;

    // Recover the offset that the helper applied to its corners.
    const Point center{size_.w / 2, size_.h / 2};
    double minX = 0.0;
    double minY = 0.0;
    const std::array<Point, 4> corners = {
        Point{0, 0}, Point{size_.w, 0}, Point{size_.w, size_.h}, Point{0, size_.h}};
    for (const auto& p : corners) {
        const Point r = RotatePoint(p, center, radians);
        minX = std::min<double>(minX, r.x);
        minY = std::min<double>(minY, r.y);
    }

    Image result(target, kCanvasBackground);
    const double c = std::cos(-radians);
    const double s = std::sin(-radians);
    for (int y = 0; y < target.h; ++y) {
        for (int x = 0; x < target.w; ++x) {
            const double rx = static_cast<double>(x) + minX - center.x;
            const double ry = static_cast<double>(y) + minY - center.y;
            const int sx = static_cast<int>(std::lround(center.x + rx * c - ry * s));
            const int sy = static_cast<int>(std::lround(center.y + rx * s + ry * c));
            if (sx >= 0 && sy >= 0 && sx < size_.w && sy < size_.h)
                result.SetPixel(Point{x, y}, GetPixel(Point{sx, sy}));
        }
    }
    *this = std::move(result);
    return true;
}

void Image::HorizontalFlip() {
    for (int y = 0; y < size_.h; ++y) {
        for (int x = 0; x < size_.w / 2; ++x) {
            const Color a = GetPixel(Point{x, y});
            SetPixel(Point{x, y}, GetPixel(Point{size_.w - 1 - x, y}));
            SetPixel(Point{size_.w - 1 - x, y}, a);
        }
    }
}

void Image::VerticalFlip() {
    for (int y = 0; y < size_.h / 2; ++y) {
        for (int x = 0; x < size_.w; ++x) {
            const Color a = GetPixel(Point{x, y});
            SetPixel(Point{x, y}, GetPixel(Point{x, size_.h - 1 - y}));
            SetPixel(Point{x, size_.h - 1 - y}, a);
        }
    }
}

bool Image::HorizontalSkew(int degrees) {
    if (!IsValidSkewValue(degrees)) return false;
    Image result(size_, kCanvasBackground);
    const double t = std::tan(ToRadian(static_cast<double>(degrees)));
    const double cy = static_cast<double>(size_.h) / 2.0;
    for (int y = 0; y < size_.h; ++y) {
        const int shift = static_cast<int>(std::lround((y - cy) * t));
        for (int x = 0; x < size_.w; ++x) {
            const int sx = x - shift;
            if (sx >= 0 && sx < size_.w) result.SetPixel(Point{x, y}, GetPixel(Point{sx, y}));
        }
    }
    *this = std::move(result);
    return true;
}

bool Image::VerticalSkew(int degrees) {
    if (!IsValidSkewValue(degrees)) return false;
    Image result(size_, kCanvasBackground);
    const double t = std::tan(ToRadian(static_cast<double>(degrees)));
    const double cx = static_cast<double>(size_.w) / 2.0;
    for (int x = 0; x < size_.w; ++x) {
        const int shift = static_cast<int>(std::lround((x - cx) * t));
        for (int y = 0; y < size_.h; ++y) {
            const int sy = y - shift;
            if (sy >= 0 && sy < size_.h) result.SetPixel(Point{x, y}, GetPixel(Point{x, sy}));
        }
    }
    *this = std::move(result);
    return true;
}

void Image::InvertColors() {
    for (auto& pixel : pixels_) pixel = Invert(Color{pixel}).argb;
}

bool Image::HasTransparentPixels() const {
    for (const auto pixel : pixels_) {
        if (Alpha(Color{pixel}) < 255) return true;
    }
    return false;
}

}  // namespace pr
