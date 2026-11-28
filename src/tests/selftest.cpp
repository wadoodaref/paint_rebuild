// Headless self-test harness. No UI, no Paint dependency, no external libs.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "../core/attributes.h"
#include "../core/codec.h"
#include "../core/clipboard.h"
#include "../core/project.h"
#include "../core/shapes.h"
#include "../core/color.h"
#include "../core/event.h"
#include "../core/geometry.h"
#include "../core/image.h"
#include "../core/operation.h"
#include "../tools/tool.h"

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(expr)) {                                                         \
            ++g_fails;                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);        \
        }                                                                      \
    } while (0)

using namespace pr;

static void TestGeometry() {
    const Rect r = NormalizeRect(Rect{10, 10, -4, -3});
    CHECK(r.x == 6 && r.y == 7 && r.w == 4 && r.h == 3);
    CHECK(Contains(r, Point{6, 7}));
    CHECK(!Contains(r, Point{10, 10}));
    CHECK(Intersects(r, Rect{7, 8, 2, 2}));
    const Rect i = Intersect(r, Rect{7, 8, 2, 2});
    CHECK(i.x == 7 && i.y == 8 && i.w == 2 && i.h == 2);
    CHECK(Empty(Rect{0, 0, 0, 5}));
}

static void TestColor() {
    const Color red = MakeColor(200, 100, 50, 255);
    CHECK(Red(red) == 200 && Green(red) == 100 && Blue(red) == 50);
    const Color inv = Invert(red);
    CHECK(Red(inv) == 55 && Green(inv) == 155 && Blue(inv) == 205 && Alpha(inv) == 255);
    CHECK(Alpha(WithAlpha(red, 32)) == 32);
    CHECK(palette::BasicColors[0].argb == MakeColor(0, 0, 0).argb);
    CHECK(palette::BasicColors[17].argb == MakeColor(255, 255, 0).argb);
    CHECK(palette::BasicColors[27].argb == MakeColor(255, 128, 64).argb);
}

static void TestAttributes() {
    CAttributeStore store;
    store.Add(1, 12);                 // thickness (REA: GetThickness/SetThickness)
    store.Add(2, 45.0);               // tolerance
    store.Add(3, std::string("brush"));
    int thickness = 0;
    double tolerance = 0.0;
    std::string tool;
    CHECK(store.GetValue(1, thickness) && thickness == 12);
    CHECK(store.GetValue(2, tolerance) && tolerance == 45.0);
    CHECK(store.GetValue(3, tool) && tool == "brush");
    CHECK(store.SetValue(1, 7) && store.GetValue(1, thickness) && thickness == 7);
    CHECK(!store.GetValue(2, thickness));  // wrong type must be rejected
}

