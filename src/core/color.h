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

// The 28-colour Paint basic palette. Inference: these canonical MS Paint
// swatches are not present as literal hex in the REA artifacts (evidence gap),
// but match the shipped colour-box convention. They are the single source of
// truth for the Win32 colour box.
namespace palette {
constexpr Color Black() { return MakeColor(0, 0, 0); }
constexpr Color White() { return MakeColor(255, 255, 255); }
constexpr Color Red() { return MakeColor(255, 0, 0); }
constexpr Color Green() { return MakeColor(0, 128, 0); }
constexpr Color Blue() { return MakeColor(0, 0, 255); }
constexpr Color Yellow() { return MakeColor(255, 255, 0); }

constexpr Color BasicColors[28] = {
    MakeColor(0x00, 0x00, 0x00), MakeColor(0x80, 0x80, 0x80),
    MakeColor(0x80, 0x00, 0x00), MakeColor(0x80, 0x80, 0x00),
    MakeColor(0x00, 0x80, 0x00), MakeColor(0x00, 0x80, 0x80),
    MakeColor(0x00, 0x00, 0x80), MakeColor(0x80, 0x00, 0x80),
    MakeColor(0x80, 0x80, 0x40), MakeColor(0x00, 0x40, 0x40),
    MakeColor(0x00, 0x80, 0xFF), MakeColor(0x00, 0x40, 0x80),
    MakeColor(0x80, 0x00, 0xFF), MakeColor(0x80, 0x40, 0x00),
    MakeColor(0xFF, 0xFF, 0xFF), MakeColor(0xC0, 0xC0, 0xC0),
    MakeColor(0xFF, 0x00, 0x00), MakeColor(0xFF, 0xFF, 0x00),
    MakeColor(0x00, 0xFF, 0x00), MakeColor(0x00, 0xFF, 0xFF),
    MakeColor(0x00, 0x00, 0xFF), MakeColor(0xFF, 0x00, 0xFF),
    MakeColor(0xFF, 0xFF, 0x80), MakeColor(0x00, 0xFF, 0x80),
    MakeColor(0x80, 0xFF, 0xFF), MakeColor(0x80, 0x80, 0xFF),
    MakeColor(0xFF, 0x00, 0x80), MakeColor(0xFF, 0x80, 0x40)};

// Paint 11 default command-bar palette (20 colors, 10x2), names match UIA.
constexpr Color Paint11Colors[20] = {
    MakeColor(0x00,0x00,0x00), MakeColor(0x7F,0x7F,0x7F), MakeColor(0x88,0x00,0x15), MakeColor(0xED,0x1C,0x24), MakeColor(0xFF,0x7F,0x27),
    MakeColor(0xFF,0xF2,0x00), MakeColor(0x22,0xB1,0x4C), MakeColor(0x00,0xA2,0xE8), MakeColor(0x3F,0x48,0xCC), MakeColor(0xA3,0x49,0xA4),
    MakeColor(0xFF,0xFF,0xFF), MakeColor(0xC3,0xC3,0xC3), MakeColor(0xB9,0x7A,0x57), MakeColor(0xFF,0xAE,0xC9), MakeColor(0xFF,0xC9,0x0E),
    MakeColor(0xEF,0xE4,0xB0), MakeColor(0xB5,0xE6,0x1D), MakeColor(0x99,0xD9,0xEA), MakeColor(0x70,0x92,0xBE), MakeColor(0xC8,0xBF,0xE7)};
}  // namespace palette

}  // namespace pr
