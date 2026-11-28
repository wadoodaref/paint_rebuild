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
constexpr int kCommandBarHeight = 108;
constexpr int kStatusHeight = 32;
constexpr int kLeftPanelWidth = 52;
constexpr int kRulerSize = 20;

bool g_commandBarVisible = true;
bool g_rulerVisible = false;

bool HitRect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

struct GroupLayout {
    const wchar_t* name = nullptr;
    int left = 0;
    int right = 0;
};

struct CommandButton {
    RECT rect{};
    int command = 0;
    const wchar_t* label = nullptr;
    const char* icon = nullptr;  // assets/icons/<icon>
    bool toggle = false;
    bool dropdown = false;
    bool enabled = true;
};

struct CommandLayout {
    std::vector<GroupLayout> groups;
    std::vector<CommandButton> buttons;
    RECT color1{};
    RECT color2{};
    RECT editColors{};
    std::vector<RECT> palette;
    RECT shapeGrid{};
    std::vector<CommandButton> shapeButtons;
};

int CommandBarTop() { return 0; }  // below Win32 menu bar, in client coords

int g_activeColorSlot = 1;  // 1 = Color 1 (foreground), 2 = Color 2 (background)
int g_hoverCmd = 0;
int g_hoverShapeIdx = -1;
int g_hoverPaletteIdx = -1;
bool g_hoverColor1 = false;
bool g_hoverColor2 = false;
bool g_hoverEditColors = false;
int g_hoverStatusPart = 0;  // 1=fit, 2=zoom-, 3=zoom+
int g_dragControl = 0;      // 1=size slider, 2=opacity slider, 3=zoom slider
int g_scrollX = 0;
int g_scrollY = 0;
int g_opacityPercent = 100;

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
    int resizeEdge = 0; // 0=none, 1=right, 2=bottom, 3=corner
    std::wstring filePath;
    HWND hwnd = nullptr;
};

AppState g;
ShapeKind g_shapeKind = ShapeKind::Rectangle;
bool g_shapeFilled = false;
int g_resizeOrigW = 0, g_resizeOrigH = 0, g_resizeStartX = 0, g_resizeStartY = 0;

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
        g.tool->SetOpacity(g_opacityPercent / 100.0);
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

RECT WorkspaceRect() {
    RECT client;
    GetClientRect(g.hwnd, &client);
    return RECT{kLeftPanelWidth, CommandBarHeight(), client.right,
                std::max<LONG>(CommandBarHeight(), client.bottom - kStatusHeight)};
}

RECT ViewportRect() {
    const RECT ws = WorkspaceRect();
    const int margin = g_rulerVisible ? kRulerSize : 0;
    return RECT{ws.left + margin + 12, ws.top + margin + 12,
                std::max<LONG>(ws.left + margin + 13, ws.right - 12),
                std::max<LONG>(ws.top + margin + 13, ws.bottom - 12)};
}

void ClampScroll() {
    if (!g.hwnd || !g.image) return;
    const RECT vp = ViewportRect();
    const int vw = vp.right - vp.left;
    const int vh = vp.bottom - vp.top;
    const int dw = g.image->width() * g.zoom;
    const int dh = g.image->height() * g.zoom;
    const int maxX = std::max(0, dw - vw);
    const int maxY = std::max(0, dh - vh);
    g_scrollX = std::max(0, std::min(maxX, g_scrollX));
    g_scrollY = std::max(0, std::min(maxY, g_scrollY));
}

RECT CanvasRect() {
    const RECT vp = ViewportRect();
    const int vw = vp.right - vp.left;
    const int vh = vp.bottom - vp.top;
    const int dw = g.image ? g.image->width() * g.zoom : 0;
    const int dh = g.image ? g.image->height() * g.zoom : 0;
    ClampScroll();
    const int left = (dw <= vw) ? (vp.left + (vw - dw) / 2) : (vp.left - g_scrollX);
    const int top = (dh <= vh) ? (vp.top + (vh - dh) / 2) : (vp.top - g_scrollY);
    return RECT{left, top, left + dw, top + dh};
}

pr::Point ToImagePoint(int x, int y) {
    const RECT canvas = CanvasRect();
    return pr::Point{(x - canvas.left) / g.zoom, (y - canvas.top) / g.zoom};
}

void FillRectValue(HDC hdc, RECT rect, HBRUSH brush) { FillRect(hdc, &rect, brush); }

