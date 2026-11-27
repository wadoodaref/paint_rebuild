#include "project.h"

#include <cstring>
#include <fstream>
#include <utility>

namespace pr {
namespace {
constexpr char kMagic[8] = {'P', 'R', 'P', 'R', 'O', 'J', '1', 0};

void WriteU32(std::ofstream& file, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        file.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
}
bool ReadU32(std::ifstream& file, std::uint32_t& value) {
    value = 0;
    for (int i = 0; i < 4; ++i) {
        const int byte = file.get();
        if (byte < 0) return false;
        value |= static_cast<std::uint32_t>(byte) << (8 * i);
    }
    return true;
}
}  // namespace

ImageLayer::ImageLayer(std::unique_ptr<Image> bitmap, LayerMetadata metadata)
    : bitmap_(std::move(bitmap)), metadata_(std::move(metadata)) {}

LayerStack::LayerStack(const Size& size) : size_(size) {}

void LayerStack::InsertLayer(std::unique_ptr<Image> bitmap, const std::string& name) {
    LayerMetadata metadata;
    metadata.name = name;
    layers_.emplace_back(std::move(bitmap), std::move(metadata));
}

void LayerStack::Preview(std::unique_ptr<Image> bitmap) { preview_ = std::move(bitmap); }

std::unique_ptr<Image> LayerStack::Flatten() const {
    const Color base = background_.visible ? background_.color : Color{0};
    auto result = Image::Create(size_, base);
    for (const auto& layer : layers_) {
        if (layer.Metadata().visible) result->Blit(layer.Bitmap(), Point{0, 0});
    }
    if (preview_) result->Blit(*preview_, Point{0, 0});
    return result;
}

bool SaveProject(const LayerStack& stack, const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(kMagic, sizeof(kMagic));
    WriteU32(file, static_cast<std::uint32_t>(stack.size().w));
    WriteU32(file, static_cast<std::uint32_t>(stack.size().h));
    WriteU32(file, stack.GetBackground().color.argb);
    file.put(stack.GetBackground().visible ? 1 : 0);
    const auto dpiX = static_cast<std::uint32_t>(stack.DpiX() * 100.0);
    const auto dpiY = static_cast<std::uint32_t>(stack.DpiY() * 100.0);
    WriteU32(file, dpiX);
    WriteU32(file, dpiY);
    WriteU32(file, static_cast<std::uint32_t>(stack.Layers().size()));
    for (const auto& layer : stack.Layers()) {
        const std::string& name = layer.Metadata().name;
        WriteU32(file, static_cast<std::uint32_t>(name.size()));
        file.write(name.data(), static_cast<std::streamsize>(name.size()));
        file.put(layer.Metadata().visible ? 1 : 0);
        for (int y = 0; y < layer.Bitmap().height(); ++y) {
            for (int x = 0; x < layer.Bitmap().width(); ++x) {
                WriteU32(file, layer.Bitmap().GetPixel(Point{x, y}).argb);
            }
        }
    }
    return static_cast<bool>(file);
}

std::unique_ptr<LayerStack> LoadProject(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;
    char magic[8];
    file.read(magic, sizeof(magic));
    if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) return nullptr;

    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::uint32_t color = 0;
    std::uint32_t dpiX = 9600;
    std::uint32_t dpiY = 9600;
    std::uint32_t layerCount = 0;
    if (!ReadU32(file, w) || !ReadU32(file, h) || w == 0 || h == 0 || w > 65536 || h > 65536)
        return nullptr;
    if (!ReadU32(file, color)) return nullptr;
    const int backgroundVisible = file.get();
    if (!ReadU32(file, dpiX) || !ReadU32(file, dpiY) || !ReadU32(file, layerCount)) return nullptr;

    auto stack = std::make_unique<LayerStack>(Size{static_cast<int>(w), static_cast<int>(h)});
    stack->GetBackground().color = Color{color};
    stack->GetBackground().visible = backgroundVisible != 0;
    stack->SetDpi(dpiX / 100.0, dpiY / 100.0);
    for (std::uint32_t i = 0; i < layerCount; ++i) {
        std::uint32_t nameLength = 0;
        if (!ReadU32(file, nameLength) || nameLength > 4096) return nullptr;
        std::string name(nameLength, '\0');
        file.read(name.data(), static_cast<std::streamsize>(nameLength));
        const int visible = file.get();
        auto bitmap = Image::Create(Size{static_cast<int>(w), static_cast<int>(h)});
        for (std::uint32_t p = 0; p < w * h; ++p) {
            std::uint32_t pixel = 0;
            if (!ReadU32(file, pixel)) return nullptr;
            bitmap->Bitmap()[p] = pixel;
        }
        LayerMetadata metadata;
        metadata.name = name;
        metadata.visible = visible != 0;
        stack->Layers().emplace_back(std::move(bitmap), std::move(metadata));
    }
    return stack;
}

}  // namespace pr
