// Image model. Mirrors BasePaint::Image (REA ev_4c7ccd8f9c9b806...):
// Create, Bitmap, Size, Clear, Crop, Resize(SIZE, ResizeMode, ResizeDirection),
// Rotate, HorizontalFlip, VerticalFlip, HorizontalSkew, VerticalSkew,
// InvertColors, HasTransparentPixels — plus the free geometry utilities
// NormalizeAngle, ToDegree, ToRadian, RotatePoint,
// ComputeRotatedDestinationSizeAndPoints, IsValidSkewValue.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "color.h"
#include "geometry.h"

namespace pr {

enum class ResizeMode { Nearest, Bilinear };
enum class ResizeDirection { Both, HorizontalOnly, VerticalOnly };

class Image {
public:
    static std::unique_ptr<Image> Create(const Size& size);
    static std::unique_ptr<Image> Create(const Size& size, Color background);

    explicit Image(const Size& size);
    Image(const Size& size, Color background);

    const Size& size() const { return size_; }
    int width() const { return size_.w; }
    int height() const { return size_.h; }
    std::uint32_t* Bitmap() { return pixels_.data(); }
    const std::uint32_t* Bitmap() const { return pixels_.data(); }

    Color GetPixel(const Point& p) const;
    void SetPixel(const Point& p, Color c);
    void Clear(Color background);
    void FillRect(const Rect& rect, Color c);
    void Blit(const Image& source, const Point& destination);

    bool Crop(const Rect& rect);
    bool Resize(const Size& size, ResizeMode mode, ResizeDirection direction);
    bool Rotate(double radians);
    void HorizontalFlip();
    void VerticalFlip();
    bool HorizontalSkew(int degrees);
    bool VerticalSkew(int degrees);
    void InvertColors();
    bool HasTransparentPixels() const;

private:
    Size size_;
    std::vector<std::uint32_t> pixels_;
};

inline double NormalizeAngle(double radians) {
    constexpr double twoPi = 6.28318530717958647692;
    double r = std::fmod(radians, twoPi);
    if (r < 0.0) r += twoPi;
    return r;
}
inline double ToDegree(double radians) { return radians * 180.0 / 3.14159265358979323846; }
inline double ToRadian(double degrees) { return degrees * 3.14159265358979323846 / 180.0; }

inline Point RotatePoint(const Point& p, const Point& center, double radians) {
    const double dx = static_cast<double>(p.x) - center.x;
    const double dy = static_cast<double>(p.y) - center.y;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return Point{static_cast<int>(std::lround(center.x + dx * c - dy * s)),
                 static_cast<int>(std::lround(center.y + dx * s + dy * c))};
}

inline bool IsValidSkewValue(int degrees) { return degrees >= -89 && degrees <= 89; }

// Returns the destination size and the four rotated corners (in destination
// coordinates), matching the shape of the BasePaint export.
inline std::pair<Size, std::array<Point, 4>>
ComputeRotatedDestinationSizeAndPoints(const Size& size, double radians) {
    const Point center{size.w / 2, size.h / 2};
    const std::array<Point, 4> source = {
        Point{0, 0}, Point{size.w, 0}, Point{size.w, size.h}, Point{0, size.h}};
    std::array<Point, 4> rotated{};
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        rotated[i] = RotatePoint(source[i], center, radians);
        minX = std::min(minX, rotated[i].x);
        minY = std::min(minY, rotated[i].y);
        maxX = std::max(maxX, rotated[i].x);
        maxY = std::max(maxY, rotated[i].y);
    }
    for (auto& p : rotated) {
        p.x -= minX;
        p.y -= minY;
    }
    return {Size{maxX - minX, maxY - minY}, rotated};
}

}  // namespace pr
