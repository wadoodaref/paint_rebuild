#define NOMINMAX
#include <windows.h>

#include <cstdio>

#include "dialogs_win.h"

namespace {

enum class DialogKind { StretchSkew, FlipRotate, Attributes };

struct DialogState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    DialogKind kind = DialogKind::Attributes;
    bool accepted = false;
    bool running = false;
    int values[4] = {100, 100, 0, 0};
};

DialogState g_dialog;

HWND AddControl(HWND parent, const wchar_t* classname, const wchar_t* text,
                DWORD style, int id, int x, int y, int w, int h) {
    static HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                    0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HWND control = CreateWindowExW(0, classname, text,
                                   WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                   parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                                   nullptr, nullptr);
    if (control && font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

void AddLabel(HWND parent, const wchar_t* text, int x, int y, int w) {
    AddControl(parent, L"Static", text, 0, 0, x, y, w, 18);
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            const DialogKind kind = g_dialog.kind;
            if (kind == DialogKind::StretchSkew) {
                AddLabel(hwnd, L"Stretch", 16, 16, 80);
                AddLabel(hwnd, L"Skew", 16, 96, 80);
                AddLabel(hwnd, L"Horizontal %", 16, 40, 110);
                AddLabel(hwnd, L"Vertical %", 200, 40, 100);
                AddLabel(hwnd, L"Horizontal degrees", 16, 120, 140);
                AddLabel(hwnd, L"Vertical degrees", 200, 120, 130);
                AddControl(hwnd, L"Edit", L"100", WS_BORDER | WS_TABSTOP, 10, 130, 38, 60, 20);
                AddControl(hwnd, L"Edit", L"100", WS_BORDER | WS_TABSTOP, 11, 306, 38, 60, 20);
                AddControl(hwnd, L"Edit", L"0", WS_BORDER | WS_TABSTOP, 12, 160, 118, 60, 20);
                AddControl(hwnd, L"Edit", L"0", WS_BORDER | WS_TABSTOP, 13, 340, 118, 60, 20);
            } else if (kind == DialogKind::FlipRotate) {
                AddControl(hwnd, L"Button", L"Flip horizontal", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                           20, 20, 16, 180, 20);
                AddControl(hwnd, L"Button", L"Flip vertical", BS_AUTORADIOBUTTON | WS_TABSTOP,
                           21, 20, 44, 180, 20);
                AddControl(hwnd, L"Button", L"Rotate 90 degrees", BS_AUTORADIOBUTTON | WS_TABSTOP,
                           22, 20, 68, 180, 20);
                AddControl(hwnd, L"Button", L"Rotate 180 degrees", BS_AUTORADIOBUTTON | WS_TABSTOP,
                           23, 20, 92, 180, 20);
                AddControl(hwnd, L"Button", L"Rotate 270 degrees", BS_AUTORADIOBUTTON | WS_TABSTOP,
                           24, 20, 116, 180, 20);
                CheckDlgButton(hwnd, 22, BST_CHECKED);
            } else {
                AddLabel(hwnd, L"Width (pixels)", 16, 24, 120);
                AddLabel(hwnd, L"Height (pixels)", 16, 56, 120);
                wchar_t buffer[32];
                swprintf(buffer, 32, L"%d", g_dialog.values[0]);
                AddControl(hwnd, L"Edit", buffer, WS_BORDER | WS_TABSTOP, 10, 140, 22, 80, 20);
                swprintf(buffer, 32, L"%d", g_dialog.values[1]);
                AddControl(hwnd, L"Edit", buffer, WS_BORDER | WS_TABSTOP, 11, 140, 54, 80, 20);
            }
            AddControl(hwnd, L"Button", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK,
                       (kind == DialogKind::FlipRotate ? 40 : 120), 160, 80, 24);
            AddControl(hwnd, L"Button", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL,
                       (kind == DialogKind::FlipRotate ? 140 : 220), 160, 80, 24);
            return 0;
        }
        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            if (id == IDOK) {
                if (g_dialog.kind == DialogKind::FlipRotate) {
                    g_dialog.values[0] = 2;
                    for (int i = 0; i < 5; ++i) {
                        if (IsDlgButtonChecked(hwnd, 20 + i) == BST_CHECKED) {
                            g_dialog.values[0] = i;
                            break;
                        }
                    }
                } else {
                    for (int i = 0; i < (g_dialog.kind == DialogKind::Attributes ? 2 : 4); ++i) {
                        BOOL translated = FALSE;
                        g_dialog.values[i] = GetDlgItemInt(hwnd, 10 + i, &translated, TRUE);
                        if (!translated) g_dialog.values[i] = 0;
                    }
                }
                g_dialog.accepted = true;
            }
            if (id == IDOK || id == IDCANCEL) {
                g_dialog.running = false;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            g_dialog.running = false;
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool RunModal(HWND owner, DialogKind kind, const wchar_t* title, int width, int height) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"PaintRebuildDialog";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    g_dialog = DialogState{};
    g_dialog.kind = kind;
    g_dialog.owner = owner;
    g_dialog.running = true;

    RECT ownerRect{};
    if (owner && GetWindowRect(owner, &ownerRect)) {
        const int x = ownerRect.left + (ownerRect.right - ownerRect.left - width) / 2;
        const int y = ownerRect.top + (ownerRect.bottom - ownerRect.top - height) / 2;
        g_dialog.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PaintRebuildDialog", title,
                                        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
                                        owner, nullptr, GetModuleHandle(nullptr), nullptr);
    } else {
        g_dialog.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PaintRebuildDialog", title,
                                        WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                        CW_USEDEFAULT, width, height, nullptr, nullptr,
                                        GetModuleHandle(nullptr), nullptr);
    }
    if (!g_dialog.hwnd) return false;

    EnableWindow(owner, FALSE);
    ShowWindow(g_dialog.hwnd, SW_SHOW);
    SetForegroundWindow(g_dialog.hwnd);
    MSG message;
    while (g_dialog.running && GetMessageW(&message, nullptr, 0, 0)) {
        if (!IsDialogMessageW(g_dialog.hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return g_dialog.accepted;
}

}  // namespace

bool ShowStretchSkewDialog(HWND owner, StretchSkewOptions& options) {
    g_dialog.values[0] = options.horizontalPercent;
    g_dialog.values[1] = options.verticalPercent;
    g_dialog.values[2] = options.skewHorizontal;
    g_dialog.values[3] = options.skewVertical;
    if (!RunModal(owner, DialogKind::StretchSkew, L"Stretch and Skew", 420, 220)) return false;
    options.horizontalPercent = g_dialog.values[0];
    options.verticalPercent = g_dialog.values[1];
    options.skewHorizontal = g_dialog.values[2];
    options.skewVertical = g_dialog.values[3];
    return true;
}

bool ShowFlipRotateDialog(HWND owner, FlipRotateOptions& options) {
    g_dialog.values[0] = static_cast<int>(options.choice);
    if (!RunModal(owner, DialogKind::FlipRotate, L"Flip and Rotate", 260, 220)) return false;
    const int choice = g_dialog.values[0];
    options.choice = (choice >= 0 && choice <= 4) ? static_cast<FlipRotateChoice>(choice)
                                                  : FlipRotateChoice::Rotate90;
    return true;
}

bool ShowAttributesDialog(HWND owner, int& width, int& height) {
    g_dialog.values[0] = width;
    g_dialog.values[1] = height;
    if (!RunModal(owner, DialogKind::Attributes, L"Image Properties", 320, 230)) return false;
    width = g_dialog.values[0];
    height = g_dialog.values[1];
    return true;
}
