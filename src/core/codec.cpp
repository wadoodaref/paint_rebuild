#include "codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <vector>

namespace pr {
namespace {

void PutU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void PutU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
}

std::uint16_t GetU16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

std::uint32_t GetU32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
    return value;
}

bool ReadWholeFile(const std::wstring& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    if (length <= 0) return false;
    file.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(length));
    return static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), length));
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
    });
    return value;
}

}  // namespace

ImageFormat DetectFormat(const std::wstring& path) {
    const std::wstring lower = Lower(path);
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".bmp") == 0) return ImageFormat::Bmp;
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".png") == 0) return ImageFormat::Png;
    if (lower.size() >= 4 && (lower.compare(lower.size() - 4, 4, L".jpg") == 0 ||
                              lower.compare(lower.size() - 5, 4, L".jpeg") == 0)) return ImageFormat::Jpeg;
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".gif") == 0) return ImageFormat::Gif;
    if (lower.size() >= 4 && (lower.compare(lower.size() - 4, 4, L".tif") == 0 ||
                              lower.compare(lower.size() - 5, 4, L".tiff") == 0)) return ImageFormat::Tiff;
    return ImageFormat::Unknown;
}

const wchar_t* FormatName(ImageFormat format) {
    switch (format) {
        case ImageFormat::Bmp: return L"BMP";
        case ImageFormat::Png: return L"PNG";
        case ImageFormat::Jpeg: return L"JPEG";
        case ImageFormat::Gif: return L"GIF";
        case ImageFormat::Tiff: return L"TIFF";
        default: return L"Unknown";
    }
}

bool SaveBmp(const Image& image, const std::wstring& path) {
    if (image.width() <= 0 || image.height() <= 0) return false;
    const std::uint32_t rowBytes = static_cast<std::uint32_t>(image.width()) * 4u;
    const std::uint32_t pixelBytes = rowBytes * static_cast<std::uint32_t>(image.height());
    const std::uint32_t fileBytes = 14u + 40u + pixelBytes;

    std::vector<std::uint8_t> bytes;
    bytes.reserve(fileBytes);
    PutU16(bytes, 0x4D42);  // 'BM'
    PutU32(bytes, fileBytes);
    PutU16(bytes, 0);
    PutU16(bytes, 0);
    PutU32(bytes, 14u + 40u);      // pixel offset
    PutU32(bytes, 40);             // BITMAPINFOHEADER size
    PutU32(bytes, static_cast<std::uint32_t>(image.width()));
    PutU32(bytes, static_cast<std::uint32_t>(image.height()));
    PutU16(bytes, 1);              // planes
    PutU16(bytes, 32);             // bits per pixel
    PutU32(bytes, 0);              // BI_RGB
    PutU32(bytes, pixelBytes);
    PutU32(bytes, 0); PutU32(bytes, 0); PutU32(bytes, 0); PutU32(bytes, 0);

    for (int y = image.height() - 1; y >= 0; --y) {
        for (int x = 0; x < image.width(); ++x) {
            const Color c = image.GetPixel(Point{x, y});
            bytes.push_back(Blue(c));
            bytes.push_back(Green(c));
            bytes.push_back(Red(c));
            bytes.push_back(Alpha(c));
        }
    }

    std::ofstream file(path.c_str(), std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

std::unique_ptr<Image> LoadBmp(const std::wstring& path) {
    std::vector<std::uint8_t> bytes;
    if (!ReadWholeFile(path, bytes) || bytes.size() < 54 || GetU16(bytes, 0) != 0x4D42)
        return nullptr;

    const std::uint32_t pixelOffset = GetU32(bytes, 10);
    const std::uint32_t headerSize = GetU32(bytes, 14);
    if (headerSize < 40 || pixelOffset < 14u + headerSize) return nullptr;

    const std::int32_t rawWidth = static_cast<std::int32_t>(GetU32(bytes, 18));
    const std::int32_t rawHeight = static_cast<std::int32_t>(GetU32(bytes, 22));
    const std::uint16_t bpp = GetU16(bytes, 28);
    const std::uint32_t compression = GetU32(bytes, 30);
    if (rawWidth <= 0 || rawHeight == 0 || compression != 0 ||
        (bpp != 24 && bpp != 32) || pixelOffset > bytes.size())
        return nullptr;

    const bool topDown = rawHeight < 0;
    const int width = rawWidth;
    const int height = topDown ? -rawHeight : rawHeight;
    const std::size_t stride =
        ((static_cast<std::size_t>(width) * bpp + 31u) / 32u) * 4u;
    if (pixelOffset + stride * static_cast<std::size_t>(height) > bytes.size()) return nullptr;

    auto image = Image::Create(Size{width, height});
    for (int row = 0; row < height; ++row) {
        const std::size_t sourceRow = pixelOffset + static_cast<std::size_t>(row) * stride;
        const int destinationY = topDown ? row : height - 1 - row;
        for (int x = 0; x < width; ++x) {
            const std::size_t at = sourceRow + static_cast<std::size_t>(x) * (bpp / 8u);
            const std::uint8_t b = bytes[at];
            const std::uint8_t g = bytes[at + 1];
            const std::uint8_t r = bytes[at + 2];
            const std::uint8_t a = (bpp == 32) ? bytes[at + 3] : 255;
            image->SetPixel(Point{x, destinationY}, MakeColor(r, g, b, a));
        }
    }
    return image;
}

}  // namespace pr
