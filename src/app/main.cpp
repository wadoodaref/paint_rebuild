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
#include "print_win.h"
#include "recovery.h"
#include "text_render.h"
#include "../core/image.h"
#include "../core/operation.h"
#include "../tools/tool.h"

using namespace pr;

namespace {

constexpr int kToolBoxWidth = 64;
constexpr int kPaletteHeight = 76;
constexpr int kStatusHeight = 24;
constexpr int kRibbonHeight = 56;

bool g_ribbonVisible = true;
bool g_rulerVisible = false;

struct RibbonButton {
    RECT rect;
    int command = 0;
    const wchar_t* label = nullptr;
};

std::vector<RibbonButton> RibbonButtons() {
    std::vector<RibbonButton> buttons;
    const int y = 26;
    const auto add = [&](int x, int w, int command, const wchar_t* label) {
        buttons.push_back(RibbonButton{RECT{x, y, x + w, y + 24}, command, label});
    };
    add(70, 46, 200, L"Undo");
    add(120, 46, 201, L"Redo");
    add(180, 46, 100, L"New");
    add(230, 46, 101, L"Open");
    add(280, 46, 102, L"Save");
    add(360, 54, 800, L"Select");
    add(418, 54, 806, L"Pencil");
    add(476, 54, 802, L"Eraser");
    add(534, 54, 803, L"Fill");
    add(592, 54, 804, L"Picker");
    add(650, 54, 805, L"Zoom");
    add(730, 54, 807, L"Brush");
    add(788, 54, 808, L"Airbrush");
    add(870, 54, 811, L"Line");
    add(928, 54, 814, L"Rect");
    add(986, 54, 815, L"Oval");
    return buttons;
}

enum class StatusButton { BrushMinus, BrushPlus, ZoomMinus, ZoomPlus };

RECT StatusButtonRect(HWND hwnd, StatusButton button) {
    RECT client;
    GetClientRect(hwnd, &client);
    const int top = client.bottom - kStatusHeight + 2;
    switch (button) {
        case StatusButton::BrushMinus: return RECT{client.right - 260, top, client.right - 236, top + 18};
        case StatusButton::BrushPlus:  return RECT{client.right - 232, top, client.right - 208, top + 18};
        case StatusButton::ZoomMinus:  return RECT{client.right - 120, top, client.right - 96, top + 18};
        case StatusButton::ZoomPlus:   return RECT{client.right - 92, top, client.right - 68, top + 18};
    }
    return RECT{};
}

bool HitRect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
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

const std::array<Color, 28> kPalette = {
    MakeColor(0,0,0), MakeColor(128,128,128), MakeColor(128,0,0), MakeColor(128,64,0),
    MakeColor(128,128,0), MakeColor(0,128,0), MakeColor(0,128,128), MakeColor(0,0,128),
    MakeColor(128,0,128), MakeColor(128,64,64), MakeColor(0,64,64), MakeColor(64,0,128),
    MakeColor(0,128,64), MakeColor(0,0,0), MakeColor(255,255,255), MakeColor(192,192,192),
    MakeColor(255,0,0), MakeColor(255,128,0), MakeColor(255,255,0), MakeColor(0,255,0),
    MakeColor(0,255,255), MakeColor(0,0,255), MakeColor(255,0,255), MakeColor(255,128,128),
    MakeColor(128,255,128), MakeColor(128,255,255), MakeColor(128,128,255), MakeColor(192,192,192)};

void RebuildTool() {
    g.context = std::make_unique<ToolContext>(*g.image, g.stock, g.attributes,
                                              g.foreground, g.background);
    g.tool = g.toolKind == PaintToolKind::AutoShape
                 ? CreateAutoShapeTool(*g.context, g_shapeKind)
                 : CreatePaintTool(*g.context, g.toolKind);
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

RECT CanvasRect() {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = g_ribbonVisible ? kRibbonHeight + 4 : 4;
    return RECT{kToolBoxWidth + 4, top, client.right - 4,
                client.bottom - kPaletteHeight - kStatusHeight - 4};
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

void DrawToolBox(HDC hdc) {
    RECT client;
    GetClientRect(g.hwnd, &client);
    HBRUSH face = CreateSolidBrush(RGB(240, 240, 240));
    FillRectValue(hdc, RECT{0, 0, kToolBoxWidth, client.bottom}, face);
    DeleteObject(face);
    for (std::size_t i = 0; i < kTools.size(); ++i) {
        const int x = 4 + (static_cast<int>(i) / 9) * 28;
        const int y = (g_ribbonVisible ? kRibbonHeight + 6 : 6) +
                      (static_cast<int>(i) % 9) * 30;
        const bool selected = kTools[i] == g.toolKind;
        HBRUSH brush = CreateSolidBrush(selected ? RGB(180, 200, 235) : RGB(225, 225, 225));
        FillRectValue(hdc, RECT{x, y, x + 26, y + 26}, brush);
        DeleteObject(brush);
        Rectangle(hdc, x, y, x + 26, y + 26);
        SetBkMode(hdc, TRANSPARENT);
        const char* name = ToolName(kTools[i]);
        const char initial = name[0];
        TextOutA(hdc, x + 9, y + 6, &initial, 1);
    }
}

void DrawPalette(HDC hdc) {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = client.bottom - kPaletteHeight - kStatusHeight;
    HBRUSH face = CreateSolidBrush(RGB(240, 240, 240));
    FillRectValue(hdc, RECT{0, top, client.right, client.bottom - kStatusHeight}, face);
    DeleteObject(face);

    // Foreground/background swatches.
    Rectangle(hdc, 6, top + 8, 42, top + 44);
    HBRUSH fg = CreateSolidBrush(RGB(Red(g.foreground), Green(g.foreground), Blue(g.foreground)));
    FillRectValue(hdc, RECT{10, top + 12, 38, top + 40}, fg);
    DeleteObject(fg);
    Rectangle(hdc, 22, top + 24, 58, top + 60);
    HBRUSH bg = CreateSolidBrush(RGB(Red(g.background), Green(g.background), Blue(g.background)));
    FillRectValue(hdc, RECT{26, top + 28, 54, top + 56}, bg);
    DeleteObject(bg);

    for (std::size_t i = 0; i < kPalette.size(); ++i) {
        const int col = static_cast<int>(i % 14);
        const int row = static_cast<int>(i / 14);
        const int x = 70 + col * 24;
        const int y = top + 10 + row * 26;
        HBRUSH swatch = CreateSolidBrush(RGB(Red(kPalette[i]), Green(kPalette[i]), Blue(kPalette[i])));
        FillRectValue(hdc, RECT{x, y, x + 22, y + 22}, swatch);
        DeleteObject(swatch);
        Rectangle(hdc, x, y, x + 22, y + 22);
    }
}

void DrawStatus(HDC hdc) {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = client.bottom - kStatusHeight;
    HBRUSH face = CreateSolidBrush(RGB(240, 240, 240));
    FillRectValue(hdc, RECT{0, top, client.right, client.bottom}, face);
    DeleteObject(face);
    std::wstring text;
    if (g.tool) {
        const char* name = g.toolKind == PaintToolKind::AutoShape ? ShapeName(g_shapeKind)
                                                                  : ToolName(g.tool->Kind());
        const int w = static_cast<int>(std::strlen(name));
        text.assign(w, L' ');
        for (int i = 0; i < w; ++i) text[i] = static_cast<wchar_t>(name[i]);
    }
    const std::wstring coords = L"  |  " + std::to_wstring(g.cursorX) + L", " +
                                std::to_wstring(g.cursorY);
    const std::wstring size = L"  |  " + std::to_wstring(g.image->width()) + L" x " +
                              std::to_wstring(g.image->height()) + L" px  |  " +
                              std::to_wstring(g.zoom) + L"00%";
    TextOutW(hdc, 8, top + 4, (text + coords + size).c_str(),
             static_cast<int>((text + coords + size).size()));

    int thickness = 1;
    g.attributes.GetValue(1, thickness);
    const std::wstring brushLabel = L"Brush " + std::to_wstring(thickness);
    TextOutW(hdc, client.right - 320, top + 4, brushLabel.c_str(),
             static_cast<int>(brushLabel.size()));
    const std::wstring zoomLabel = L"Zoom " + std::to_wstring(g.zoom * 100) + L"%";
    TextOutW(hdc, client.right - 180, top + 4, zoomLabel.c_str(),
             static_cast<int>(zoomLabel.size()));
    const StatusButton buttons[] = {StatusButton::BrushMinus, StatusButton::BrushPlus,
                                    StatusButton::ZoomMinus, StatusButton::ZoomPlus};
    const wchar_t* glyphs[] = {L"-", L"+", L"-", L"+"};
    for (int i = 0; i < 4; ++i) {
        const RECT box = StatusButtonRect(g.hwnd, buttons[i]);
        FillRect(hdc, &box, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        Rectangle(hdc, box.left, box.top, box.right, box.bottom);
        DrawTextW(hdc, glyphs[i], 1, const_cast<RECT*>(&box), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawRibbon(HDC hdc) {
    if (!g_ribbonVisible) return;
    RECT client;
    GetClientRect(g.hwnd, &client);
    HBRUSH face = CreateSolidBrush(RGB(245, 245, 245));
    FillRectValue(hdc, RECT{0, 0, client.right, kRibbonHeight}, face);
    DeleteObject(face);
    SetBkMode(hdc, TRANSPARENT);
    const wchar_t* tabs[] = {L"Home", L"View", L"Image"};
    for (int i = 0; i < 3; ++i) {
        const int x = 80 + i * 80;
        TextOutW(hdc, x, 4, tabs[i], static_cast<int>(wcslen(tabs[i])));
        if (i == 0) {
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
            const HPEN old = static_cast<HPEN>(SelectObject(hdc, pen));
            MoveToEx(hdc, x, 22, nullptr);
            LineTo(hdc, x + 48, 22);
            SelectObject(hdc, old);
            DeleteObject(pen);
        }
    }
    const int separators[] = {350, 720, 850};
    HPEN line = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    const HPEN oldLine = static_cast<HPEN>(SelectObject(hdc, line));
    for (const int sx : separators) {
        MoveToEx(hdc, sx, 4, nullptr);
        LineTo(hdc, sx, kRibbonHeight - 4);
    }
    SelectObject(hdc, oldLine);
    DeleteObject(line);
    for (const auto& button : RibbonButtons()) {
        FillRectValue(hdc, button.rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        Rectangle(hdc, button.rect.left, button.rect.top, button.rect.right, button.rect.bottom);
        RECT text = button.rect;
        DrawTextW(hdc, button.label, static_cast<int>(wcslen(button.label)), &text,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
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
    DrawRibbon(mem);
    DrawToolBox(mem);
    DrawPalette(mem);
    DrawStatus(mem);
    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

bool HandleToolBoxClick(int x, int y) {
    if (x < 0 || x >= kToolBoxWidth) return false;
    for (std::size_t i = 0; i < kTools.size(); ++i) {
        const int bx = 4 + (static_cast<int>(i) / 9) * 28;
        const int by = (g_ribbonVisible ? kRibbonHeight + 6 : 6) +
                       (static_cast<int>(i) % 9) * 30;
        if (x >= bx && x <= bx + 26 && y >= by && y <= by + 26) {
            FinishTextEditor(true);
            g.toolKind = kTools[i];
            RebuildTool();
            InvalidateRect(g.hwnd, nullptr, TRUE);
            return true;
        }
    }
    return true;
}

bool HandlePaletteClick(int x, int y, bool rightButton) {
    RECT client;
    GetClientRect(g.hwnd, &client);
    const int top = client.bottom - kPaletteHeight - kStatusHeight;
    if (y < top) return false;
    if (x >= 6 && x < 58) {
        if (rightButton) g.background = g.foreground;
        else g.foreground = g.background;
        RebuildTool();
        InvalidateRect(g.hwnd, nullptr, TRUE);
        return true;
    }
    for (std::size_t i = 0; i < kPalette.size(); ++i) {
        const int px = 70 + static_cast<int>(i % 14) * 24;
        const int py = top + 10 + static_cast<int>(i / 14) * 26;
        if (x >= px && x < px + 22 && y >= py && y < py + 22) {
            if (rightButton) g.background = kPalette[i];
            else g.foreground = kPalette[i];
            if (g.tool && g.tool->Kind() != PaintToolKind::Eraser &&
                g.tool->Kind() != PaintToolKind::PickColor)
                RebuildTool();
            InvalidateRect(g.hwnd, nullptr, TRUE);
            return true;
        }
    }
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
        g_ribbonVisible = !g_ribbonVisible;
        InvalidateRect(g.hwnd, nullptr, TRUE);
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
        case 304: g.showGrid = !g.showGrid; InvalidateRect(g.hwnd, nullptr, TRUE); break;
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
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            if (y >= clientRect.bottom - kStatusHeight) {
                if (HitRect(StatusButtonRect(hwnd, StatusButton::BrushMinus), x, y) ||
                    HitRect(StatusButtonRect(hwnd, StatusButton::BrushPlus), x, y)) {
                    int thickness = 1;
                    g.attributes.GetValue(1, thickness);
                    thickness += HitRect(StatusButtonRect(hwnd, StatusButton::BrushPlus), x, y) ? 1 : -1;
                    thickness = std::max(1, std::min(40, thickness));
                    g.attributes.SetValue(1, thickness);
                    if (g.tool) g.tool->SetThickness(static_cast<unsigned>(thickness));
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                if (HitRect(StatusButtonRect(hwnd, StatusButton::ZoomMinus), x, y) ||
                    HitRect(StatusButtonRect(hwnd, StatusButton::ZoomPlus), x, y)) {
                    const bool zoomIn = HitRect(StatusButtonRect(hwnd, StatusButton::ZoomPlus), x, y);
                    g.zoom = std::max(1, std::min(16, zoomIn ? g.zoom * 2 : g.zoom / 2));
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                return 0;
            }
            if (g_ribbonVisible && y < kRibbonHeight) {
                for (const auto& button : RibbonButtons()) {
                    if (HitRect(button.rect, x, y)) {
                        DoCommand(button.command);
                        return 0;
                    }
                }
                return 0;
            }
            if (HandleToolBoxClick(x, y)) return 0;
            if (HandlePaletteClick(x, y, false)) return 0;
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
            HandlePaletteClick(x, y, true);
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
                                CW_USEDEFAULT, CW_USEDEFAULT, 1060, 760,
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