enum class ResizeEdge { None, Right, Bottom, Corner };
ResizeEdge CanvasResizeHit(int x, int y, const RECT& canvas) {
    const int w = g.image->width() * g.zoom, h = g.image->height() * g.zoom;
    const int rx = canvas.left + w, by = canvas.top + h;
    auto hit = [&](int cx, int cy) { return x >= cx - 4 && x <= cx + 4 && y >= cy - 4 && y <= cy + 4; };
    if (hit(rx, by)) return ResizeEdge::Corner;
    if (hit(rx, canvas.top + h / 2)) return ResizeEdge::Right;
    if (hit(canvas.left + w / 2, by)) return ResizeEdge::Bottom;
    return ResizeEdge::None;
}
void DrawCanvasResizeHandles(HDC hdc, const RECT& canvas) {
    const int w = g.image->width() * g.zoom, h = g.image->height() * g.zoom;
    const POINT pts[] = {{canvas.left + w, canvas.top + h / 2}, {canvas.left + w / 2, canvas.top + h}, {canvas.left + w, canvas.top + h}};
    for (auto p : pts) {
        RECT r{p.x - 3, p.y - 3, p.x + 4, p.y + 4};
        FillRect(hdc, &r, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        FrameRect(hdc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
}

void DrawCanvas(HDC hdc, const RECT& canvas) {
    const int w = g.image->width();
    const int h = g.image->height();
    // Checkerboard behind transparent pixels
    const int dw = w * g.zoom, dh = h * g.zoom;
    const int tile = 8;
    for (int ty = 0; ty < dh; ty += tile) {
        for (int tx = 0; tx < dw; tx += tile) {
            const bool light = ((tx / tile) + (ty / tile)) % 2 == 0;
            HBRUSH cb = CreateSolidBrush(light ? RGB(255,255,255) : RGB(204,204,204));
            RECT tr{canvas.left + tx, canvas.top + ty, canvas.left + std::min(tx + tile, dw), canvas.top + std::min(ty + tile, dh)};
            FillRectValue(hdc, tr, cb); DeleteObject(cb);
        }
    }
    // Alpha-blend image (premultiply temp)
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> prem(static_cast<size_t>(w) * h * 4);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(g.image->Bitmap());
    for (size_t i = 0, n = prem.size() / 4; i < n; ++i) {
        const uint8_t a = src[i*4+3];
        prem[i*4+0] = static_cast<uint8_t>((src[i*4+0] * a + 127) / 255);
        prem[i*4+1] = static_cast<uint8_t>((src[i*4+1] * a + 127) / 255);
        prem[i*4+2] = static_cast<uint8_t>((src[i*4+2] * a + 127) / 255);
        prem[i*4+3] = a;
    }
    HDC mem = CreateCompatibleDC(hdc);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        memcpy(bits, prem.data(), prem.size());
        HGDIOBJ old = SelectObject(mem, dib);
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(hdc, canvas.left, canvas.top, dw, dh, mem, 0, 0, w, h, bf);
        SelectObject(mem, old);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);

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

static void DrawVSeparator(HDC hdc, int x, int top, int bottom) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(218, 218, 218));
    HGDIOBJ old = SelectObject(hdc, pen);
    MoveToEx(hdc, x, top + 8, nullptr);
    LineTo(hdc, x, bottom - 8);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static void DrawCmdBtn(HDC hdc, const RECT& r, const char* icon, const wchar_t* label,
                       bool selected, bool hovered, bool iconOnly, bool dropdown = false,
                       bool enabled = true) {
    COLORREF bg = selected ? RGB(204, 228, 247)
                           : (hovered && enabled ? RGB(232, 232, 232) : RGB(243, 243, 243));
    COLORREF border = selected ? RGB(0, 120, 215)
                               : (hovered && enabled ? RGB(212, 212, 212) : bg);
    HBRUSH br = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, 6, 6);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);

    const int bw = r.right - r.left;
    const int bh = r.bottom - r.top;
    const bool compactHoriz = (!iconOnly && bh <= 40);

    if (icon) {
        if (Image* img = GetIcon(icon)) {
            if (compactHoriz) {
                const int isz = 16;
                DrawToolIcon(hdc, *img, r.left + 6, r.top + (bh - isz) / 2, isz);
            } else {
                const int isz = iconOnly ? 20 : 24;
                const int ix = r.left + (bw - isz) / 2;
                const int iy = r.top + (iconOnly ? (bh - isz) / 2 : 10);
                DrawToolIcon(hdc, *img, ix, iy, isz);
            }
        }
    }
    if (label && !iconOnly) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, enabled ? RGB(32, 32, 32) : RGB(150, 150, 150));
        if (compactHoriz) {
            RECT tr{r.left + 20, r.top, r.right - (dropdown ? 7 : 4), r.bottom};
            DrawTextW(hdc, label, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        } else {
            RECT tr{r.left + 2, r.top + 38, r.right - 2, r.bottom - 4};
            DrawTextW(hdc, label, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }
    if (dropdown) {
        const int cx = compactHoriz ? (r.right - 8) : (r.right - 8);
        const int cy = compactHoriz ? (r.top + bh / 2) : (r.top + 52);
        POINT pts[3] = {{cx - 3, cy - 1}, {cx + 3, cy - 1}, {cx, cy + 2}};
        HBRUSH ar = CreateSolidBrush(enabled ? RGB(80, 80, 80) : RGB(160, 160, 160));
        HPEN ap = CreatePen(PS_SOLID, 1, enabled ? RGB(80, 80, 80) : RGB(160, 160, 160));
        HGDIOBJ oa = SelectObject(hdc, ar);
        HGDIOBJ op = SelectObject(hdc, ap);
        Polygon(hdc, pts, 3);
        SelectObject(hdc, op);
        SelectObject(hdc, oa);
        DeleteObject(ap);
        DeleteObject(ar);
    }
}

CommandLayout BuildCommandLayout() {
    CommandLayout L;
    const int top = CommandBarTop() + 4;
    int x = 8;

    auto addBtn = [&](int bx, int by, int bw, int bh, int cmd, const wchar_t* label,
                      const char* icon, bool sel = false, bool dropdown = false,
                      bool enabled = true) {
        CommandButton b;
        b.rect = RECT{bx, by, bx + bw, by + bh};
        b.command = cmd;
        b.label = label;
        b.icon = icon;
        b.toggle = sel;
        b.dropdown = dropdown;
        b.enabled = enabled;
        L.buttons.push_back(b);
    };

    // Selection group
    const int selLeft = x;
    const bool selActive = (g.toolKind == PaintToolKind::Selection ||
                            g.toolKind == PaintToolKind::FreeFormSelection);
    const char* selIcon =
        (g.toolKind == PaintToolKind::FreeFormSelection) ? "freeform.png" : "select.png";
    addBtn(x + 6, top + 4, 56, 72, 9004, L"Select", selIcon, selActive, true);
    x += 68;
    L.groups.push_back({L"Selection", selLeft, x});
    x += 6;

    // Image group
    const int imgLeft = x;
    const Rect selRect = g.tool ? g.tool->SelectionRect() : Rect{};
    const bool hasSel = (selRect.w > 0 && selRect.h > 0);
    addBtn(x + 4, top + 4, 48, 72, 407, L"Crop", "select.png", false, false, hasSel);
    addBtn(x + 56, top + 4, 72, 34, 9006, L"Rotate", "rotate.png", false, true);
    addBtn(x + 56, top + 42, 72, 34, 9005, L"Flip", "flip.png", false, true);
    addBtn(x + 134, top + 4, 52, 72, 405, L"Resize", "resize.png", false, false);
    x += 192;
    L.groups.push_back({L"Image", imgLeft, x});
    x += 6;

    // Tools group (2 rows x 3)
    const int toolsLeft = x;
    const int tx[] = {x + 4, x + 42, x + 80};
    addBtn(tx[0], top + 4, 34, 34, 806, nullptr, "pencil.png", g.toolKind == PaintToolKind::Pencil);
    addBtn(tx[1], top + 4, 34, 34, 803, nullptr, "fill.png", g.toolKind == PaintToolKind::FloodFill);
    addBtn(tx[2], top + 4, 34, 34, 810, nullptr, "text.png", g.toolKind == PaintToolKind::Text);
    addBtn(tx[0], top + 42, 34, 34, 802, nullptr, "eraser.png", g.toolKind == PaintToolKind::Eraser);
    addBtn(tx[1], top + 42, 34, 34, 804, nullptr, "picker.png", g.toolKind == PaintToolKind::PickColor);
    addBtn(tx[2], top + 42, 34, 34, 805, nullptr, "zoom.png", g.toolKind == PaintToolKind::Zoom);
    x += 118;
    L.groups.push_back({L"Tools", toolsLeft, x});
    x += 6;

    // Brushes group
    const int brushLeft = x;
    const bool brushActive = (g.toolKind == PaintToolKind::Brush ||
                              g.toolKind == PaintToolKind::Airbrush ||
                              g.toolKind == PaintToolKind::Calligraphic ||
                              g.toolKind == PaintToolKind::OilBrush ||
                              g.toolKind == PaintToolKind::Watercolor ||
                              g.toolKind == PaintToolKind::Marker ||
                              g.toolKind == PaintToolKind::Crayon ||
                              g.toolKind == PaintToolKind::NaturalPencil);
    addBtn(x + 6, top + 4, 76, 72, 807, L"Brushes", "brush.png", brushActive, true);
    x += 84;
    L.groups.push_back({L"Brushes", brushLeft, x});
    x += 6;

    // Shapes group (8 cols x 3 rows = 24 slots for 23 shapes; fits cleanly above label)
    const int shapesLeft = x;
    L.shapeGrid = RECT{x + 4, top + 4, x + 188, top + 79};
    static const struct { const char* icon; ShapeKind shape; int cmd; } shapeDefs[] = {
        {"line.png", ShapeKind::Rectangle, 811}, {"curve.png", ShapeKind::Rectangle, 812},
        {"oval.png", ShapeKind::Ellipse, 815}, {"rectangle.png", ShapeKind::Rectangle, 814},
        {"rounded_rect.png", ShapeKind::RoundedRectangle, 816}, {"polygon.png", ShapeKind::Rectangle, 813},
        {"triangle.png", ShapeKind::Triangle, 600 + (int)ShapeKind::Triangle},
        {"right_triangle.png", ShapeKind::RightTriangle, 600 + (int)ShapeKind::RightTriangle},
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
        const int col = i % 8;
        const int row = i / 8;
        CommandButton sb;
        sb.rect = RECT{L.shapeGrid.left + col * 23, L.shapeGrid.top + row * 25,
                       L.shapeGrid.left + col * 23 + 21, L.shapeGrid.top + row * 25 + 23};
        sb.command = shapeDefs[i].cmd;
        sb.icon = shapeDefs[i].icon;
        if (sb.command == 811) sb.toggle = (g.toolKind == PaintToolKind::Line);
        else if (sb.command == 812) sb.toggle = (g.toolKind == PaintToolKind::Curve);
        else if (sb.command == 813) sb.toggle = (g.toolKind == PaintToolKind::Polygon);
        else if (sb.command == 814) sb.toggle = (g.toolKind == PaintToolKind::Rectangle);
        else if (sb.command == 815) sb.toggle = (g.toolKind == PaintToolKind::Ellipse);
        else if (sb.command == 816) sb.toggle = (g.toolKind == PaintToolKind::RoundedRectangle);
        else sb.toggle = (g.toolKind == PaintToolKind::AutoShape && g_shapeKind == shapeDefs[i].shape);
        L.shapeButtons.push_back(sb);
    }
    x += 192;
    int outlineThickness = 1;
    g.attributes.GetValue(1, outlineThickness);
    addBtn(x + 2, top + 4, 72, 34, 9001, L"Outline", "shape_outline.png", outlineThickness > 0, true);
    addBtn(x + 2, top + 42, 72, 34, 9002, L"Fill", "shape_fill.png", g_shapeFilled, true);
    x += 78;
    L.groups.push_back({L"Shapes", shapesLeft, x});
    x += 6;

    // Size group
    const int sizeLeft = x;
    addBtn(x + 4, top + 4, 48, 72, 9003, L"Size", "size.png", false, true);
    x += 56;
    L.groups.push_back({L"Size", sizeLeft, x});
    x += 6;

    // Colors group
    const int colorsLeft = x;
    L.color1 = RECT{x + 4, top + 4, x + 50, top + 76};
    L.color2 = RECT{x + 54, top + 4, x + 100, top + 76};
    for (int i = 0; i < 20; ++i) {
        const int col = i % 10;
        const int row = i / 10;
        L.palette.push_back(RECT{x + 108 + col * 22, top + 12 + row * 28,
                                 x + 108 + col * 22 + 20, top + 12 + row * 28 + 20});
    }
    L.editColors = RECT{x + 326, top + 4, x + 380, top + 76};
    x += 386;
    L.groups.push_back({L"Colors", colorsLeft, x});

    return L;
}

void DrawCommandBar(HDC hdc) {
    if (!g_commandBarVisible) return;
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = CommandBarTop();
    const int h = kCommandBarHeight;
    static HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                    0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    HBRUSH bg = CreateSolidBrush(RGB(243, 243, 243));
    FillRectValue(hdc, RECT{0, top, client.right, top + h}, bg);
    DeleteObject(bg);
    HPEN line = CreatePen(PS_SOLID, 1, RGB(218, 218, 218));
    HGDIOBJ oldPen = SelectObject(hdc, line);
    MoveToEx(hdc, 0, top + h - 1, nullptr);
    LineTo(hdc, client.right, top + h - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(line);

    CommandLayout L = BuildCommandLayout();
    SetBkMode(hdc, TRANSPARENT);

    for (size_t i = 0; i < L.groups.size(); ++i) {
        const auto& grp = L.groups[i];
        if (i + 1 < L.groups.size())
            DrawVSeparator(hdc, grp.right + 2, top, top + h);
        RECT lr{grp.left, top + h - 22, grp.right, top + h - 4};
        SetTextColor(hdc, RGB(96, 96, 96));
        DrawTextW(hdc, grp.name, -1, &lr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    for (const auto& b : L.buttons) {
        const bool iconOnly = (b.label == nullptr);
        const bool hovered = (g_hoverCmd == b.command && b.command != 0);
        DrawCmdBtn(hdc, b.rect, b.icon, b.label, b.toggle, hovered, iconOnly, b.dropdown, b.enabled);
    }

    for (int i = 0; i < static_cast<int>(L.shapeButtons.size()); ++i) {
        const auto& sb = L.shapeButtons[i];
        const bool sel = sb.toggle;
        const bool hov = (g_hoverShapeIdx == i);
        COLORREF sbg = sel ? RGB(204, 228, 247) : (hov ? RGB(232, 232, 232) : RGB(243, 243, 243));
        COLORREF sbd = sel ? RGB(0, 120, 215) : (hov ? RGB(212, 212, 212) : sbg);
        HBRUSH br = CreateSolidBrush(sbg);
        HPEN p = CreatePen(PS_SOLID, 1, sbd);
        HGDIOBJ op = SelectObject(hdc, p);
        HGDIOBJ ob = SelectObject(hdc, br);
        RoundRect(hdc, sb.rect.left, sb.rect.top, sb.rect.right, sb.rect.bottom, 4, 4);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(p);
        DeleteObject(br);
        if (Image* img = GetIcon(sb.icon))
            DrawToolIcon(hdc, *img, sb.rect.left + 3, sb.rect.top + 2, 18);
    }

    // Color 1 / Color 2 selectable swatch buttons (Paint 11 style)
    auto drawActiveColorBtn = [&](const RECT& r, Color c, bool active, bool hovered, const wchar_t* lbl) {
        COLORREF cbg = active ? RGB(204, 228, 247) : (hovered ? RGB(232, 232, 232) : RGB(243, 243, 243));
        COLORREF cbd = active ? RGB(0, 120, 215) : (hovered ? RGB(212, 212, 212) : cbg);
        HBRUSH br = CreateSolidBrush(cbg);
        HPEN pen = CreatePen(PS_SOLID, 1, cbd);
        HGDIOBJ op = SelectObject(hdc, pen);
        HGDIOBJ ob = SelectObject(hdc, br);
        RoundRect(hdc, r.left, r.top, r.right, r.bottom, 6, 6);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(pen);
        DeleteObject(br);

        const int cx = (r.left + r.right) / 2;
        const int cy = r.top + 24;
        HBRUSH cb = CreateSolidBrush(RGB(Red(c), Green(c), Blue(c)));
        HPEN cp = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
        op = SelectObject(hdc, cp);
        ob = SelectObject(hdc, cb);
        Ellipse(hdc, cx - 13, cy - 13, cx + 13, cy + 13);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(cp);
        DeleteObject(cb);

        RECT lr{r.left, r.top + 44, r.right, r.bottom - 4};
        SetTextColor(hdc, RGB(32, 32, 32));
        DrawTextW(hdc, lbl, -1, &lr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    };
    drawActiveColorBtn(L.color1, g.foreground, g_activeColorSlot == 1, g_hoverColor1, L"Color 1");
    drawActiveColorBtn(L.color2, g.background, g_activeColorSlot == 2, g_hoverColor2, L"Color 2");

    const Color activeCol = (g_activeColorSlot == 1) ? g.foreground : g.background;
    for (int i = 0; i < 20 && i < static_cast<int>(L.palette.size()); ++i) {
        RECT r = L.palette[i];
        Color c = palette::Paint11Colors[i];
        const bool match = (Red(c) == Red(activeCol) && Green(c) == Green(activeCol) &&
                            Blue(c) == Blue(activeCol));
        const bool hov = (g_hoverPaletteIdx == i);
        if (match || hov) {
            HPEN ring = CreatePen(PS_SOLID, match ? 2 : 1, match ? RGB(0, 120, 215) : RGB(150, 150, 150));
            HGDIOBJ op = SelectObject(hdc, ring);
            HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Ellipse(hdc, r.left - 2, r.top - 2, r.right + 2, r.bottom + 2);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(ring);
        }
        HBRUSH cb = CreateSolidBrush(RGB(Red(c), Green(c), Blue(c)));
        HGDIOBJ oldB = SelectObject(hdc, cb);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
        HGDIOBJ oldP = SelectObject(hdc, pen);
        Ellipse(hdc, r.left, r.top, r.right, r.bottom);
        SelectObject(hdc, oldP);
        SelectObject(hdc, oldB);
        DeleteObject(pen);
        DeleteObject(cb);
    }
    DrawCmdBtn(hdc, L.editColors, "colors.png", L"Edit", false, g_hoverEditColors, false);
    SelectObject(hdc, oldFont);
}

struct LeftSliderGeom {
    int sx = 0;
    int sy1 = 0, sy2 = 0;
    int oy1 = 0, oy2 = 0;
    int sliderH = 0;
};

LeftSliderGeom GetLeftSliderGeom() {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = CommandBarHeight();
    const int bottom = std::max<LONG>(top + 120, client.bottom - kStatusHeight);
    const int avail = bottom - top;
    const int sliderH = std::max(50, std::min(150, (avail - 116) / 2));
    LeftSliderGeom G;
    G.sx = kLeftPanelWidth / 2;
    G.sliderH = sliderH;
    G.sy1 = top + 28;
    G.sy2 = G.sy1 + sliderH;
    G.oy1 = G.sy2 + 52;
    G.oy2 = G.oy1 + sliderH;
    return G;
}

void DrawVerticalSlider(HDC hdc, int cx, int y1, int y2, double frac) {
    frac = std::max(0.0, std::min(1.0, frac));
    const int h = y2 - y1;
    const int thumbY = y2 - static_cast<int>(frac * h);
    HBRUSH track = CreateSolidBrush(RGB(205, 205, 205));
    HPEN tp = CreatePen(PS_SOLID, 1, RGB(205, 205, 205));
    HGDIOBJ op = SelectObject(hdc, tp);
    HGDIOBJ ob = SelectObject(hdc, track);
    RoundRect(hdc, cx - 2, y1, cx + 2, y2, 4, 4);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(tp);
    DeleteObject(track);

    if (thumbY < y2) {
        HBRUSH fill = CreateSolidBrush(RGB(0, 120, 215));
        HPEN fp = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
        op = SelectObject(hdc, fp);
        ob = SelectObject(hdc, fill);
        RoundRect(hdc, cx - 2, thumbY, cx + 2, y2, 4, 4);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(fp);
        DeleteObject(fill);
    }

    HBRUSH outer = CreateSolidBrush(RGB(255, 255, 255));
    HPEN op2 = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    op = SelectObject(hdc, op2);
    ob = SelectObject(hdc, outer);
    Ellipse(hdc, cx - 7, thumbY - 7, cx + 7, thumbY + 7);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(op2);
    DeleteObject(outer);

    HBRUSH inner = CreateSolidBrush(RGB(0, 120, 215));
    HPEN ip = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
    op = SelectObject(hdc, ip);
    ob = SelectObject(hdc, inner);
    Ellipse(hdc, cx - 4, thumbY - 4, cx + 4, thumbY + 4);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(ip);
    DeleteObject(inner);
}

void DrawLeftPanel(HDC hdc) {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = CommandBarHeight();
    const int bottom = client.bottom - kStatusHeight;
    static HFONT font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                    0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    HBRUSH bg = CreateSolidBrush(RGB(243, 243, 243));
    FillRectValue(hdc, RECT{0, top, kLeftPanelWidth, bottom}, bg);
    DeleteObject(bg);
    HPEN line = CreatePen(PS_SOLID, 1, RGB(218, 218, 218));
    HGDIOBJ op = SelectObject(hdc, line);
    MoveToEx(hdc, kLeftPanelWidth - 1, top, nullptr);
    LineTo(hdc, kLeftPanelWidth - 1, bottom);
    SelectObject(hdc, op);
    DeleteObject(line);

    const LeftSliderGeom G = GetLeftSliderGeom();
    int thickness = 1;
    g.attributes.GetValue(1, thickness);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(80, 80, 80));

    RECT sl{2, G.sy1 - 20, kLeftPanelWidth - 2, G.sy1 - 4};
    DrawTextW(hdc, L"Size", -1, &sl, DT_CENTER | DT_SINGLELINE);
    DrawVerticalSlider(hdc, G.sx, G.sy1, G.sy2, (thickness - 1) / 39.0);
    std::wstring sizeVal = std::to_wstring(thickness) + L"px";
    RECT sv{2, G.sy2 + 4, kLeftPanelWidth - 2, G.sy2 + 20};
    DrawTextW(hdc, sizeVal.c_str(), -1, &sv, DT_CENTER | DT_SINGLELINE);

    RECT ol{2, G.oy1 - 20, kLeftPanelWidth - 2, G.oy1 - 4};
    DrawTextW(hdc, L"Opacity", -1, &ol, DT_CENTER | DT_SINGLELINE);
    DrawVerticalSlider(hdc, G.sx, G.oy1, G.oy2, g_opacityPercent / 100.0);
    std::wstring opVal = std::to_wstring(g_opacityPercent) + L"%";
    RECT ov{2, G.oy2 + 4, kLeftPanelWidth - 2, G.oy2 + 20};
    DrawTextW(hdc, opVal.c_str(), -1, &ov, DT_CENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
}

struct StatusGeom {
    RECT fit{};
    RECT pct{};
    RECT zoomOut{};
    RECT slider{};
    RECT zoomIn{};
};

StatusGeom GetStatusGeom(const RECT& client) {
    const int zy = client.bottom - kStatusHeight + 4;
    StatusGeom S;
    S.fit = RECT{client.right - 310, zy, client.right - 282, zy + 24};
    S.pct = RECT{client.right - 278, zy, client.right - 230, zy + 24};
    S.zoomOut = RECT{client.right - 226, zy, client.right - 198, zy + 24};
    S.slider = RECT{client.right - 192, zy, client.right - 48, zy + 24};
    S.zoomIn = RECT{client.right - 42, zy, client.right - 14, zy + 24};
    return S;
}

void DrawStatus(HDC hdc) {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = client.bottom - kStatusHeight;
    static HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                    0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    HBRUSH bg = CreateSolidBrush(RGB(243, 243, 243));
    FillRectValue(hdc, RECT{0, top, client.right, client.bottom}, bg);
    DeleteObject(bg);
    HPEN line = CreatePen(PS_SOLID, 1, RGB(218, 218, 218));
    HGDIOBJ op = SelectObject(hdc, line);
    MoveToEx(hdc, 0, top, nullptr);
    LineTo(hdc, client.right, top);
    SelectObject(hdc, op);
    DeleteObject(line);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(60, 60, 60));

    // Left: cursor pos (when inside image), selection size (when active), canvas size
    if (g.cursorX >= 0 && g.cursorY >= 0 && g.image &&
        g.cursorX < g.image->width() && g.cursorY < g.image->height()) {
        std::wstring pos = std::to_wstring(g.cursorX) + L", " +
                           std::to_wstring(g.cursorY) + L"px";
        TextOutW(hdc, 14, top + 8, pos.c_str(), static_cast<int>(pos.size()));
    }
    const Rect selRect = g.tool ? g.tool->SelectionRect() : Rect{};
    if (selRect.w > 0 && selRect.h > 0) {
        std::wstring selStr = std::to_wstring(selRect.w) + L" \u00D7 " +
                              std::to_wstring(selRect.h) + L"px";
        TextOutW(hdc, 124, top + 8, selStr.c_str(), static_cast<int>(selStr.size()));
    }
    if (g.image) {
        std::wstring size = std::to_wstring(g.image->width()) + L" \u00D7 " +
                            std::to_wstring(g.image->height()) + L"px";
        TextOutW(hdc, 240, top + 8, size.c_str(), static_cast<int>(size.size()));
    }

    const StatusGeom S = GetStatusGeom(client);
    auto drawSBtn = [&](const RECT& r, const char* icon, const wchar_t* fallback, bool hov) {
        if (hov) {
            HBRUSH hb = CreateSolidBrush(RGB(228, 228, 228));
            HPEN hp = CreatePen(PS_SOLID, 1, RGB(205, 205, 205));
            HGDIOBJ ohp = SelectObject(hdc, hp);
            HGDIOBJ ohb = SelectObject(hdc, hb);
            RoundRect(hdc, r.left, r.top, r.right, r.bottom, 4, 4);
            SelectObject(hdc, ohb);
            SelectObject(hdc, ohp);
            DeleteObject(hp);
            DeleteObject(hb);
        }
        if (Image* img = icon ? GetIcon(icon) : nullptr) {
            DrawToolIcon(hdc, *img, r.left + 6, r.top + 4, 16);
        } else if (fallback) {
            DrawTextW(hdc, fallback, -1, const_cast<RECT*>(&r), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    };

    drawSBtn(S.fit, "fit_window.png", L"\u229F", g_hoverStatusPart == 1);
    std::wstring zstr = std::to_wstring(g.zoom * 100) + L"%";
    DrawTextW(hdc, zstr.c_str(), -1, const_cast<RECT*>(&S.pct), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawSBtn(S.zoomOut, "zoom_out.png", L"\u2212", g_hoverStatusPart == 2);

    // Zoom slider
    const int sl = S.slider.left + 8;
    const int sr = S.slider.right - 8;
    const int cy = (S.slider.top + S.slider.bottom) / 2;
    const int zx = sl + (g.zoom - 1) * (sr - sl) / 15;
    HBRUSH trk = CreateSolidBrush(RGB(200, 200, 200));
    FillRectValue(hdc, RECT{sl, cy - 2, sr, cy + 2}, trk);
    DeleteObject(trk);
    if (zx > sl) {
        HBRUSH fl = CreateSolidBrush(RGB(0, 120, 215));
        FillRectValue(hdc, RECT{sl, cy - 2, zx, cy + 2}, fl);
        DeleteObject(fl);
    }
    HBRUSH tho = CreateSolidBrush(RGB(255, 255, 255));
    HPEN thop = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    HGDIOBJ op1 = SelectObject(hdc, thop);
    HGDIOBJ ob1 = SelectObject(hdc, tho);
    Ellipse(hdc, zx - 7, cy - 7, zx + 7, cy + 7);
    SelectObject(hdc, ob1);
    SelectObject(hdc, op1);
    DeleteObject(thop);
    DeleteObject(tho);
    HBRUSH thi = CreateSolidBrush(RGB(0, 120, 215));
    HPEN thip = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
    op1 = SelectObject(hdc, thip);
    ob1 = SelectObject(hdc, thi);
    Ellipse(hdc, zx - 4, cy - 4, zx + 4, cy + 4);
    SelectObject(hdc, ob1);
    SelectObject(hdc, op1);
    DeleteObject(thip);
    DeleteObject(thi);

    drawSBtn(S.zoomIn, "zoom_in.png", L"+", g_hoverStatusPart == 3);
    SelectObject(hdc, oldFont);
}

void DrawRuler(HDC hdc, const RECT& canvas) {
    if (!g_rulerVisible) return;
    const RECT ws = WorkspaceRect();
    static HFONT rfont = CreateFontW(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                     0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, rfont);
    HBRUSH face = CreateSolidBrush(RGB(246, 246, 246));
    FillRectValue(hdc, RECT{ws.left, ws.top, ws.right, ws.top + kRulerSize}, face);
    FillRectValue(hdc, RECT{ws.left, ws.top, ws.left + kRulerSize, ws.bottom}, face);
    DeleteObject(face);

    HPEN border = CreatePen(PS_SOLID, 1, RGB(212, 212, 212));
    HGDIOBJ old = SelectObject(hdc, border);
    MoveToEx(hdc, ws.left, ws.top + kRulerSize - 1, nullptr);
    LineTo(hdc, ws.right, ws.top + kRulerSize - 1);
    MoveToEx(hdc, ws.left + kRulerSize - 1, ws.top, nullptr);
    LineTo(hdc, ws.left + kRulerSize - 1, ws.bottom);
    SelectObject(hdc, old);
    DeleteObject(border);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
    old = SelectObject(hdc, pen);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(90, 90, 90));
    const int step = (g.zoom >= 4) ? 10 : 20;
    for (int px = 0; px <= g.image->width(); px += step) {
        const int x = canvas.left + px * g.zoom;
        if (x < ws.left + kRulerSize) continue;
        if (x > ws.right) break;
        const int length = (px % 100 == 0) ? 10 : 5;
        MoveToEx(hdc, x, ws.top + kRulerSize - 1, nullptr);
        LineTo(hdc, x, ws.top + kRulerSize - 1 - length);
        if (px % 100 == 0) {
            const std::wstring label = std::to_wstring(px);
            TextOutW(hdc, x + 2, ws.top + 1, label.c_str(), static_cast<int>(label.size()));
        }
    }
    for (int py = 0; py <= g.image->height(); py += step) {
        const int y = canvas.top + py * g.zoom;
        if (y < ws.top + kRulerSize) continue;
        if (y > ws.bottom) break;
        const int length = (py % 100 == 0) ? 10 : 5;
        MoveToEx(hdc, ws.left + kRulerSize - 1, y, nullptr);
        LineTo(hdc, ws.left + kRulerSize - 1 - length, y);
        if (py % 100 == 0) {
            const std::wstring label = std::to_wstring(py);
            TextOutW(hdc, ws.left + 1, y + 2, label.c_str(), static_cast<int>(label.size()));
        }
    }
    SelectObject(hdc, old);
    DeleteObject(pen);
    SelectObject(hdc, oldFont);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ old = SelectObject(mem, bitmap);
    HBRUSH wsBrush = CreateSolidBrush(RGB(238, 238, 238));
    FillRect(mem, &client, wsBrush);
    DeleteObject(wsBrush);

    const RECT ws = WorkspaceRect();
    const RECT vp = ViewportRect();
    const int clipLeft = g_rulerVisible ? (ws.left + kRulerSize) : ws.left;
    const int clipTop = g_rulerVisible ? (ws.top + kRulerSize) : ws.top;
    (void)vp;
    const int saved = SaveDC(mem);
    IntersectClipRect(mem, clipLeft, clipTop, ws.right, ws.bottom);

    const RECT canvas = CanvasRect();
    {
        const int cw = g.image->width() * g.zoom, ch = g.image->height() * g.zoom;
        RECT shadow{canvas.left + 3, canvas.top + 3, canvas.left + cw + 3, canvas.top + ch + 3};
        HBRUSH sh = CreateSolidBrush(RGB(210, 210, 210));
        FillRect(mem, &shadow, sh);
        DeleteObject(sh);
    }
    DrawCanvas(mem, canvas);
    DrawCanvasResizeHandles(mem, canvas);
    RestoreDC(mem, saved);

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
    for (const auto& b : L.buttons) {
        if (HitRect(b.rect, x, y)) {
            if (b.enabled) DoCommand(static_cast<WORD>(b.command));
            return true;
        }
    }
    for (const auto& sb : L.shapeButtons) {
        if (HitRect(sb.rect, x, y)) {
            DoCommand(static_cast<WORD>(sb.command));
            return true;
        }
    }
    if (HitRect(L.color1, x, y)) {
        g_activeColorSlot = 1;
        InvalidateRect(g.hwnd, nullptr, FALSE);
        return true;
    }
    if (HitRect(L.color2, x, y)) {
        g_activeColorSlot = 2;
        InvalidateRect(g.hwnd, nullptr, FALSE);
        return true;
    }
    for (int i = 0; i < static_cast<int>(L.palette.size()) && i < 20; ++i) {
        if (HitRect(L.palette[i], x, y)) {
            Color c = palette::Paint11Colors[i];
            if (rightButton || g_activeColorSlot == 2) g.background = c;
            else g.foreground = c;
            RebuildTool();
            InvalidateRect(g.hwnd, nullptr, FALSE);
            return true;
        }
    }
    if (HitRect(L.editColors, x, y)) {
        DoCommand(501);
        return true;
    }
    return true;
}

void ApplySizeSliderY(int y) {
    const LeftSliderGeom G = GetLeftSliderGeom();
    const double frac = 1.0 - static_cast<double>(y - G.sy1) / std::max(1, G.sliderH);
    const int t = std::max(1, std::min(40, 1 + static_cast<int>(frac * 39.0 + 0.5)));
    g.attributes.SetValue(1, t);
    if (g.tool) g.tool->SetThickness(static_cast<unsigned>(t));
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void ApplyOpacitySliderY(int y) {
    const LeftSliderGeom G = GetLeftSliderGeom();
    const double frac = 1.0 - static_cast<double>(y - G.oy1) / std::max(1, G.sliderH);
    g_opacityPercent = std::max(0, std::min(100, static_cast<int>(frac * 100.0 + 0.5)));
    if (g.tool) g.tool->SetOpacity(g_opacityPercent / 100.0);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

bool HandleLeftPanelClick(int x, int y) {
    if (x >= kLeftPanelWidth) return false;
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = CommandBarHeight();
    if (y < top || y >= client.bottom - kStatusHeight) return false;
    const LeftSliderGeom G = GetLeftSliderGeom();
    if (y >= G.sy1 - 6 && y <= G.sy2 + 6) {
        g_dragControl = 1;
        SetCapture(g.hwnd);
        ApplySizeSliderY(y);
        return true;
    }
    if (y >= G.oy1 - 6 && y <= G.oy2 + 6) {
        g_dragControl = 2;
        SetCapture(g.hwnd);
        ApplyOpacitySliderY(y);
        return true;
    }
    return true;
}

void ApplyZoomSliderX(int x) {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const StatusGeom S = GetStatusGeom(client);
    const int sl = S.slider.left + 8;
    const int sr = S.slider.right - 8;
    const double frac = static_cast<double>(x - sl) / std::max(1, sr - sl);
    g.zoom = std::max(1, std::min(16, 1 + static_cast<int>(frac * 15.0 + 0.5)));
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

bool HandleStatusBarClick(int x, int y) {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = client.bottom - kStatusHeight;
    if (y < top) return false;
    const StatusGeom S = GetStatusGeom(client);
    if (HitRect(S.fit, x, y)) { DoCommand(311); return true; }
    if (HitRect(S.zoomOut, x, y)) {
        g.zoom = std::max(1, g.zoom / 2);
        InvalidateRect(g.hwnd, nullptr, FALSE);
        return true;
    }
    if (HitRect(S.slider, x, y)) {
        g_dragControl = 3;
        SetCapture(g.hwnd);
        ApplyZoomSliderX(x);
        return true;
    }
    if (HitRect(S.zoomIn, x, y)) {
        g.zoom = std::min(16, g.zoom * 2);
        InvalidateRect(g.hwnd, nullptr, FALSE);
        return true;
    }
    return true;
}

void UpdateHover(int x, int y) {
    int newCmd = 0;
    int newShape = -1;
    int newPal = -1;
    bool newC1 = false, newC2 = false, newEdit = false;
    int newStatus = 0;

    if (g_commandBarVisible && y < kCommandBarHeight) {
        CommandLayout L = BuildCommandLayout();
        for (const auto& b : L.buttons) {
            if (HitRect(b.rect, x, y)) { newCmd = b.command; break; }
        }
        if (newCmd == 0) {
            for (int i = 0; i < static_cast<int>(L.shapeButtons.size()); ++i) {
                if (HitRect(L.shapeButtons[i].rect, x, y)) { newShape = i; break; }
            }
        }
        if (HitRect(L.color1, x, y)) newC1 = true;
        else if (HitRect(L.color2, x, y)) newC2 = true;
        else if (HitRect(L.editColors, x, y)) newEdit = true;
        else {
            for (int i = 0; i < static_cast<int>(L.palette.size()); ++i) {
                if (HitRect(L.palette[i], x, y)) { newPal = i; break; }
            }
        }
    } else {
        RECT client;
        GetClientRect(g.hwnd, &client);
        if (y >= client.bottom - kStatusHeight) {
            const StatusGeom S = GetStatusGeom(client);
            if (HitRect(S.fit, x, y)) newStatus = 1;
            else if (HitRect(S.zoomOut, x, y)) newStatus = 2;
            else if (HitRect(S.zoomIn, x, y)) newStatus = 3;
        }
    }

    if (newCmd != g_hoverCmd || newShape != g_hoverShapeIdx || newPal != g_hoverPaletteIdx ||
        newC1 != g_hoverColor1 || newC2 != g_hoverColor2 || newEdit != g_hoverEditColors ||
        newStatus != g_hoverStatusPart) {
        g_hoverCmd = newCmd;
        g_hoverShapeIdx = newShape;
        g_hoverPaletteIdx = newPal;
        g_hoverColor1 = newC1;
        g_hoverColor2 = newC2;
        g_hoverEditColors = newEdit;
        g_hoverStatusPart = newStatus;
        InvalidateRect(g.hwnd, nullptr, FALSE);
    }
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
    if (id >= 600 && id <= 619) {
        g_shapeKind = static_cast<ShapeKind>(id - 600);
        g.toolKind = PaintToolKind::AutoShape;
        RebuildTool();
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return;
    }
    if (id == 307) {
        g_rulerVisible = !g_rulerVisible;
        if (HMENU m = GetMenu(g.hwnd))
            CheckMenuItem(m, 307, MF_BYCOMMAND | (g_rulerVisible ? MF_CHECKED : MF_UNCHECKED));
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return;
    }
    if (id == 308) {
        g_commandBarVisible = !g_commandBarVisible;
        if (HMENU m = GetMenu(g.hwnd))
            CheckMenuItem(m, 308, MF_BYCOMMAND | (g_commandBarVisible ? MF_CHECKED : MF_UNCHECKED));
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return;
    }
    if (id == 9001) {
        int curT = 1; g.attributes.GetValue(1, curT);
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING | (curT == 0 ? MF_CHECKED : 0), 9201, L"No outline");
        AppendMenuW(m, MF_STRING | (curT > 0 ? MF_CHECKED : 0), 9202, L"Solid color");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (curT == 1 ? MF_CHECKED : 0), 9203, L"1px");
        AppendMenuW(m, MF_STRING | (curT == 3 ? MF_CHECKED : 0), 9204, L"3px");
        AppendMenuW(m, MF_STRING | (curT == 5 ? MF_CHECKED : 0), 9205, L"5px");
        AppendMenuW(m, MF_STRING | (curT == 8 ? MF_CHECKED : 0), 9206, L"8px");
        POINT pt; GetCursorPos(&pt);
        int sel = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g.hwnd, nullptr);
        DestroyMenu(m);
        if (sel == 9201 || sel == 9202 || sel == 9203 || sel == 9204 || sel == 9205 || sel == 9206) {
            int t = sel == 9201 ? 0 : sel == 9203 ? 1 : sel == 9204 ? 3 : sel == 9205 ? 5 : 8;
            g.attributes.SetValue(1, t); if (g.tool) g.tool->SetThickness(static_cast<unsigned>(t));
            InvalidateRect(g.hwnd, nullptr, TRUE);
        }
        return;
    }
    if (id == 9002) {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING | (!g_shapeFilled ? MF_CHECKED : 0), 9211, L"No fill");
        AppendMenuW(m, MF_STRING | (g_shapeFilled ? MF_CHECKED : 0), 9212, L"Solid color");
        POINT pt; GetCursorPos(&pt);
        int sel = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g.hwnd, nullptr);
        DestroyMenu(m);
        if (sel == 9211) { g_shapeFilled = false; RebuildTool(); InvalidateRect(g.hwnd, nullptr, TRUE); }
        if (sel == 9212) { g_shapeFilled = true; RebuildTool(); InvalidateRect(g.hwnd, nullptr, TRUE); }
        return;
    }
    if (id == 9004) {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::Selection ? MF_CHECKED : 0), 800, L"Rectangular selection");
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::FreeFormSelection ? MF_CHECKED : 0), 801, L"Free-form selection");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        // REA: PaintUI.SelectionCommandBarViewModel exposes Cut/Copy/Paste,
        // SelectAll and ClearSelection commands (ev_ffbc7e54...).
        AppendMenuW(m, MF_STRING, 202, L"Cut\tCtrl+X");
        AppendMenuW(m, MF_STRING, 203, L"Copy\tCtrl+C");
        AppendMenuW(m, MF_STRING, 204, L"Paste\tCtrl+V");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, 206, L"Select all\tCtrl+A");
        AppendMenuW(m, MF_STRING, 205, L"Clear image");
        POINT pt; GetCursorPos(&pt);
        int sel = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g.hwnd, nullptr);
        DestroyMenu(m);
        if (sel) DoCommand(static_cast<WORD>(sel));
        return;
    }
    if (id == 9005) {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, 401, L"Flip horizontal");
        AppendMenuW(m, MF_STRING, 402, L"Flip vertical");
        POINT pt; GetCursorPos(&pt);
        int sel = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g.hwnd, nullptr);
        DestroyMenu(m);
        if (sel) DoCommand(static_cast<WORD>(sel));
        return;
    }
    if (id == 9006) {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, 403, L"Rotate right 90\u00B0");
        AppendMenuW(m, MF_STRING, 9221, L"Rotate left 90\u00B0");
        AppendMenuW(m, MF_STRING, 9222, L"Rotate 180\u00B0");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, 406, L"Custom rotate...");
        POINT pt; GetCursorPos(&pt);
        int sel = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g.hwnd, nullptr);
        DestroyMenu(m);
        if (sel == 9221) WholeImageOperation(OperationKind::Rotate, [](Image& i) { i.Rotate(4.7123889803846899); });
        else if (sel == 9222) WholeImageOperation(OperationKind::Rotate, [](Image& i) { i.Rotate(3.1415926535897932); });
        else if (sel) DoCommand(static_cast<WORD>(sel));
        return;
    }
    if (id == 9003) {
        int curT = 1; g.attributes.GetValue(1, curT);
        HMENU m = CreatePopupMenu();
        const int sizes[] = {1,3,5,8};
        for (int i = 0; i < 4; ++i)
            AppendMenuW(m, MF_STRING | (curT == sizes[i] ? MF_CHECKED : 0),
                        9100 + sizes[i], (std::to_wstring(sizes[i]) + L"px").c_str());
        POINT pt; GetCursorPos(&pt);
        int sel = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g.hwnd, nullptr);
        DestroyMenu(m);
        if (sel >= 9101) { g.attributes.SetValue(1, sel - 9100); if (g.tool) g.tool->SetThickness(static_cast<unsigned>(sel - 9100)); InvalidateRect(g.hwnd, nullptr, TRUE); }
        return;
    }
    if (id == 807) {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::Brush ? MF_CHECKED : 0), 701, L"Brush");
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::Calligraphic ? MF_CHECKED : 0), 703, L"Calligraphy brush");
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::Airbrush ? MF_CHECKED : 0), 702, L"Airbrush");
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::OilBrush ? MF_CHECKED : 0), 704, L"Oil brush");
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::Crayon ? MF_CHECKED : 0), 707, L"Crayon");
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::Marker ? MF_CHECKED : 0), 706, L"Marker");
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::NaturalPencil ? MF_CHECKED : 0), 708, L"Natural pencil");
        AppendMenuW(m, MF_STRING | (g.toolKind == PaintToolKind::Watercolor ? MF_CHECKED : 0), 705, L"Watercolour");
        POINT pt; GetCursorPos(&pt);
        int sel = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g.hwnd, nullptr);
        DestroyMenu(m);
        if (sel >= 700) DoCommand(static_cast<WORD>(sel));
        return;
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
        case 304:
            g.showGrid = !g.showGrid;
            if (HMENU m = GetMenu(g.hwnd))
                CheckMenuItem(m, 304, MF_BYCOMMAND | (g.showGrid ? MF_CHECKED : MF_UNCHECKED));
            InvalidateRect(g.hwnd, nullptr, TRUE);
            break;
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
        case 407: {
            Rect r = g.tool ? g.tool->SelectionRect() : Rect{};
            if (r.w > 0 && r.h > 0) {
                WholeImageOperation(OperationKind::Crop, [&](Image& image) { image.Crop(r); });
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
            // REA: PaintUI.RotateCustomDialogViewModel.RotationAngle — accept an
            // arbitrary signed angle plus optional flips instead of presets.
            RotateCustomOptions options;
            if (!ShowRotateCustomDialog(g.hwnd, options)) break;
            const double radians = ToRadian(options.angleDegrees);
            WholeImageOperation(OperationKind::Rotate, [&](Image& image) {
                if (options.flipHorizontal) image.HorizontalFlip();
                if (options.flipVertical) image.VerticalFlip();
                if (std::abs(options.angleDegrees) > 0.0001) image.Rotate(radians);
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
            AppendMenuW(view, MF_STRING | MF_CHECKED, 308, L"Show &Command bar");
            AppendMenuW(view, MF_STRING, 309, L"&Full screen");
            AppendMenuW(view, MF_STRING, 310, L"&Thumbnail");
            HMENU imageMenu = CreatePopupMenu();
            AppendMenuW(imageMenu, MF_STRING, 400, L"&Invert Colors");
            AppendMenuW(imageMenu, MF_STRING, 401, L"Flip &Horizontal");
            AppendMenuW(imageMenu, MF_STRING, 402, L"Flip &Vertical");
            AppendMenuW(imageMenu, MF_STRING, 403, L"Rotate &90°");
            AppendMenuW(imageMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(imageMenu, MF_STRING, 404, L"Image &Properties...");
            AppendMenuW(imageMenu, MF_STRING, 405, L"&Resize and skew...");
            AppendMenuW(imageMenu, MF_STRING, 406, L"Flip / &Rotate...");
            HMENU help = CreatePopupMenu();
            AppendMenuW(help, MF_STRING, 500, L"&About PaintRebuild...");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(imageMenu), L"&Image");
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
            {
                const RECT canvas = CanvasRect();
                const ResizeEdge edge = CanvasResizeHit(x, y, canvas);
                if (edge != ResizeEdge::None) {
                    g.resizeEdge = (edge == ResizeEdge::Right) ? 1 : (edge == ResizeEdge::Bottom) ? 2 : 3;
                    g_resizeOrigW = g.image->width(); g_resizeOrigH = g.image->height();
                    g_resizeStartX = x; g_resizeStartY = y;
                    g.drawing = true; SetCapture(hwnd);
                    return 0;
                }
            }
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
            if (g.toolKind == PaintToolKind::Zoom) {
                if (auto* z = dynamic_cast<ZoomTool*>(g.tool.get())) g.zoom = std::max(1, std::min(16, z->ZoomFactor()));
            }
            if (g.toolKind == PaintToolKind::PickColor && g.context) {
                g.foreground = g.context->foreground;
                RebuildTool();
            }
            g.drawing = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            if (g_dragControl == 1) { ApplySizeSliderY(y); return 0; }
            if (g_dragControl == 2) { ApplyOpacitySliderY(y); return 0; }
            if (g_dragControl == 3) { ApplyZoomSliderX(x); return 0; }
            if (g.drawing && g.resizeEdge != 0) {
                SetCursor(LoadCursor(nullptr, g.resizeEdge == 1 ? IDC_SIZEWE : g.resizeEdge == 2 ? IDC_SIZENS : IDC_SIZENWSE));
                return 0;
            }
            UpdateHover(x, y);
            const RECT canvas = CanvasRect();
            const ResizeEdge hovEdge = CanvasResizeHit(x, y, canvas);
            if (hovEdge != ResizeEdge::None) {
                SetCursor(LoadCursor(nullptr, hovEdge == ResizeEdge::Right ? IDC_SIZEWE : hovEdge == ResizeEdge::Bottom ? IDC_SIZENS : IDC_SIZENWSE));
            } else if (HitRect(canvas, x, y)) {
                SetCursor(LoadCursor(nullptr, g.toolKind == PaintToolKind::Text ? IDC_IBEAM : IDC_CROSS));
            } else {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
            }
            const pr::Point p = ToImagePoint(x, y);
            if (p.x != g.cursorX || p.y != g.cursorY) {
                g.cursorX = p.x;
                g.cursorY = p.y;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (g.drawing && g.tool) {
                g.tool->OnStylusMove(p);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_hoverCmd = 0;
            g_hoverShapeIdx = -1;
            g_hoverPaletteIdx = -1;
            g_hoverColor1 = g_hoverColor2 = g_hoverEditColors = false;
            g_hoverStatusPart = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (delta > 0) g.zoom = std::min(16, g.zoom * 2);
                else if (delta < 0) g.zoom = std::max(1, g.zoom / 2);
                ClampScroll();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (GetKeyState(VK_SHIFT) & 0x8000) g_scrollX -= delta / 3;
            else g_scrollY -= delta / 3;
            ClampScroll();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (g_dragControl != 0) {
                g_dragControl = 0;
                ReleaseCapture();
                return 0;
            }
            if (g.drawing && g.resizeEdge != 0) {
                const int dx = (GET_X_LPARAM(lParam) - g_resizeStartX) / std::max(1, g.zoom);
                const int dy = (GET_Y_LPARAM(lParam) - g_resizeStartY) / std::max(1, g.zoom);
                int nw = g_resizeOrigW, nh = g_resizeOrigH;
                if (g.resizeEdge == 1 || g.resizeEdge == 3) nw = std::max(1, g_resizeOrigW + dx);
                if (g.resizeEdge == 2 || g.resizeEdge == 3) nh = std::max(1, g_resizeOrigH + dy);
                if (nw != g.image->width() || nh != g.image->height()) {
                    WholeImageOperation(OperationKind::Resize, [&](Image& image) {
                        image.Resize(Size{nw, nh}, ResizeMode::Nearest, ResizeDirection::Both);
                    });
                }
                g.resizeEdge = 0; g.drawing = false; ReleaseCapture();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (g.drawing && g.tool) {
                const pr::Point p = ToImagePoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                g.tool->OnStylusUp(p);
                g.drawing = g.tool->IsDrawing();
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
                const pr::Point p = ToImagePoint(x, y);
                if (g.toolKind == PaintToolKind::Zoom) {
                    g.zoom = std::max(1, g.zoom / 2); InvalidateRect(hwnd, nullptr, TRUE); return 0;
                }
                if (g.toolKind == PaintToolKind::PickColor && p.x >= 0 && p.y >= 0 && p.x < g.image->width() && p.y < g.image->height()) {
                    g.background = g.image->GetPixel(p); RebuildTool(); InvalidateRect(hwnd, nullptr, TRUE); return 0;
                }
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
                    case VK_OEM_PLUS: case VK_ADD: g.zoom = std::min(16, g.zoom * 2); InvalidateRect(hwnd, nullptr, TRUE); return 0;
                    case VK_OEM_MINUS: case VK_SUBTRACT: g.zoom = std::max(1, g.zoom / 2); InvalidateRect(hwnd, nullptr, TRUE); return 0;
                    case '0': g.zoom = 1; InvalidateRect(hwnd, nullptr, TRUE); return 0;
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
