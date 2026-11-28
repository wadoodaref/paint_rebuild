// Paint 11 Light Theme tokens:
// - Accent blue (#005FB8) and primary/secondary ink (#373737, #555555) are
//   extracted directly from reference/paint-11.2609.441.0/ui-assets/*.svg.
// - Surface/hover/divider neutrals (#F3F3F3, #E1DFDD, #DAD8D6) follow the
//   shared Windows 11 / Fluent Light tokens.
#pragma once

#include <windows.h>

namespace pr {
namespace ui_theme {

// Command/ribbon surfaces.
constexpr COLORREF kSurface          = RGB(243, 243, 243);  // #F3F3F3
constexpr COLORREF kDivider          = RGB(218, 216, 214);  // #DAD8D6
constexpr COLORREF kGroupText        = RGB(85, 85, 85);     // #555555 (from Paint 11 SVGs)
constexpr COLORREF kTextPrimary      = RGB(55, 55, 55);     // #373737 (from Paint 11 SVGs)
constexpr COLORREF kBtnHover         = RGB(225, 223, 221);  // #E1DFDD
constexpr COLORREF kBtnActive        = RGB(200, 198, 196);  // #C8C6C4
constexpr COLORREF kBtnChecked       = RGB(218, 234, 250);  // soft tint of #005FB8
constexpr COLORREF kBtnCheckedBorder = RGB(0, 95, 184);     // #005FB8 (from Paint 11 SVGs)
constexpr COLORREF kDisabledText     = RGB(150, 150, 150);

// Color swatch outlines and slider chrome.
constexpr COLORREF kSwatchOutline    = RGB(180, 180, 180);
constexpr COLORREF kSliderTrack      = RGB(205, 205, 205);
constexpr COLORREF kSliderFill       = RGB(0, 95, 184);     // #005FB8 (from Paint 11 SVGs)

// Workspace and status bar.
constexpr COLORREF kWorkspaceBg      = RGB(238, 238, 238);
constexpr COLORREF kStatusBg         = RGB(245, 245, 245);  // #F5F5F5
constexpr COLORREF kStatusText       = RGB(60, 60, 60);
constexpr COLORREF kRulerFace        = RGB(246, 246, 246);

}  // namespace ui_theme
}  // namespace pr