static void TestImage() {
    auto image = Image::Create(Size{4, 3});
    CHECK(image->width() == 4 && image->height() == 3);
    CHECK(image->GetPixel(Point{0, 0}).argb == MakeColor(255, 255, 255).argb);

    image->SetPixel(Point{1, 1}, palette::Red());
    CHECK(image->GetPixel(Point{1, 1}).argb == palette::Red().argb);
    image->InvertColors();
    CHECK(image->GetPixel(Point{1, 1}).argb == MakeColor(0, 255, 255).argb);

    image->Clear(MakeColor(0, 0, 0));
    image->SetPixel(Point{0, 0}, palette::Red());
    image->HorizontalFlip();
    CHECK(image->GetPixel(Point{3, 0}).argb == palette::Red().argb);

    image->VerticalFlip();
    CHECK(image->GetPixel(Point{3, 2}).argb == palette::Red().argb);

    CHECK(!image->HasTransparentPixels());
    image->SetPixel(Point{0, 0}, Color{0});
    CHECK(image->HasTransparentPixels());

    auto tile = Image::Create(Size{2, 2}, palette::Blue());
    image->Blit(*tile, Point{1, 1});
    CHECK(image->GetPixel(Point{1, 1}).argb == palette::Blue().argb);

    CHECK(image->Crop(Rect{1, 1, 2, 2}));
    CHECK(image->size().w == 2 && image->size().h == 2);

    CHECK(image->Resize(Size{4, 4}, ResizeMode::Nearest, ResizeDirection::Both));
    CHECK(image->size().w == 4 && image->size().h == 4);

    const auto geometry = ComputeRotatedDestinationSizeAndPoints(Size{4, 3}, ToRadian(90.0));
    CHECK(geometry.first.w == 3 && geometry.first.h == 4);
    CHECK(geometry.second.size() == 4);

    auto square = Image::Create(Size{4, 4});
    square->SetPixel(Point{0, 0}, palette::Red());
    CHECK(square->Rotate(ToRadian(90.0)));
    CHECK(square->size().w == 4 && square->size().h == 4);

    CHECK(IsValidSkewValue(10) && !IsValidSkewValue(90));
    auto skew = Image::Create(Size{4, 4});
    CHECK(skew->HorizontalSkew(10) && skew->VerticalSkew(-10));

    CHECK(NormalizeAngle(-ToRadian(90.0)) > 4.7 && NormalizeAngle(-ToRadian(90.0)) < 4.8);
}

static void TestOperations() {
    auto image = Image::Create(Size{3, 3});
    COperationStock stock;

    stock.Commit(MakeFillRectOperation(*image, Rect{1, 1, 1, 1}, palette::Red()));
    CHECK(image->GetPixel(Point{1, 1}).argb == palette::Red().argb);
    CHECK(stock.CanUndo() && !stock.CanRedo() && stock.IsDirty());

    stock.MarkSaved();
    CHECK(!stock.IsDirty());
    stock.Commit(MakeSetPixelOperation(*image, Point{2, 2}, palette::Blue()));
    CHECK(stock.IsDirty());

    CHECK(stock.Undo(*image));
    CHECK(image->GetPixel(Point{2, 2}).argb != palette::Blue().argb);
    CHECK(stock.CanRedo());
    CHECK(stock.Undo(*image));
    CHECK(image->GetPixel(Point{1, 1}).argb != palette::Red().argb);
    CHECK(!stock.CanUndo());

    CHECK(stock.Redo(*image));
    CHECK(image->GetPixel(Point{1, 1}).argb == palette::Red().argb);
    CHECK(stock.Redo(*image));
    CHECK(image->GetPixel(Point{2, 2}).argb == palette::Blue().argb);
    CHECK(!stock.CanRedo());

    stock.Commit(MakeClearOperation(*image, palette::White()));
    CHECK(image->GetPixel(Point{1, 1}).argb == palette::White().argb);

    auto before = std::make_unique<Image>(*image);
    image->Crop(Rect{0, 0, 2, 2});
    auto after = std::make_unique<Image>(*image);
    SnapshotOperation crop(OperationKind::Crop, std::move(before), std::move(after));
    crop.Revert(*image);
    CHECK(image->size().w == 3);
    crop.Apply(*image);
    CHECK(image->size().w == 2);
}

