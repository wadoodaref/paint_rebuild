// Headless self-test harness. No UI, no Paint dependency, no external libs.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "../core/attributes.h"
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

int main() {
    CHECK(sizeof(std::uint32_t) == 4);  // Stage 0 harness check
    TestGeometry();
    TestColor();
    TestAttributes();
    TestImage();
    TestOperations();
    TestTools();

    std::printf("pr_selftest: %d checks, %d FAIL\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
