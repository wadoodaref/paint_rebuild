// ARGB colour helpers. BasePaint.dll exports ToColor/FromNative/WithAlpha and
// Invert(Color) (REA ev_4c7ccd8f...); these are clean-room equivalents.
#pragma once

#include <cstdint>

namespace pr {

struct Color {
    std::uint32_t argb = 0xFF000000u;  // opaque black by default
};

constexpr Color MakeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                          std::uint8_t a = 255) {
    return Color{static_cast<std::uint32_t>(a) << 24 |
                 static_cast<std::uint32_t>(r) << 16 |
                 static_cast<std::uint32_t>(g) << 8 |
                 static_cast<std::uint32_t>(b)};
}

constexpr std::uint8_t Red(Color c) {
    return static_cast<std::uint8_t>((c.argb >> 16) & 0xFFu);
}
constexpr std::uint8_t Green(Color c) {
    return static_cast<std::uint8_t>((c.argb >> 8) & 0xFFu);
}
constexpr std::uint8_t Blue(Color c) {
    return static_cast<std::uint8_t>(c.argb & 0xFFu);
}
constexpr std::uint8_t Alpha(Color c) {
    return static_cast<std::uint8_t>((c.argb >> 24) & 0xFFu);
}

inline Color WithAlpha(Color c, std::uint8_t a) {
    return MakeColor(Red(c), Green(c), Blue(c), a);
}

// Channel inversion keeps alpha (Paint's Invert Colors never makes a opaque
// pixel transparent).
inline Color Invert(Color c) {
    return MakeColor(static_cast<std::uint8_t>(255 - Red(c)),
                     static_cast<std::uint8_t>(255 - Green(c)),
                     static_cast<std::uint8_t>(255 - Blue(c)), Alpha(c));
}

// GAP: the exact 28-colour palette values are resource data, not export data.
// These are the conventional Paint swatches; replace from REA resources in a
// later fidelity stage if a difference is observed.
namespace palette {
constexpr Color Black() { return MakeColor(0, 0, 0); }
constexpr Color White() { return MakeColor(255, 255, 255); }
constexpr Color Red() { return MakeColor(255, 0, 0); }
constexpr Color Green() { return MakeColor(0, 128, 0); }
constexpr Color Blue() { return MakeColor(0, 0, 255); }
constexpr Color Yellow() { return MakeColor(255, 255, 0); }
}  // namespace palette

}  // namespace pr