static void TestTools() {
    auto image = Image::Create(Size{8, 8});
    COperationStock stock;
    CAttributeStore attributes;
    ToolContext context{*image, stock, attributes, palette::Red(), palette::White()};

    auto pencil = CreatePaintTool(context, PaintToolKind::Pencil);
    CHECK(pencil != nullptr);
    CHECK(pencil->Kind() == PaintToolKind::Pencil);
    pencil->SetThickness(3);
    CHECK(pencil->GetThickness() == 3);
    pencil->OnStylusDown(Point{2, 2});
    CHECK(pencil->IsDrawing());
    pencil->OnStylusMove(Point{5, 2});
    pencil->OnStylusUp(Point{5, 2});
    CHECK(!pencil->IsDrawing() && pencil->HasBeenModified());
    CHECK(image->GetPixel(Point{3, 2}).argb == palette::Red().argb);
    CHECK(stock.CanUndo());
    CHECK(stock.Undo(*image));
    CHECK(image->GetPixel(Point{3, 2}).argb == palette::White().argb);

    context.foreground = palette::Blue();
    auto fill = CreatePaintTool(context, PaintToolKind::FloodFill);
    fill->OnStylusDown(Point{0, 0});
    CHECK(image->GetPixel(Point{7, 7}).argb == palette::Blue().argb);
    CHECK(stock.Undo(*image));
    CHECK(image->GetPixel(Point{7, 7}).argb == palette::White().argb);

    context.foreground = palette::Green();
    context.background = palette::Yellow();
    image->Clear(palette::White());
    auto picker = CreatePaintTool(context, PaintToolKind::PickColor);
    image->SetPixel(Point{1, 1}, palette::Green());
    picker->OnStylusDown(Point{1, 1});
    CHECK(context.foreground.argb == palette::Green().argb);

    auto eraser = CreatePaintTool(context, PaintToolKind::Eraser);
    image->SetPixel(Point{4, 4}, palette::Red());
    eraser->OnStylusDown(Point{4, 4});
    eraser->OnStylusUp(Point{4, 4});
    CHECK(image->GetPixel(Point{4, 4}).argb == palette::Yellow().argb);

    auto rect = CreatePaintTool(context, PaintToolKind::Rectangle);
    image->Clear(palette::White());
    rect->OnStylusDown(Point{1, 1});
    rect->OnStylusMove(Point{6, 5});
    rect->OnStylusUp(Point{6, 5});
    CHECK(image->GetPixel(Point{1, 1}).argb == palette::Green().argb);
    CHECK(image->GetPixel(Point{3, 3}).argb == palette::White().argb);

    auto select = CreatePaintTool(context, PaintToolKind::Selection);
    select->OnStylusDown(Point{6, 6});
    select->OnStylusMove(Point{2, 2});
    select->OnStylusUp(Point{2, 2});
    const Rect r = select->SelectionRect();
    CHECK(r.x == 2 && r.y == 2 && r.w == 4 && r.h == 4);

    auto zoom = CreatePaintTool(context, PaintToolKind::Zoom);
    zoom->OnStylusDown(Point{0, 0});
    zoom->OnStylusDown(Point{0, 0});
    CHECK(dynamic_cast<ZoomTool*>(zoom.get())->ZoomFactor() == 4);

    CHECK(std::string(ToolName(PaintToolKind::FloodFill)) == "Fill with color");
}

