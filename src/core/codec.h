// Codec layer (ImageProcessing.dll analog). The headless core implements the
// native BMP path with no external libraries; WIC/GDI+ PNG/JPEG support is
// added in the Win32 app stage via windowscodecs.dll / gdiplus.dll.
#pragma once

#include <memory>
#include <string>

#include "image.h"

namespace pr {

enum class ImageFormat { Unknown, Bmp, Png, Jpeg, Gif, Tiff };

ImageFormat DetectFormat(const std::wstring& path);

// 32-bit uncompressed BMP (BI_RGB), written bottom-up like GDI DIBs.
bool SaveBmp(const Image& image, const std::wstring& path);
std::unique_ptr<Image> LoadBmp(const std::wstring& path);

const wchar_t* FormatName(ImageFormat format);

// WIC backend implemented in src/app/codec_wic.cpp (windowscodecs.dll).
std::unique_ptr<Image> LoadImageWic(const std::wstring& path);
bool SaveImageWic(const Image& image, const std::wstring& path);

}  // namespace pr
