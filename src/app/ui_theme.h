// Paint 11 Light Theme tokens:
// - Accent blue (#005FB8) and primary/secondary ink (#373737, #555555) are
//   extracted directly from reference/paint-11.2609.441.0/ui-assets/*.svg.
// - Surface/hover/divider neutrals follow the shared Windows 11 / Fluent Light
//   tokens. The measured Windows-app neutrals, ruler ink, status hover and
//   two-step canvas shadow are aligned with the sibling WordRebuild v2
//   `word_theme.h` (owner permission, Session 41); tokens are re-declared here,
//   no sibling code or assets are linked or shipped.
#pragma once

#include <windows.h>
#include <initializer_list>

namespace pr {
namespace ui_theme {

// Command/ribbon surfaces.
constexpr COLORREF kSurface          = RGB(243, 243, 243);  // #F3F3F3
constexpr COLORREF kDivider          = RGB(218, 216, 214);  // #DAD8D6 (group divider)
constexpr COLORREF kCardBorder       = RGB(225, 225, 225);  // #E1E1E1 (sibling card outline)
constexpr COLORREF kGroupText        = RGB(85, 85, 85);     // #555555 (from Paint 11 SVGs)
constexpr COLORREF kTextPrimary      = RGB(55, 55, 55);     // #373737 (from Paint 11 SVGs)
constexpr COLORREF kBtnHover         = RGB(225, 223, 221);  // #E1DFDD
constexpr COLORREF kBtnActive        = RGB(200, 198, 196);  // #C8C6C4
constexpr COLORREF kBtnChecked       = RGB(205, 228, 255);  // #CDE4FF (sibling checked fill)
constexpr COLORREF kBtnCheckedBorder = RGB(0, 95, 184);     // #005FB8 (from Paint 11 SVGs)
constexpr COLORREF kDisabledText     = RGB(150, 150, 150);

// Title / tab row (sibling Word 16 measured values; Paint keeps #F3F3F3 caption).
constexpr COLORREF kTitleBg          = RGB(240, 240, 240); // #F0F0F0
constexpr COLORREF kTitleText        = RGB(36, 36, 36);    // #242424

// Color swatch outlines and slider chrome.
constexpr COLORREF kSwatchOutline    = RGB(180, 180, 180);
constexpr COLORREF kSliderTrack      = RGB(205, 205, 205);
constexpr COLORREF kSliderFill       = RGB(0, 95, 184);     // #005FB8 (from Paint 11 SVGs)

// Workspace and status bar.
constexpr COLORREF kWorkspaceBg      = RGB(232, 232, 232);  // #E8E8E8 (sibling workspace)
constexpr COLORREF kStatusBg         = RGB(245, 245, 245);  // #F5F5F5
constexpr COLORREF kStatusText       = RGB(36, 36, 36);     // #242424 (sibling status ink)
constexpr COLORREF kStatusHover      = RGB(225, 225, 225);  // sibling status hover
constexpr COLORREF kRulerFace        = RGB(240, 240, 240);  // #F0F0F0 (sibling ruler)
constexpr COLORREF kRulerTick        = RGB(128, 128, 128);  // sibling ruler tick
constexpr COLORREF kRulerText        = RGB(90, 90, 90);     // sibling ruler number

// Two-step page/canvas shadow (sibling Word 16 workspace tokens).
constexpr COLORREF kShadowDark       = RGB(180, 180, 180);  // #B4B4B4
constexpr COLORREF kShadowLight      = RGB(210, 210, 210);  // #D2D2D2

// Segoe UI / ClearType font set for a Windows-app ribbon. Re-declared from the
// sibling WordRebuild v2 font manager (no code copied); fonts are initialized
// once in wWinMain and released at process exit.
struct UiFonts {
    HFONT hTitleFont = nullptr;
    HFONT hBoldFont = nullptr;
    HFONT hRibbonFont = nullptr;
    HFONT hGroupFont = nullptr;
    HFONT hStatusFont = nullptr;
    HFONT hRulerFont = nullptr;

    void init() {
        if (hRibbonFont) return;
        auto make = [](int height, int weight) {
            return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        };
        hTitleFont  = make(-13, FW_SEMIBOLD);
        hBoldFont   = make(-12, FW_BOLD);
        hRibbonFont = make(-12, FW_NORMAL);
        hGroupFont  = make(-10, FW_NORMAL);
        hStatusFont = make(-12, FW_NORMAL);
        hRulerFont  = make(-10, FW_NORMAL);
    }

    void cleanup() {
        for (HFONT f : {hTitleFont, hBoldFont, hRibbonFont, hGroupFont, hStatusFont, hRulerFont})
            if (f) DeleteObject(f);
        hTitleFont = hBoldFont = hRibbonFont = hGroupFont = hStatusFont = hRulerFont = nullptr;
    }
};

inline UiFonts g_fonts;

}  // namespace ui_theme
}  // namespace pr
