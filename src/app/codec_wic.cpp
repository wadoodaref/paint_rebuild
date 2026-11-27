// WIC codec backend (windowscodecs.dll). The Word rebuild used the same DLL
// approach for imaging; only public Win32/WIC APIs are used here. No cloud or
// AI modules, no Microsoft assets.
#define NOMINMAX
#include <objbase.h>
#include <wincodec.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "../core/codec.h"

namespace pr {
namespace {

bool EncoderClsid(const std::wstring& path, CLSID& clsid) {
    const auto ext = DetectFormat(path);
    switch (ext) {
        case ImageFormat::Png:  clsid = CLSID_WICPngEncoder;  return true;
        case ImageFormat::Jpeg: clsid = CLSID_WICJpegEncoder; return true;
        case ImageFormat::Gif:  clsid = CLSID_WICGifEncoder;  return true;
        case ImageFormat::Tiff: clsid = CLSID_WICTiffEncoder; return true;
        case ImageFormat::Bmp:  clsid = CLSID_WICBmpEncoder;  return true;
        default: return false;
    }
}

}  // namespace

std::unique_ptr<Image> LoadImageWic(const std::wstring& path) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    std::unique_ptr<Image> result;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return nullptr;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, &decoder)))
        goto cleanup;
    if (FAILED(decoder->GetFrame(0, &frame))) goto cleanup;
    if (FAILED(factory->CreateFormatConverter(&converter))) goto cleanup;
    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)))
        goto cleanup;

    {
        UINT width = 0;
        UINT height = 0;
        if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0)
            goto cleanup;
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
        const UINT stride = width * 4;
        if (FAILED(converter->CopyPixels(nullptr, stride, stride * height,
                                         reinterpret_cast<BYTE*>(pixels.data()))))
            goto cleanup;
        result = Image::Create(Size{static_cast<int>(width), static_cast<int>(height)});
        for (UINT y = 0; y < height; ++y) {
            for (UINT x = 0; x < width; ++x)
                result->SetPixel(Point{static_cast<int>(x), static_cast<int>(y)},
                                 Color{pixels[static_cast<std::size_t>(y) * width + x]});
        }
    }

cleanup:
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return result;
}

bool SaveImageWic(const Image& image, const std::wstring& path) {
    CLSID encoderClsid;
    if (!EncoderClsid(path, encoderClsid)) return false;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;
    bool ok = false;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return false;
    if (FAILED(factory->CreateStream(&stream))) goto cleanup;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) goto cleanup;
    if (FAILED(factory->CreateEncoder(encoderClsid, nullptr, &encoder))) goto cleanup;
    if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) goto cleanup;
    if (FAILED(encoder->CreateNewFrame(&frame, &properties))) goto cleanup;
    if (FAILED(frame->Initialize(properties))) goto cleanup;
    if (FAILED(frame->SetSize(static_cast<UINT>(image.width()), static_cast<UINT>(image.height()))))
        goto cleanup;
    {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&format))) goto cleanup;
        const UINT stride = static_cast<UINT>(image.width()) * 4;
        if (FAILED(frame->WritePixels(static_cast<UINT>(image.height()), stride,
                                      stride * static_cast<UINT>(image.height()),
                                      reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.Bitmap())))))
            goto cleanup;
    }
    if (FAILED(frame->Commit())) goto cleanup;
    if (FAILED(encoder->Commit())) goto cleanup;
    ok = true;

cleanup:
    if (properties) properties->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    return ok;
}

}  // namespace pr