static void TestCodec() {
    const std::wstring path = (std::filesystem::temp_directory_path() / L"pr_selftest.bmp").wstring();
    auto image = Image::Create(Size{5, 4});
    image->SetPixel(Point{0, 0}, palette::Red());
    image->SetPixel(Point{4, 3}, palette::Blue());
    image->SetPixel(Point{2, 1}, MakeColor(12, 34, 56, 255));
    CHECK(SaveBmp(*image, path));
    CHECK(DetectFormat(path) == ImageFormat::Bmp);
    auto loaded = LoadBmp(path);
    CHECK(loaded != nullptr);
    if (loaded) {
        CHECK(loaded->size().w == 5 && loaded->size().h == 4);
        CHECK(loaded->GetPixel(Point{0, 0}).argb == palette::Red().argb);
        CHECK(loaded->GetPixel(Point{4, 3}).argb == palette::Blue().argb);
        CHECK(loaded->GetPixel(Point{2, 1}).argb == MakeColor(12, 34, 56, 255).argb);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void TestProjectAndClipboard() {
    LayerStack stack(Size{4, 4});
    stack.GetBackground().color = palette::Red();
    auto base = Image::Create(Size{4, 4}, palette::Blue());
    stack.InsertLayer(std::move(base), "Layer 1");
    auto detail = Image::Create(Size{4, 4}, Color{0});
    detail->SetPixel(Point{2, 2}, palette::Green());
    stack.InsertLayer(std::move(detail), "Layer 2");
    stack.SetDpi(120.0, 96.0);

    auto flat = stack.Flatten();
    CHECK(flat->GetPixel(Point{0, 0}).argb == palette::Blue().argb);
    CHECK(flat->GetPixel(Point{2, 2}).argb == palette::Green().argb);
    CHECK(stack.Layers().size() == 2);

    const std::string path = (std::filesystem::temp_directory_path() / "pr_selftest.prproj").string();
    CHECK(SaveProject(stack, path));
    auto loaded = LoadProject(path);
    CHECK(loaded != nullptr);
    if (loaded) {
        CHECK(loaded->Layers().size() == 2);
        CHECK(loaded->Layers()[0].Metadata().name == "Layer 1");
        CHECK(loaded->DpiX() == 120.0);
        auto second = loaded->Flatten();
        CHECK(second->GetPixel(Point{2, 2}).argb == palette::Green().argb);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);

    auto canvas = Image::Create(Size{5, 5});
    canvas->FillRect(Rect{1, 1, 2, 2}, palette::Red());
    SelectionClipboard clipboard;
    clipboard.Copy(*canvas, Rect{1, 1, 2, 2});
    CHECK(clipboard.HasContent());
    CHECK(clipboard.Content().width() == 2 && clipboard.Content().height() == 2);
    clipboard.Cut(*canvas, Rect{1, 1, 2, 2}, palette::White());
    CHECK(canvas->GetPixel(Point{1, 1}).argb == palette::White().argb);
    CHECK(clipboard.Paste(*canvas, Point{3, 3}));
    CHECK(canvas->GetPixel(Point{3, 3}).argb == palette::Red().argb);
}

static void TestShapes() {
    const auto diamond = ShapePoints(ShapeKind::Diamond, Rect{0, 0, 10, 10});
    CHECK(diamond.size() == 4);
    CHECK(diamond[0].x == 5 && diamond[0].y == 0);
    CHECK(std::string(ShapeName(ShapeKind::Heart)) == "Heart");
    CHECK(ShapePoints(ShapeKind::CloudCallout, Rect{0, 0, 10, 10}).size() > 30);

    auto image = Image::Create(Size{24, 24});
    DrawShape(*image, Rect{2, 2, 16, 16}, ShapeKind::Diamond, palette::Red(), true);
    CHECK(image->GetPixel(Point{10, 10}).argb == palette::Red().argb);
    CHECK(image->GetPixel(Point{0, 0}).argb == palette::White().argb);

    auto filled = Image::Create(Size{24, 24});
    DrawShape(*filled, Rect{2, 2, 16, 16}, ShapeKind::Ellipse, palette::Blue(), true);
    CHECK(filled->GetPixel(Point{10, 10}).argb == palette::Blue().argb);

    const auto rtri = ShapePoints(ShapeKind::RightTriangle, Rect{0, 0, 10, 10});
    CHECK(rtri.size() == 3);
    CHECK(rtri[0].x == 0 && rtri[0].y == 0);
}

static void TestFidelityTools() {
    auto img = Image::Create(Size{32, 32});
    COperationStock stock;
    CAttributeStore attrs;
    ToolContext ctx(*img, stock, attrs, palette::Red(), palette::White());

    // Curve (3-step Bezier)
    auto curve = CreatePaintTool(ctx, PaintToolKind::Curve);
    curve->OnStylusDown(Point{2, 16});
    curve->OnStylusMove(Point{28, 16});
    curve->OnStylusUp(Point{28, 16});
    CHECK(curve->IsDrawing());
    curve->OnStylusDown(Point{10, 4});
    curve->OnStylusUp(Point{10, 4});
    CHECK(curve->IsDrawing());
    curve->OnStylusDown(Point{20, 28});
    curve->OnStylusUp(Point{20, 28});
    CHECK(!curve->IsDrawing());
    CHECK(stock.CanUndo());
    CHECK(stock.Undo(*img));

    // Polygon (multi-vertex + close on first vertex)
    auto poly = CreatePaintTool(ctx, PaintToolKind::Polygon);
    poly->OnStylusDown(Point{4, 4});
    poly->OnStylusUp(Point{20, 4});
    poly->OnStylusDown(Point{20, 20});
    poly->OnStylusUp(Point{20, 20});
    poly->OnStylusDown(Point{4, 4});
    CHECK(!poly->IsDrawing());
    CHECK(img->GetPixel(Point{4, 4}).argb == palette::Red().argb);

    // Opacity blending on an untouched white pixel
    auto brush = CreatePaintTool(ctx, PaintToolKind::Brush);
    brush->SetOpacity(0.5);
    brush->OnStylusDown(Point{10, 22});
    brush->OnStylusUp(Point{10, 22});
    const Color blended = img->GetPixel(Point{10, 22});
    CHECK(Red(blended) > 120 && Green(blended) > 100);
}

static void TestShapeBrushStyles() {
    const Rect r{4, 4, 24, 24};
    auto solid = Image::Create(Size{32, 32});
    auto wash = Image::Create(Size{32, 32});
    DrawShape(*solid, r, ShapeKind::Rectangle, palette::Red(), false, 3, palette::White(),
              ShapeBrushStyle::Color);
    DrawShape(*wash, r, ShapeKind::Rectangle, palette::Red(), false, 3, palette::White(),
              ShapeBrushStyle::Watercolor);
    CHECK(solid->GetPixel(Point{10, 4}).argb != wash->GetPixel(Point{10, 4}).argb);

    auto none = Image::Create(Size{32, 32});
    DrawShape(*none, r, ShapeKind::Rectangle, palette::Red(), false, 3, palette::White(),
              ShapeBrushStyle::None);
    CHECK(none->GetPixel(Point{10, 4}).argb == palette::White().argb);

    // Coordinate noise is deterministic: two draws must give identical pixels.
    auto a = Image::Create(Size{32, 32});
    auto b = Image::Create(Size{32, 32});
    for (auto* img : {a.get(), b.get()})
        DrawShape(*img, r, ShapeKind::Ellipse, palette::Blue(), true, 3, palette::Green(),
                  ShapeBrushStyle::Crayon, ShapeBrushStyle::Marker);
    bool identical = true;
    for (int y = 0; y < 32 && identical; ++y)
        for (int x = 0; x < 32; ++x)
            if (a->GetPixel(Point{x, y}).argb != b->GetPixel(Point{x, y}).argb) {
                identical = false;
                break;
            }
    CHECK(identical);

    auto fillSolid = Image::Create(Size{32, 32});
    auto fillMarker = Image::Create(Size{32, 32});
    DrawShape(*fillSolid, r, ShapeKind::Rectangle, palette::Red(), true, 0, palette::Green(),
              ShapeBrushStyle::None, ShapeBrushStyle::Color);
    DrawShape(*fillMarker, r, ShapeKind::Rectangle, palette::Red(), true, 0, palette::Green(),
              ShapeBrushStyle::None, ShapeBrushStyle::Marker);
    CHECK(fillSolid->GetPixel(Point{16, 16}).argb != fillMarker->GetPixel(Point{16, 16}).argb);
}

int main() {
    CHECK(sizeof(std::uint32_t) == 4);  // Stage 0 harness check
    TestGeometry();
    TestColor();
    TestAttributes();
    TestImage();
    TestOperations();
    TestTools();
    TestCodec();
    TestProjectAndClipboard();
    TestShapes();
    TestFidelityTools();
    TestShapeBrushStyles();

    std::printf("pr_selftest: %d checks, %d FAIL\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
