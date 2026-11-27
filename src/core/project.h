// Layered project model, mirroring ProjectFile.dll (REA ev_97afa9f8...):
// LayerStack, ImageLayer, LayerMetadata, Background, DpiX/DpiY, Save/Load.
// The file format here is our own simple binary format (no Paint asset or
// format is copied); cloud/AI project features are out of scope.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "image.h"

namespace pr {

struct LayerMetadata {
    std::string name = "Layer";
    bool visible = true;
};

class ImageLayer {
public:
    ImageLayer(std::unique_ptr<Image> bitmap, LayerMetadata metadata);
    Image& Bitmap() { return *bitmap_; }
    const Image& Bitmap() const { return *bitmap_; }
    LayerMetadata& Metadata() { return metadata_; }
    const LayerMetadata& Metadata() const { return metadata_; }

private:
    std::unique_ptr<Image> bitmap_;
    LayerMetadata metadata_;
};

struct Background {
    Color color = palette::White();
    bool visible = true;
};

class LayerStack {
public:
    explicit LayerStack(const Size& size);

    const Size& size() const { return size_; }
    Background& GetBackground() { return background_; }
    const Background& GetBackground() const { return background_; }
    std::vector<ImageLayer>& Layers() { return layers_; }
    const std::vector<ImageLayer>& Layers() const { return layers_; }

    void InsertLayer(std::unique_ptr<Image> bitmap, const std::string& name);
    void Preview(std::unique_ptr<Image> bitmap);
    Image* Preview() { return preview_.get(); }
    std::unique_ptr<Image> Flatten() const;

    double DpiX() const { return dpiX_; }
    double DpiY() const { return dpiY_; }
    void SetDpi(double x, double y) { dpiX_ = x; dpiY_ = y; }

private:
    Size size_;
    Background background_;
    std::vector<ImageLayer> layers_;
    std::unique_ptr<Image> preview_;
    double dpiX_ = 96.0;
    double dpiY_ = 96.0;
};

bool SaveProject(const LayerStack& stack, const std::string& path);
std::unique_ptr<LayerStack> LoadProject(const std::string& path);

}  // namespace pr
