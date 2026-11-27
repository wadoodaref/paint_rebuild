// GDI printing and print preview (PaintUI CanvasPrintPreview analog).
#pragma once

#include <windows.h>

#include "../core/image.h"

bool PrintImage(HWND owner, const pr::Image& image);
void TogglePrintPreview(HWND owner, pr::Image* image);
void UpdatePrintPreviewImage(pr::Image* image);
bool IsPrintPreviewVisible();
