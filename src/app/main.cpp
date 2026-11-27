// PaintRebuild — native Win32 shell (PaintUI analog).
// Classic Paint workflow: tool box on the left, colour box at the bottom,
// canvas in the middle, menus and status bar. All drawing runs on our own
// Image/tool model; no Paint DLL or asset is loaded at runtime.
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/clipboard.h"
#include "../core/codec.h"
#include "clipboard_win.h"
#include "dialogs_win.h"
#include "extra_ui.h"
#include "icons.h"
#include "print_win.h"
#include "recovery.h"
#include "text_render.h"
#include "../core/image.h"
#include "../core/operation.h"
#include "../tools/tool.h"

using namespace pr;

namespace {

// Paint 11 layout (from UIA observation, 2026-11-27): top command bar with
// grouped sections, left vertical sliders, bottom status bar.  Excluded from
// the reference layout: Copilot, Layers, BG removal, Share, sign-in (cloud/AI).
constexpr int kCommandBarHeight = 112;
constexpr int kStatusHeight = 32;
constexpr int kLeftPanelWidth = 46;
constexpr int kSliderHeight = 180;

bool g_commandBarVisible = true;
bool g_rulerVisible = false;

bool HitRect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

struct CommandButton {
    RECT rect{};
    int command = 0;
    const wchar_t* label = nullptr;
    const char* icon = nullptr;  // assets/icons/<icon>
    bool toggle = false;
};

struct CommandLayout {
    std::vector<CommandButton> buttons;
    RECT color1{};
    RECT color2{};
    RECT editColors{};
    std::vector<RECT> palette;
    RECT shapeGrid{};
    std::vector<CommandButton> shapeButtons;
    RECT brushMinus{};
    RECT brushPlus{};
    RECT zoomMinus{};
    RECT zoomPlus{};
    RECT zoomSlider{};
    RECT sizeSlider{};
    RECT opacitySlider{};
    RECT fitButton{};
};

int CommandBarTop() { return 0; }  // below Win32 menu bar, in client coords

RECT ColorSwatchRect(int index) {
    // 10 columns x 2 rows, in the Colors group
    const int col = index % 10;
    const int row = index / 10;
    const int x = 0; const int y = 0; (void)x; (void)y; (void)col; (void)row;
    return RECT{};
}

struct AppState {
    std::unique_ptr<Image> image;
    std::unique_ptr<ToolContext> context;
    std::unique_ptr<CPaintTool> tool;
    COperationStock stock;
    CAttributeStore attributes;
    SelectionClipboard clipboard;
    PaintToolKind toolKind = PaintToolKind::Pencil;
    Color foreground = palette::Black();
    Color background = palette::White();
    int zoom = 1;
    int cursorX = -1;
    int cursorY = -1;
    bool showGrid = false;
    bool drawing = false;
    std::wstring filePath;
    HWND hwnd = nullptr;
};

AppState g;
ShapeKind g_shapeKind = ShapeKind::Rectangle;
bool g_shapeFilled = false;

void WholeImageOperation(OperationKind kind, const std::function<void(Image&)>& mutate);

HWND g_textEdit = nullptr;
HFONT g_textFont = nullptr;
pr::Point g_textPos{};

void FinishTextEditor(bool commit) {
    HWND editor = g_textEdit;
    g_textEdit = nullptr;  // guard against re-entrant EN_KILLFOCUS
    if (!editor) return;
    if (commit) {
        const int length = GetWindowTextLengthW(editor);
        std::wstring text(static_cast<std::size_t>(std::max(0, length)), L'\0');
        if (!text.empty())
            GetWindowTextW(editor, text.data(), static_cast<int>(text.size()) + 1);
        if (!text.empty()) {
            const pr::Point at = g_textPos;
            const int height = std::max(8, 16 * g.zoom);
            const pr::Color color = g.foreground;
            WholeImageOperation(OperationKind::Text, [&](Image& image) {
                RenderTextIntoImage(image, at, text, color, height);
            });
        }
    }
    DestroyWindow(editor);
    if (g_textFont) {
        DeleteObject(g_textFont);
        g_textFont = nullptr;
    }
    if (g.hwnd) InvalidateRect(g.hwnd, nullptr, TRUE);
}

const std::array<PaintToolKind, 18> kTools = {
    PaintToolKind::Selection, PaintToolKind::FreeFormSelection,
    PaintToolKind::Eraser, PaintToolKind::FloodFill,
    PaintToolKind::PickColor, PaintToolKind::Zoom,
    PaintToolKind::Pencil, PaintToolKind::Brush,
    PaintToolKind::Airbrush, PaintToolKind::Calligraphic,
    PaintToolKind::Text, PaintToolKind::Line,
    PaintToolKind::Curve, PaintToolKind::Polygon,
    PaintToolKind::Rectangle, PaintToolKind::Ellipse,
    PaintToolKind::RoundedRectangle, PaintToolKind::Inactive};

const Color* kPalette = palette::BasicColors;
constexpr std::size_t kPaletteCount = 28;

void RebuildTool() {
    g.context = std::make_unique<ToolContext>(*g.image, g.stock, g.attributes,
                                              g.foreground, g.background);
    g.tool = g.toolKind == PaintToolKind::AutoShape
                 ? CreateAutoShapeTool(*g.context, g_shapeKind, g_shapeFilled)
                 : CreatePaintTool(*g.context, g.toolKind, g_shapeFilled);
    if (g.tool) {
        int thickness = 1;
        if (g.attributes.GetValue(1, thickness)) g.tool->SetThickness(thickness);
        double tolerance = 0.0;
        if (g.attributes.GetValue(2, tolerance)) g.tool->SetTolerance(tolerance);
    }
}

void NewDocument(int width, int height) {
    FinishTextEditor(false);
    g.image = Image::Create(Size{width, height});
    g.stock.Clear();
    g.filePath.clear();
    g.cursorX = g.cursorY = -1;
    RebuildTool();
    UpdateThumbnailImage(g.image.get());
    UpdatePrintPreviewImage(g.image.get());
    InvalidateRect(g.hwnd, nullptr, TRUE);
}

void SetTitle() {
    std::wstring title = L"PaintRebuild - ";
    title += g.filePath.empty() ? L"Untitled" : g.filePath;
    if (g.stock.IsDirty()) title += L"*";
    SetWindowTextW(g.hwnd, title.c_str());
}

void WholeImageOperation(OperationKind kind, const std::function<void(Image&)>& mutate) {
    auto before = std::make_unique<Image>(*g.image);
    mutate(*g.image);
    auto after = std::make_unique<Image>(*g.image);
    g.stock.Commit(std::make_unique<SnapshotOperation>(kind, std::move(before), std::move(after)));
    SetTitle();
    InvalidateRect(g.hwnd, nullptr, TRUE);
}

int CommandBarHeight() { return g_commandBarVisible ? kCommandBarHeight : 0; }

RECT CanvasRect() {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = CommandBarHeight() + 4;
    const int left = kLeftPanelWidth + 4;
    return RECT{left, top, client.right - 4, client.bottom - kStatusHeight - 4};
}

pr::Point ToImagePoint(int x, int y) {
    const RECT canvas = CanvasRect();
    return pr::Point{(x - canvas.left) / g.zoom, (y - canvas.top) / g.zoom};
}

void FillRectValue(HDC hdc, RECT rect, HBRUSH brush) { FillRect(hdc, &rect, brush); }

void DrawCanvas(HDC hdc, const RECT& canvas) {
    const int w = g.image->width();
    const int h = g.image->height();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;  // top-down DIB (our rows are already top-down)
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(hdc, canvas.left, canvas.top, w * g.zoom, h * g.zoom,
                  0, 0, w, h, g.image->Bitmap(), &info, DIB_RGB_COLORS, SRCCOPY);

    if (g.showGrid && g.zoom >= 4) {
        HPEN grid = CreatePen(PS_SOLID, 1, RGB(190, 190, 190));
        const HPEN old = static_cast<HPEN>(SelectObject(hdc, grid));
        const int step = 8 * g.zoom;
        for (int x = canvas.left; x <= canvas.left + w * g.zoom; x += step) {
            MoveToEx(hdc, x, canvas.top, nullptr);
            LineTo(hdc, x, canvas.top + h * g.zoom);
        }
        for (int y = canvas.top; y <= canvas.top + h * g.zoom; y += step) {
            MoveToEx(hdc, canvas.left, y, nullptr);
            LineTo(hdc, canvas.left + w * g.zoom, y);
        }
        SelectObject(hdc, old);
        DeleteObject(grid);
    }

    if (g.tool && (g.tool->Kind() == PaintToolKind::Selection ||
                   g.tool->Kind() == PaintToolKind::FreeFormSelection)) {
        const Rect r = g.tool->SelectionRect();
        if (r.w > 0 && r.h > 0) {
            HPEN dash = CreatePen(PS_DASH, 1, RGB(0, 0, 0));
            const HPEN old = static_cast<HPEN>(SelectObject(hdc, dash));
            const int left = canvas.left + r.x * g.zoom;
            const int top = canvas.top + r.y * g.zoom;
            Rectangle(hdc, left, top, left + r.w * g.zoom, top + r.h * g.zoom);
            SelectObject(hdc, old);
            DeleteObject(dash);
        }
    }
}

struct GroupLayout {
    const wchar_t* name = nullptr;
    int left = 0;
    int right = 0;
};

static void DrawGroupLabel(HDC hdc, const GroupLayout& g_, int top, int barH) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(60, 60, 60));
    RECT r{g_.left, top + barH - 18, g_.right, top + barH - 2};
    DrawTextW(hdc, g_.name, -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

static void DrawVSeparator(HDC hdc, int x, int top, int bottom) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(210, 210, 210));
    HGDIOBJ old = SelectObject(hdc, pen);
    MoveToEx(hdc, x, top + 6, nullptr);
    LineTo(hdc, x, bottom - 6);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static void DrawCmdBtn(HDC hdc, const RECT& r, const char* icon, const wchar_t* label,
                       bool selected, bool iconOnly) {
    COLORREF bg = selected ? RGB(196, 220, 246) : RGB(250, 250, 250);
    HBRUSH br = CreateSolidBrush(bg);
    FillRectValue(hdc, r, br);
    DeleteObject(br);
    HPEN pen = CreatePen(PS_SOLID, 1, selected ? RGB(100, 150, 220) : RGB(200, 200, 200));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    if (icon) {
        if (Image* img = GetIcon(icon)) {
            const int isz = iconOnly ? 24 : 20;
            const int ix = r.left + (r.right - r.left - isz) / 2;
            const int iy = r.top + (iconOnly ? (r.bottom - r.top - isz) / 2 : 4);
            DrawToolIcon(hdc, *img, ix, iy, isz);
        }
    }
    if (label && !iconOnly) {
        SetBkMode(hdc, TRANSPARENT);
        RECT tr{r.left, r.top + 24, r.right, r.bottom - 1};
        DrawTextW(hdc, label, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
}

CommandLayout BuildCommandLayout() {
    CommandLayout L;
    const int top = CommandBarTop() + 4;
    int x = 8;

    auto addBtn = [&](int bx, int by, int bw, int bh, int cmd, const wchar_t* label,
                      const char* icon, bool sel = false, bool iconOnly = false) {
        CommandButton b;
        b.rect = RECT{bx, by, bx + bw, by + bh};
        b.command = cmd; b.label = label; b.icon = icon; b.toggle = sel;
        L.buttons.push_back(b);
    };

    // Selection group
    const int selLeft = x;
    addBtn(x + 10, top + 8, 44, 62, 800, L"Select", "select.png", g.toolKind == PaintToolKind::Selection);
    addBtn(x + 58, top + 8, 44, 62, 801, L"Free-form", "freeform.png", g.toolKind == PaintToolKind::FreeFormSelection);
    x += 112;

    // Image group
    const int imgLeft = x;
    addBtn(x + 6, top + 8, 44, 40, 407, L"Crop", "select.png", false, true);
    addBtn(x + 54, top + 8, 44, 40, 405, L"Resize", "resize.png", false, true);
    addBtn(x + 102, top + 8, 44, 40, 406, L"Rotate", "rotate.png", false, true);
    addBtn(x + 6, top + 52, 44, 40, 409, L"Flip", "flip.png", false, true);
    addBtn(x + 54, top + 52, 90, 40, 403, L"Properties", "resize.png");
    x += 154;

    // Tools group (2 rows x 3) - matches Paint 11 exactly
    const int toolsLeft = x;
    const int tx[] = {x + 6, x + 48, x + 90};
    addBtn(tx[0], top + 8, 38, 38, 806, L"Pencil", "pencil.png", g.toolKind == PaintToolKind::Pencil, true);
    addBtn(tx[1], top + 8, 38, 38, 803, L"Fill", "fill.png", g.toolKind == PaintToolKind::FloodFill, true);
    addBtn(tx[2], top + 8, 38, 38, 810, L"Text", "text.png", g.toolKind == PaintToolKind::Text, true);
    addBtn(tx[0], top + 50, 38, 38, 802, L"Eraser", "eraser.png", g.toolKind == PaintToolKind::Eraser, true);
    addBtn(tx[1], top + 50, 38, 38, 804, L"Picker", "picker.png", g.toolKind == PaintToolKind::PickColor, true);
    addBtn(tx[2], top + 50, 38, 38, 805, L"Zoom", "zoom.png", g.toolKind == PaintToolKind::Zoom, true);
    x += 136;

    // Brushes group
    const int brushLeft = x;
    addBtn(x + 8, top + 8, 52, 62, 807, L"Brushes", "brush.png",
           g.toolKind == PaintToolKind::Brush || g.toolKind == PaintToolKind::Airbrush || g.toolKind == PaintToolKind::Calligraphic);
    x += 76;

    // Shapes group
    const int shapesLeft = x;
    L.shapeGrid = RECT{x + 4, top + 6, x + 224, top + 88};
    static const struct { const char* icon; ShapeKind shape; int cmd; } shapeDefs[] = {
        {"line.png", ShapeKind::Rectangle, 811}, {"curve.png", ShapeKind::Rectangle, 812},
        {"oval.png", ShapeKind::Ellipse, 815}, {"rectangle.png", ShapeKind::Rectangle, 814},
        {"rounded_rect.png", ShapeKind::RoundedRectangle, 816}, {"polygon.png", ShapeKind::Rectangle, 813},
        {"triangle.png", ShapeKind::Triangle, 600 + (int)ShapeKind::Triangle},
        {"right_triangle.png", ShapeKind::Triangle, 600 + (int)ShapeKind::Triangle},
        {"diamond.png", ShapeKind::Diamond, 600 + (int)ShapeKind::Diamond},
        {"pentagon.png", ShapeKind::Pentagon, 600 + (int)ShapeKind::Pentagon},
        {"hexagon.png", ShapeKind::Hexagon, 600 + (int)ShapeKind::Hexagon},
        {"right_arrow.png", ShapeKind::RightArrow, 600 + (int)ShapeKind::RightArrow},
        {"left_arrow.png", ShapeKind::LeftArrow, 600 + (int)ShapeKind::LeftArrow},
        {"up_arrow.png", ShapeKind::UpArrow, 600 + (int)ShapeKind::UpArrow},
        {"down_arrow.png", ShapeKind::DownArrow, 600 + (int)ShapeKind::DownArrow},
        {"four_point_star.png", ShapeKind::FourPointedStar, 600 + (int)ShapeKind::FourPointedStar},
        {"five_point_star.png", ShapeKind::FivePointedStar, 600 + (int)ShapeKind::FivePointedStar},
        {"six_point_star.png", ShapeKind::SixPointedStar, 600 + (int)ShapeKind::SixPointedStar},
        {"rounded_rect_callout.png", ShapeKind::RoundedRectangularCallout, 600 + (int)ShapeKind::RoundedRectangularCallout},
        {"oval_callout.png", ShapeKind::OvalCallout, 600 + (int)ShapeKind::OvalCallout},
        {"cloud_callout.png", ShapeKind::CloudCallout, 600 + (int)ShapeKind::CloudCallout},
        {"heart.png", ShapeKind::Heart, 600 + (int)ShapeKind::Heart},
        {"lightning.png", ShapeKind::Lightning, 600 + (int)ShapeKind::Lightning},
    };
    for (int i = 0; i < 23; ++i) {
        const int col = i % 7; const int row = i / 7;
        CommandButton sb;
        sb.rect = RECT{L.shapeGrid.left + col * 31, L.shapeGrid.top + row * 27,
                       L.shapeGrid.left + col * 31 + 28, L.shapeGrid.top + row * 27 + 24};
        sb.command = shapeDefs[i].cmd; sb.icon = shapeDefs[i].icon;
        if (sb.command == 811) sb.toggle = (g.toolKind == PaintToolKind::Line);
        else if (sb.command == 812) sb.toggle = (g.toolKind == PaintToolKind::Curve);
        else if (sb.command == 813) sb.toggle = (g.toolKind == PaintToolKind::Polygon);
        else if (sb.command == 814) sb.toggle = (g.toolKind == PaintToolKind::Rectangle);
        else if (sb.command == 815) sb.toggle = (g.toolKind == PaintToolKind::Ellipse);
        else if (sb.command == 816) sb.toggle = (g.toolKind == PaintToolKind::RoundedRectangle);
        else sb.toggle = (g.toolKind == PaintToolKind::AutoShape && g_shapeKind == shapeDefs[i].shape);
        L.shapeButtons.push_back(sb);
    }
    x += 232;
    addBtn(x + 4, top + 10, 52, 36, 9001, L"Outline", "shape_outline.png", !g_shapeFilled);
    addBtn(x + 4, top + 50, 52, 36, 9002, L"Fill", "shape_fill.png", g_shapeFilled);
    x += 64;

    // Size group
    const int sizeLeft = x;
    addBtn(x + 6, top + 18, 44, 56, 9003, L"Size", "size.png");
    x += 60;

    // Colors group
    const int colorsLeft = x;
    L.color1 = RECT{x + 4, top + 10, x + 44, top + 50};
    L.color2 = RECT{x + 4, top + 54, x + 44, top + 94};
    for (int i = 0; i < 20; ++i) {
        const int col = i % 10; const int row = i / 10;
        L.palette.push_back(RECT{x + 54 + col * 22, top + 14 + row * 24,
                                 x + 54 + col * 22 + 20, top + 14 + row * 24 + 20});
    }
    L.editColors = RECT{x + 280, top + 28, x + 322, top + 70};
    x += 330;

    (void)selLeft; (void)imgLeft; (void)toolsLeft; (void)brushLeft;
    (void)shapesLeft; (void)sizeLeft; (void)colorsLeft;
    return L;
}

void DrawCommandBar(HDC hdc) {
    if (!g_commandBarVisible) return;
    RECT client; GetClientRect(g.hwnd, &client);
    const int top = CommandBarTop();
    const int h = kCommandBarHeight;
    HBRUSH bg = CreateSolidBrush(RGB(243, 243, 243));
    FillRectValue(hdc, RECT{0, top, client.right, top + h}, bg);
    DeleteObject(bg);
    HPEN line = CreatePen(PS_SOLID, 1, RGB(218, 218, 218));
    HGDIOBJ oldPen = SelectObject(hdc, line);
    MoveToEx(hdc, 0, top + h - 1, nullptr); LineTo(hdc, client.right, top + h - 1);
    SelectObject(hdc, oldPen); DeleteObject(line);

    CommandLayout L = BuildCommandLayout();
    SetBkMode(hdc, TRANSPARENT);

    // Group separators and labels
    struct GL { const wchar_t* n; int l; int r; } groups[] = {
        {L"Selection", 8, 118}, {L"Image", 120, 272}, {L"Tools", 274, 408},
        {L"Brushes", 410, 484}, {L"Shapes", 486, 780}, {L"Size", 782, 840},
        {L"Colors", 842, 1172},
    };
    for (auto& g_ : groups) {
        DrawVSeparator(hdc, g_.r, top, top + h);
        RECT lr{g_.l, top + h - 20, g_.r, top + h - 2};
        SetTextColor(hdc, RGB(70, 70, 70));
        DrawTextW(hdc, g_.n, -1, &lr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    for (auto& b : L.buttons) DrawCmdBtn(hdc, b.rect, b.icon, b.label, b.toggle, false);
    for (auto& sb : L.shapeButtons) {
        bool sel = sb.toggle;
        HBRUSH br = CreateSolidBrush(sel ? RGB(196, 220, 246) : RGB(243, 243, 243));
        FillRectValue(hdc, sb.rect, br); DeleteObject(br);
        if (sel) { HPEN p = CreatePen(PS_SOLID, 1, RGB(100,150,220)); HGDIOBJ op=SelectObject(hdc,p); HGDIOBJ ob=SelectObject(hdc,GetStockObject(NULL_BRUSH)); Rectangle(hdc,sb.rect.left,sb.rect.top,sb.rect.right,sb.rect.bottom); SelectObject(hdc,ob); SelectObject(hdc,op); DeleteObject(p); }
        if (Image* img = GetIcon(sb.icon)) DrawToolIcon(hdc, *img, sb.rect.left + 2, sb.rect.top + 1, 22);
    }

    // Colors: Color 1 / Color 2
    auto drawSwatch = [&](RECT r, Color c, bool sel, const wchar_t* lbl) {
        HBRUSH br = CreateSolidBrush(sel ? RGB(220,235,250) : RGB(243,243,243));
        FillRectValue(hdc, r, br); DeleteObject(br);
        if (sel) { HPEN p=CreatePen(PS_SOLID,2,RGB(0,120,215)); HGDIOBJ op=SelectObject(hdc,p); HGDIOBJ ob=SelectObject(hdc,GetStockObject(NULL_BRUSH)); Rectangle(hdc,r.left,r.top,r.right,r.bottom); SelectObject(hdc,ob); SelectObject(hdc,op); DeleteObject(p); }
        RECT inner{r.left + 6, r.top + 4, r.right - 6, r.bottom - 16};
        HBRUSH cb = CreateSolidBrush(RGB(Red(c), Green(c), Blue(c)));
        FillRectValue(hdc, inner, cb); DeleteObject(cb);
        FrameRect(hdc, &inner, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        RECT lr{r.left, r.bottom - 14, r.right, r.bottom};
        SetTextColor(hdc, RGB(60,60,60));
        DrawTextW(hdc, lbl, -1, &lr, DT_CENTER | DT_SINGLELINE);
    };
    drawSwatch(L.color1, g.foreground, true, L"Color 1");
    drawSwatch(L.color2, g.background, false, L"Color 2");

    for (int i = 0; i < 20 && i < static_cast<int>(L.palette.size()); ++i) {
        RECT r = L.palette[i];
        Color c = (i < static_cast<int>(kPaletteCount)) ? kPalette[i] : MakeColor(255,255,255);
        HBRUSH cb = CreateSolidBrush(RGB(Red(c), Green(c), Blue(c)));
        FillRectValue(hdc, r, cb); DeleteObject(cb);
        FrameRect(hdc, &r, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    }
    DrawCmdBtn(hdc, L.editColors, "colors.png", L"Edit", false, false);
}

void DrawLeftPanel(HDC hdc) {
    RECT client; GetClientRect(g.hwnd, &client);
    const int top = CommandBarHeight();
    const int bottom = client.bottom - kStatusHeight;
    HBRUSH bg = CreateSolidBrush(RGB(243, 243, 243));
    FillRectValue(hdc, RECT{0, top, kLeftPanelWidth, bottom}, bg);
    DeleteObject(bg);
    HPEN line = CreatePen(PS_SOLID, 1, RGB(218, 218, 218));
    HGDIOBJ op = SelectObject(hdc, line);
    MoveToEx(hdc, kLeftPanelWidth - 1, top, nullptr); LineTo(hdc, kLeftPanelWidth - 1, bottom);
    SelectObject(hdc, op); DeleteObject(line);

    int thickness = 1; g.attributes.GetValue(1, thickness);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(60,60,60));
    RECT sl{2, top + 8, kLeftPanelWidth - 2, top + 24};
    DrawTextW(hdc, L"Size", -1, &sl, DT_CENTER | DT_SINGLELINE);
    // Size slider track
    const int sx = kLeftPanelWidth / 2 - 2;
    const int sy1 = top + 30, sy2 = top + 30 + kSliderHeight;
    HBRUSH track = CreateSolidBrush(RGB(200,200,200));
    FillRectValue(hdc, RECT{sx, sy1, sx + 4, sy2}, track); DeleteObject(track);
    const int thumbY = sy2 - (thickness * (kSliderHeight - 10) / 40) - 5;
    HBRUSH thumb = CreateSolidBrush(RGB(0,120,215));
    FillRectValue(hdc, RECT{sx - 6, thumbY, sx + 10, thumbY + 10}, thumb); DeleteObject(thumb);

    RECT ol{2, sy2 + 16, kLeftPanelWidth - 2, sy2 + 32};
    DrawTextW(hdc, L"Opacity", -1, &ol, DT_CENTER | DT_SINGLELINE);
    const int oy1 = sy2 + 38, oy2 = oy1 + kSliderHeight;
    HBRUSH track2 = CreateSolidBrush(RGB(200,200,200));
    FillRectValue(hdc, RECT{sx, oy1, sx + 4, oy2}, track2); DeleteObject(track2);
    HBRUSH thumb2 = CreateSolidBrush(RGB(0,120,215));
    FillRectValue(hdc, RECT{sx - 6, oy2 - 10, sx + 10, oy2}, thumb2); DeleteObject(thumb2);
}

void DrawStatus(HDC hdc) {
    RECT client; GetClientRect(g.hwnd, &client);
    const int top = client.bottom - kStatusHeight;
    HBRUSH bg = CreateSolidBrush(RGB(243, 243, 243));
    FillRectValue(hdc, RECT{0, top, client.right, client.bottom}, bg);
    DeleteObject(bg);
    HPEN line = CreatePen(PS_SOLID, 1, RGB(218, 218, 218));
    HGDIOBJ op = SelectObject(hdc, line);
    MoveToEx(hdc, 0, top, nullptr); LineTo(hdc, client.right, top);
    SelectObject(hdc, op); DeleteObject(line);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(50,50,50));

    // Left: cursor pos and canvas size
    std::wstring left = std::to_wstring(g.cursorX >= 0 ? g.cursorX : 0) + L", " +
                        std::to_wstring(g.cursorY >= 0 ? g.cursorY : 0) + L"px";
    TextOutW(hdc, 12, top + 8, left.c_str(), static_cast<int>(left.size()));
    std::wstring size = std::to_wstring(g.image->width()) + L" x " +
                        std::to_wstring(g.image->height()) + L"px";
    TextOutW(hdc, 140, top + 8, size.c_str(), static_cast<int>(size.size()));

    // Right: zoom controls matching Paint 11
    const int zy = top + 4;
    auto btn = [&](int bx, const wchar_t* t, int w = 28) {
        RECT r{bx, zy, bx + w, zy + 24};
        HBRUSH bbr = CreateSolidBrush(RGB(243,243,243));
        FillRectValue(hdc, r, bbr); DeleteObject(bbr);
        DrawTextW(hdc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return r;
    };
    btn(client.right - 320, L"\u229F", 30); // fit
    TextOutW(hdc, client.right - 280, top + 8, (std::to_wstring(g.zoom*100)+L"%").c_str(),
             static_cast<int>((std::to_wstring(g.zoom*100)+L"%").size()));
    btn(client.right - 220, L"\u2212");
    // slider track
    const int sl = client.right - 186, sr = client.right - 70;
    HBRUSH trk = CreateSolidBrush(RGB(190,190,190));
    FillRectValue(hdc, RECT{sl, top + 15, sr, top + 18}, trk); DeleteObject(trk);
    const int zx = sl + (g.zoom - 1) * (sr - sl) / 15;
    HBRUSH th = CreateSolidBrush(RGB(0,120,215));
    FillRectValue(hdc, RECT{zx-4, top+9, zx+4, top+23}, th); DeleteObject(th);
    btn(client.right - 64, L"+");
}

void DrawRuler(HDC hdc, const RECT& canvas) {
    if (!g_rulerVisible) return;
    const int ruler = 18;
    HBRUSH face = CreateSolidBrush(RGB(235, 235, 235));
    FillRectValue(hdc, RECT{canvas.left, canvas.top, canvas.right, canvas.top + ruler}, face);
    FillRectValue(hdc, RECT{canvas.left, canvas.top, canvas.left + ruler, canvas.bottom}, face);
    DeleteObject(face);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
    const HPEN old = static_cast<HPEN>(SelectObject(hdc, pen));
    for (int px = 0; px <= g.image->width(); px += 10) {
        const int x = canvas.left + px * g.zoom;
        if (x > canvas.right) break;
        const int length = (px % 100 == 0) ? 10 : 5;
        MoveToEx(hdc, x, canvas.top + ruler, nullptr);
        LineTo(hdc, x, canvas.top + ruler - length);
        if (px % 100 == 0 && px > 0) {
            const std::wstring label = std::to_wstring(px);
            TextOutW(hdc, x + 2, canvas.top + 1, label.c_str(), static_cast<int>(label.size()));
        }
    }
    for (int py = 0; py <= g.image->height(); py += 10) {
        const int y = canvas.top + py * g.zoom;
        if (y > canvas.bottom) break;
        const int length = (py % 100 == 0) ? 10 : 5;
        MoveToEx(hdc, canvas.left + ruler, y, nullptr);
        LineTo(hdc, canvas.left + ruler - length, y);
    }
    SelectObject(hdc, old);
    DeleteObject(pen);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ old = SelectObject(mem, bitmap);
    FillRect(mem, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    const RECT canvas = CanvasRect();
    DrawCanvas(mem, canvas);
    DrawRuler(mem, canvas);
    DrawCommandBar(mem);
    DrawLeftPanel(mem);
    DrawStatus(mem);
    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void DoCommand(WORD id);

bool HandleCommandBarClick(int x, int y, bool rightButton) {
    if (!g_commandBarVisible || y >= kCommandBarHeight) return false;
    CommandLayout L = BuildCommandLayout();
    for (auto& b : L.buttons) {
        if (HitRect(b.rect, x, y)) { DoCommand(static_cast<WORD>(b.command)); return true; }
    }
    for (auto& sb : L.shapeButtons) {
        if (HitRect(sb.rect, x, y)) { DoCommand(static_cast<WORD>(sb.command)); return true; }
    }
    if (HitRect(L.color1, x, y) || HitRect(L.color2, x, y)) {
        std::swap(g.foreground, g.background); RebuildTool(); InvalidateRect(g.hwnd, nullptr, TRUE); return true;
    }
    for (int i = 0; i < static_cast<int>(L.palette.size()) && i < 20; ++i) {
        if (HitRect(L.palette[i], x, y)) {
            Color c = (i < static_cast<int>(kPaletteCount)) ? kPalette[i] : MakeColor(255,255,255);
            if (rightButton) g.background = c; else g.foreground = c;
            RebuildTool(); InvalidateRect(g.hwnd, nullptr, TRUE); return true;
        }
    }
    if (HitRect(L.editColors, x, y)) { DoCommand(501); return true; }
    return true; // consume clicks in command bar area
}

bool HandleLeftPanelClick(int x, int y) {
    if (x >= kLeftPanelWidth) return false;
    RECT client; GetClientRect(g.hwnd, &client);
    const int top = CommandBarHeight();
    if (y < top || y >= client.bottom - kStatusHeight) return false;
    const int sy1 = top + 30, sy2 = top + 30 + kSliderHeight;
    if (y >= sy1 && y <= sy2) {
        int t = 40 - (y - sy1) * 40 / (kSliderHeight - 10);
        t = std::max(1, std::min(40, t));
        g.attributes.SetValue(1, t);
        if (g.tool) g.tool->SetThickness(static_cast<unsigned>(t));
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return true;
    }
    return true;
}

bool HandleStatusBarClick(int x, int y) {
    RECT client; GetClientRect(g.hwnd, &client);
    const int top = client.bottom - kStatusHeight;
    if (y < top) return false;
    // fit, zoom-, slider, zoom+
    if (x >= client.right - 320 && x < client.right - 290) { DoCommand(311); return true; } // fit to window
    if (x >= client.right - 220 && x < client.right - 192) { g.zoom = std::max(1, g.zoom / 2); InvalidateRect(g.hwnd,nullptr,TRUE); return true; }
    if (x >= client.right - 186 && x < client.right - 70) {
        int z = 1 + (x - (client.right - 186)) * 15 / (116);
        g.zoom = std::max(1, std::min(16, z)); InvalidateRect(g.hwnd,nullptr,TRUE); return true;
    }
    if (x >= client.right - 64 && x < client.right - 36) { g.zoom = std::min(16, g.zoom * 2); InvalidateRect(g.hwnd,nullptr,TRUE); return true; }
    return true;
}

void SelectFile(bool save) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Paint images\0*.bmp;*.png;*.jpg;*.jpeg;*.gif;*.tif\0BMP\0*.bmp\0PNG\0*.png\0JPEG\0*.jpg\0All files\0*.*\0\0";
    ofn.Flags = save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
    ofn.lpstrDefExt = L"bmp";
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return;
    if (save) {
        const std::wstring p(path);
        bool written = false;
        if (DetectFormat(p) == ImageFormat::Bmp) written = SaveBmp(*g.image, p);
        else written = SaveImageWic(*g.image, p);
        if (written) {
            g.filePath = p;
            g.stock.MarkSaved();
            ClearRecovery(FindLatestRecovery());
            SetTitle();
        } else {
            MessageBoxW(g.hwnd, L"The image could not be saved.", L"PaintRebuild", MB_OK);
        }
    } else {
        std::unique_ptr<Image> loaded;
        const std::wstring p(path);
        if (DetectFormat(p) == ImageFormat::Bmp) loaded = LoadBmp(p);
        else loaded = LoadImageWic(p);
        if (loaded) {
            g.image = std::move(loaded);
            g.stock.Clear();
            g.filePath = p;
            RebuildTool();
            UpdateThumbnailImage(g.image.get());
    UpdatePrintPreviewImage(g.image.get());
            SetTitle();
            InvalidateRect(g.hwnd, nullptr, TRUE);
        } else {
            MessageBoxW(g.hwnd, L"The image could not be opened.", L"PaintRebuild", MB_OK);
        }
    }
}

void DoCommand(WORD id) {
    if (id >= 600 && id <= 618) {
        g_shapeKind = static_cast<ShapeKind>(id - 600);
        g.toolKind = PaintToolKind::AutoShape;
        RebuildTool();
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return;
    }
    if (id == 307) {
        g_rulerVisible = !g_rulerVisible;
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return;
    }
    if (id == 308) {
        g_commandBarVisible = !g_commandBarVisible;
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return;
    }
    if (id == 9001) { g_shapeFilled = false; RebuildTool(); InvalidateRect(g.hwnd, nullptr, TRUE); return; }
    if (id == 9002) { g_shapeFilled = true; RebuildTool(); InvalidateRect(g.hwnd, nullptr, TRUE); return; }
    if (id == 9003) {
        int t = 1; g.attributes.GetValue(1, t);
        t = (t < 3) ? 3 : (t < 5) ? 5 : (t < 8) ? 8 : 1;
        g.attributes.SetValue(1, t); if (g.tool) g.tool->SetThickness(static_cast<unsigned>(t));
        InvalidateRect(g.hwnd, nullptr, TRUE); return;
    }
    if (id >= 800 && id <= 817) {
        g.toolKind = kTools[id - 800];
        RebuildTool();
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return;
    }
    if (id >= 700 && id <= 708) {
        static const PaintToolKind brushTools[] = {
            PaintToolKind::Pencil, PaintToolKind::Brush, PaintToolKind::Airbrush,
            PaintToolKind::Calligraphic, PaintToolKind::OilBrush, PaintToolKind::Watercolor,
            PaintToolKind::Marker, PaintToolKind::Crayon, PaintToolKind::NaturalPencil};
        g.toolKind = brushTools[id - 700];
        RebuildTool();
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return;
    }
    switch (id) {
        case 100: NewDocument(800, 600); SetTitle(); break;
        case 101: SelectFile(false); break;
        case 102:
        case 103: SelectFile(true); break;
        case 104: PostQuitMessage(0); break;
        case 105: SetAsWallpaper(g.image.get(), false); break;
        case 106: SetAsWallpaper(g.image.get(), true); break;
        case 107: PrintImage(g.hwnd, *g.image); break;
        case 108: TogglePrintPreview(g.hwnd, g.image.get()); break;
        case 309: ToggleFullScreen(g.hwnd); break;
        case 310: ToggleThumbnail(g.hwnd, g.image.get()); break;
        case 200:
            if (g.stock.Undo(*g.image)) { SetTitle(); InvalidateRect(g.hwnd, nullptr, TRUE); }
            break;
        case 201:
            if (g.stock.Redo(*g.image)) { SetTitle(); InvalidateRect(g.hwnd, nullptr, TRUE); }
            break;
        case 202:
        case 203: {
            Rect r = g.tool ? g.tool->SelectionRect() : Rect{0, 0, g.image->width(), g.image->height()};
            if (r.w <= 0 || r.h <= 0) r = Rect{0, 0, g.image->width(), g.image->height()};
            const bool copied = CopyImageToClipboard(g.hwnd, *g.image, r);
            if (!copied) g.clipboard.Copy(*g.image, r);  // headless fallback
            if (id == 202 && (copied || g.clipboard.HasContent())) {
                WholeImageOperation(OperationKind::Erase, [&](Image& image) {
                    image.FillRect(r, g.background);
                });
            }
            break;
        }
        case 204: {
            auto pasted = PasteImageFromClipboard();
            if (pasted) {
                WholeImageOperation(OperationKind::FillRectangle, [&](Image& image) {
                    image.Blit(*pasted, Point{0, 0});
                });
            } else if (g.clipboard.HasContent()) {
                WholeImageOperation(OperationKind::FillRectangle, [&](Image& image) {
                    g.clipboard.Paste(image, Point{0, 0});
                });
            }
            break;
        }
        case 205:
            WholeImageOperation(OperationKind::Clear, [&](Image& image) {
                image.Clear(g.background);
            });
            break;
        case 206: {
            // Select All must drive a SelectionTool, not whatever tool is active.
            g.toolKind = PaintToolKind::Selection;
            g.tool = std::make_unique<SelectionTool>(*g.context);
            g.tool->OnStylusDown(Point{0, 0});
            g.tool->OnStylusMove(Point{g.image->width() - 1, g.image->height() - 1});
            g.tool->OnStylusUp(Point{g.image->width() - 1, g.image->height() - 1});
            InvalidateRect(g.hwnd, nullptr, TRUE);
            break;
        }
        case 300: g.zoom = 1; InvalidateRect(g.hwnd, nullptr, TRUE); break;
        case 301: g.zoom = 2; InvalidateRect(g.hwnd, nullptr, TRUE); break;
        case 302: g.zoom = 4; InvalidateRect(g.hwnd, nullptr, TRUE); break;
        case 303: g.zoom = 8; InvalidateRect(g.hwnd, nullptr, TRUE); break;
        case 304: g.showGrid = !g.showGrid; InvalidateRect(g.hwnd, nullptr, TRUE); break;
        case 311: {
            const RECT c = CanvasRect();
            const int cw = std::max(1, static_cast<int>(c.right - c.left));
            const int ch = std::max(1, static_cast<int>(c.bottom - c.top));
            int z = std::min(cw / std::max(1, g.image->width()), ch / std::max(1, g.image->height()));
            g.zoom = std::max(1, std::min(16, z));
            InvalidateRect(g.hwnd, nullptr, TRUE);
            break;
        }
        case 400: WholeImageOperation(OperationKind::InvertColors, [](Image& i) { i.InvertColors(); }); break;
        case 401: WholeImageOperation(OperationKind::FlipHorizontal, [](Image& i) { i.HorizontalFlip(); }); break;
        case 402: WholeImageOperation(OperationKind::FlipVertical, [](Image& i) { i.VerticalFlip(); }); break;
        case 403: WholeImageOperation(OperationKind::Rotate, [](Image& i) { i.Rotate(1.5707963267948966); }); break;
        case 404: {
            int width = g.image->width();
            int height = g.image->height();
            if (ShowAttributesDialog(g.hwnd, width, height) && width > 0 && height > 0 &&
                (width != g.image->width() || height != g.image->height())) {
                WholeImageOperation(OperationKind::Resize, [&](Image& image) {
                    image.Resize(Size{width, height}, ResizeMode::Nearest, ResizeDirection::Both);
                });
            }
            break;
        }
        case 405: {
            StretchSkewOptions options;
            if (!ShowStretchSkewDialog(g.hwnd, options)) break;
            int width = g.image->width() * std::max(1, options.horizontalPercent) / 100;
            int height = g.image->height() * std::max(1, options.verticalPercent) / 100;
            if (width < 1) width = 1;
            if (height < 1) height = 1;
            WholeImageOperation(OperationKind::Resize, [&](Image& image) {
                image.Resize(Size{width, height}, ResizeMode::Bilinear, ResizeDirection::Both);
                if (options.skewHorizontal != 0) image.HorizontalSkew(options.skewHorizontal);
                if (options.skewVertical != 0) image.VerticalSkew(options.skewVertical);
            });
            break;
        }
        case 406: {
            FlipRotateOptions options;
            if (!ShowFlipRotateDialog(g.hwnd, options)) break;
            const auto choice = options.choice;
            WholeImageOperation(OperationKind::FlipHorizontal, [&](Image& image) {
                switch (choice) {
                    case FlipRotateChoice::FlipHorizontal: image.HorizontalFlip(); break;
                    case FlipRotateChoice::FlipVertical: image.VerticalFlip(); break;
                    case FlipRotateChoice::Rotate90: image.Rotate(1.5707963267948966); break;
                    case FlipRotateChoice::Rotate180: image.Rotate(3.1415926535897932); break;
                    case FlipRotateChoice::Rotate270: image.Rotate(4.7123889803846899); break;
                }
            });
            break;
        }
        case 501: {
            CHOOSECOLORW color{};
            color.lStructSize = sizeof(color);
            color.hwndOwner = g.hwnd;
            color.rgbResult = RGB(Red(g.foreground), Green(g.foreground), Blue(g.foreground));
            static COLORREF customColors[16] = {};
            color.lpCustColors = customColors;
            color.Flags = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorW(&color)) {
                g.foreground = MakeColor(GetRValue(color.rgbResult), GetGValue(color.rgbResult),
                                         GetBValue(color.rgbResult), 255);
                RebuildTool();
                InvalidateRect(g.hwnd, nullptr, TRUE);
            }
            break;
        }
        case 500:
            MessageBoxW(g.hwnd, L"PaintRebuild — a clean-room Paint workflow rebuild in C++/Win32.",
                        L"About", MB_OK);
            break;
        default: break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g.hwnd = hwnd;
            HMENU menu = CreateMenu();
            HMENU file = CreatePopupMenu();
            AppendMenuW(file, MF_STRING, 100, L"&New");
            AppendMenuW(file, MF_STRING, 101, L"&Open...");
            AppendMenuW(file, MF_STRING, 102, L"&Save");
            AppendMenuW(file, MF_STRING, 103, L"Save &As...");
            AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(file, MF_STRING, 105, L"Set as Wallpaper (stretched)");
            AppendMenuW(file, MF_STRING, 106, L"Set as Wallpaper (tiled)");
            AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(file, MF_STRING, 107, L"&Print...");
            AppendMenuW(file, MF_STRING, 108, L"Print &Preview");
            AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(file, MF_STRING, 104, L"E&xit");
            HMENU edit = CreatePopupMenu();
            AppendMenuW(edit, MF_STRING, 200, L"&Undo\tCtrl+Z");
            AppendMenuW(edit, MF_STRING, 201, L"&Redo\tCtrl+Y");
            AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(edit, MF_STRING, 202, L"Cu&t\tCtrl+X");
            AppendMenuW(edit, MF_STRING, 203, L"&Copy\tCtrl+C");
            AppendMenuW(edit, MF_STRING, 204, L"&Paste\tCtrl+V");
            AppendMenuW(edit, MF_STRING, 205, L"Clear &Image");
            AppendMenuW(edit, MF_STRING, 206, L"Select &All\tCtrl+A");
            HMENU view = CreatePopupMenu();
            AppendMenuW(view, MF_STRING, 300, L"Zoom &100%");
            AppendMenuW(view, MF_STRING, 301, L"Zoom &200%");
            AppendMenuW(view, MF_STRING, 302, L"Zoom &400%");
            AppendMenuW(view, MF_STRING, 303, L"Zoom &800%");
            AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(view, MF_STRING, 304, L"Show &Grid");
            AppendMenuW(view, MF_STRING, 307, L"Show &Ruler");
            AppendMenuW(view, MF_STRING, 308, L"Show &Ribbon");
            AppendMenuW(view, MF_STRING, 309, L"&Full screen");
            AppendMenuW(view, MF_STRING, 310, L"&Thumbnail");
            HMENU imageMenu = CreatePopupMenu();
            AppendMenuW(imageMenu, MF_STRING, 400, L"&Invert Colors");
            AppendMenuW(imageMenu, MF_STRING, 401, L"Flip &Horizontal");
            AppendMenuW(imageMenu, MF_STRING, 402, L"Flip &Vertical");
            AppendMenuW(imageMenu, MF_STRING, 403, L"Rotate &90°");
            AppendMenuW(imageMenu, MF_STRING, 404, L"&Attributes...");
            AppendMenuW(imageMenu, MF_STRING, 405, L"&Stretch / Skew...");
            AppendMenuW(imageMenu, MF_STRING, 406, L"Flip / &Rotate...");
            static const wchar_t* shapeLabels[] = {
                L"Rectangle", L"Rounded rectangle", L"Oval", L"Right triangle",
                L"Diamond", L"Pentagon", L"Hexagon", L"5-point star", L"4-point star",
                L"6-point star", L"Heart", L"Lightning", L"Left arrow", L"Right arrow",
                L"Up arrow", L"Down arrow", L"Cloud callout", L"Oval callout",
                L"Rounded rectangle callout"};
            HMENU shapes = CreatePopupMenu();
            for (int i = 0; i < 19; ++i)
                AppendMenuW(shapes, MF_STRING, 600 + i, shapeLabels[i]);
            static const wchar_t* brushLabels[] = {
                L"Pencil", L"Brush", L"Airbrush", L"Calligraphy pen", L"Oil brush",
                L"Watercolor", L"Marker", L"Crayon", L"Natural pencil"};
            HMENU brushes = CreatePopupMenu();
            for (int i = 0; i < 9; ++i)
                AppendMenuW(brushes, MF_STRING, 700 + i, brushLabels[i]);
            HMENU colors = CreatePopupMenu();
            AppendMenuW(colors, MF_STRING, 501, L"&Edit Colors...");
            HMENU help = CreatePopupMenu();
            AppendMenuW(help, MF_STRING, 500, L"&About...");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(imageMenu), L"&Image");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(shapes), L"&Shapes");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(brushes), L"&Brushes");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(colors), L"&Colors");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
            SetMenu(hwnd, menu);
            NewDocument(800, 600);
            SetTitle();
            const std::wstring recovery = FindLatestRecovery();
            if (!recovery.empty()) {
                const int answer = MessageBoxW(
                    hwnd, L"An unsaved PaintRebuild image was recovered. Open it?",
                    L"Document Recovery", MB_YESNO | MB_ICONQUESTION);
                if (answer == IDYES) {
                    auto restored = LoadRecovery(recovery);
                    if (restored) {
                        g.image = std::move(restored);
                        g.stock.Clear();
                        RebuildTool();
                        InvalidateRect(hwnd, nullptr, TRUE);
                    }
                }
                ClearRecovery(recovery);
            }
            SetTimer(hwnd, 7001, 60000, nullptr);  // autorecovery heartbeat
            return 0;
        }
        case WM_TIMER:
            if (wParam == 7001 && g.stock.IsDirty())
                SaveRecoverySnapshot(*g.image, g.filePath);
            return 0;
        case WM_PAINT:
            Paint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
            if (HIWORD(wParam) == EN_KILLFOCUS && LOWORD(wParam) == 600) {
                FinishTextEditor(true);
                return 0;
            }
            DoCommand(LOWORD(wParam));
            return 0;
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (HandleStatusBarClick(x, y)) return 0;
            if (HandleCommandBarClick(x, y, false)) return 0;
            if (HandleLeftPanelClick(x, y)) return 0;
            const pr::Point p = ToImagePoint(x, y);
            if (p.x < 0 || p.y < 0 || p.x >= g.image->width() || p.y >= g.image->height())
                return 0;
            if (g.toolKind == PaintToolKind::Text) {
                FinishTextEditor(true);
                const RECT canvas = CanvasRect();
                const int px = canvas.left + p.x * g.zoom;
                const int py = canvas.top + p.y * g.zoom;
                const int canvasRight = static_cast<int>(canvas.right);
                const int canvasBottom = static_cast<int>(canvas.bottom);
                const int width = std::max(80, std::min(240 * g.zoom, canvasRight - px));
                const int height = std::max(24, std::min(28 * g.zoom, canvasBottom - py));
                g_textPos = p;
                g_textEdit = CreateWindowExW(
                    0, L"Edit", L"",
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | WS_BORDER | WS_TABSTOP,
                    px, py, width, height, hwnd, reinterpret_cast<HMENU>(600), nullptr, nullptr);
                g_textFont = CreateFontW(16 * g.zoom, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                         DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         ANTIALIASED_QUALITY, FF_DONTCARE, L"Segoe UI");
                SendMessageW(g_textEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_textFont), TRUE);
                SetFocus(g_textEdit);
                return 0;
            }
            if (g.tool) g.tool->OnStylusDown(p);
            g.drawing = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const pr::Point p = ToImagePoint(x, y);
            if (p.x != g.cursorX || p.y != g.cursorY) {
                g.cursorX = p.x;
                g.cursorY = p.y;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (g.drawing && g.tool) g.tool->OnStylusMove(p);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (g.drawing && g.tool) {
                const pr::Point p = ToImagePoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                g.tool->OnStylusUp(p);
                g.drawing = false;
                ReleaseCapture();
                SetTitle();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        case WM_RBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (g_commandBarVisible && y < kCommandBarHeight) {
                if (HandleCommandBarClick(x, y, true)) return 0;
            }
            const RECT canvas = CanvasRect();
            if (x >= canvas.left && x <= canvas.right && y >= canvas.top && y <= canvas.bottom) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 202, L"Cut");
                AppendMenuW(menu, MF_STRING, 203, L"Copy");
                AppendMenuW(menu, MF_STRING, 204, L"Paste");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, 200, L"Undo");
                AppendMenuW(menu, MF_STRING, 201, L"Redo");
                AppendMenuW(menu, MF_STRING, 206, L"Select All");
                AppendMenuW(menu, MF_STRING, 205, L"Clear Image");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, 405, L"Stretch / Skew...");
                AppendMenuW(menu, MF_STRING, 406, L"Flip / Rotate...");
                POINT screen{x, y};
                ClientToScreen(hwnd, &screen);
                TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, screen.x, screen.y,
                                 hwnd, nullptr);
                DestroyMenu(menu);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl) {
                switch (wParam) {
                    case 'Z': DoCommand(200); return 0;
                    case 'Y': DoCommand(201); return 0;
                    case 'X': DoCommand(202); return 0;
                    case 'C': DoCommand(203); return 0;
                    case 'V': DoCommand(204); return 0;
                    case 'A': DoCommand(206); return 0;
                    case 'N': DoCommand(100); return 0;
                    case 'O': DoCommand(101); return 0;
                    case 'S': DoCommand(103); return 0;
                    default: break;
                }
            } else if (wParam == VK_ESCAPE && IsFullScreen()) {
                ToggleFullScreen(hwnd);
            } else if (wParam == VK_ESCAPE && g.tool) {
                g.tool->OnCancel();
                g.drawing = false;
                InvalidateRect(hwnd, nullptr, TRUE);
            } else if (wParam == VK_DELETE) {
                DoCommand(205);
            } else if (g.tool) {
                g.tool->OnKeyDown(static_cast<unsigned>(wParam));
            }
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_CLOSE:
            FinishTextEditor(true);
            if (!g.stock.IsDirty()) ClearRecovery(FindLatestRecovery());
            KillTimer(hwnd, 7001);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    CoInitialize(nullptr);
    g.attributes.Add(1, 1);    // thickness
    g.attributes.Add(2, 0.0);  // tolerance (REA attribute IDs are GAP values)
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"PaintRebuildWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"PaintRebuildWindow", L"PaintRebuild",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        CoUninitialize();
        return 1;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
