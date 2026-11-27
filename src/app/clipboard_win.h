// Win32 system clipboard bridge (CF_DIB). The headless SelectionClipboard in
// src/core remains the model-level implementation used by selftests.
#pragma once

#include <memory>

#include "../core/image.h"

bool CopyImageToClipboard(void* hwnd, const pr::Image& image, const pr::Rect& rect);
std::unique_ptr<pr::Image> PasteImageFromClipboard();
