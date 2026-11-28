// Shared Microsoft/Fluent visual tokens, adapted with owner permission from
// word_rebuild_v2/src/app/word_theme.h and ppt_rebuild/src/ui/ppt_theme.h.
// The source values there were measured from installed Office 16 apps; they
// are a closer basis than invented literals, but Paint 11's exact RGB values
// are not present in the vendored REA artifacts and remain an unknown.
#pragma once

#include <windows.h>

namespace pr {
namespace ui_theme {

// Command/ribbon surfaces (#F3F2F1 family, shared by Word/PowerPoint shells).
constexpr COLORREF kSurface          = RGB(243, 243, 243);
constexpr COLORREF kDivider          = RGB(218, 216, 214);
constexpr COLORREF kGroupText        = RGB(96, 94, 92);
constexpr COLORREF kTextPrimary      = RGB(36, 36, 36);
constexpr COLORREF kBtnHover         = RGB(225, 223, 221);  // #E1DFDD
constexpr COLORREF kBtnActive        = RGB(200, 198, 196);  // #C8C6C4
constexpr COLORREF kBtnChecked       = RGB(205, 228, 255);  // #CDE4FF
constexpr COLORREF kBtnCheckedBorder = RGB(96, 160, 240);   // #60A0F0
constexpr COLORREF kDisabledText     = RGB(150, 150, 150);

// Color swatch outlines and slider chrome.
constexpr COLORREF kSwatchOutline    = RGB(180, 180, 180);
constexpr COLORREF kSliderTrack      = RGB(205, 205, 205);
constexpr COLORREF kSliderFill       = RGB(0, 120, 215);

// Workspace and status bar.
constexpr COLORREF kWorkspaceBg      = RGB(238, 238, 238);
constexpr COLORREF kStatusBg         = RGB(245, 245, 245);  // #F5F5F5
constexpr COLORREF kStatusText       = RGB(60, 60, 60);
constexpr COLORREF kRulerFace        = RGB(246, 246, 246);

}  // namespace ui_theme
}  // namespace pr
