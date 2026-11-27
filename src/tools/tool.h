// Tool layer. Mirrors the observed CPaintTool surface in PaintTools.dll
// (REA ev_2771fce7cf12cd28...): OnStylusDown/Move/Up, OnKeyDown, OnCancel,
// OnClear, GetThickness/SetThickness, tolerance/opacity attributes, IsDrawing,
// HasBeenModified, Kind/Id. Tool names follow the exported classes.
#pragma once

#include <memory>

#include "../core/attributes.h"
#include "../core/color.h"
#include "../core/geometry.h"
#include "../core/image.h"
#include "../core/operation.h"

namespace pr {

enum class PaintToolKind {
    Pencil,
    Brush,
    Airbrush,
    Calligraphic,
    Eraser,
    FloodFill,
    PickColor,
    Line,
    Curve,
    Polygon,
    Rectangle,
    Ellipse,
    RoundedRectangle,
    AutoShape,
    Text,
    Selection,
    FreeFormSelection,
    Zoom,
    Inactive
};

struct ToolContext {
    Image& image;
    COperationStock& stock;
    CAttributeStore& attributes;
    Color foreground = palette::Black();
    Color background = palette::White();

    ToolContext(Image& imageRef, COperationStock& stockRef, CAttributeStore& attributesRef,
                Color foregroundColor = palette::Black(), Color backgroundColor = palette::White())
        : image(imageRef), stock(stockRef), attributes(attributesRef),
          foreground(foregroundColor), background(backgroundColor) {}
};

class CPaintTool {
public:
    CPaintTool(ToolContext& context, PaintToolKind kind)
        : context_(context), kind_(kind) {}
    virtual ~CPaintTool() = default;

    virtual PaintToolKind Kind() const { return kind_; }
    int Id() const { return static_cast<int>(kind_); }

    unsigned GetThickness() const { return thickness_; }
    void SetThickness(unsigned value) { thickness_ = value; }
    double GetTolerance() const { return tolerance_; }
    void SetTolerance(double value) { tolerance_ = value; }
    double GetOpacity() const { return opacity_; }
    void SetOpacity(double value) { opacity_ = value; }

    bool IsDrawing() const { return drawing_; }
    bool HasBeenModified() const { return modified_; }
    const Rect& SelectionRect() const { return selection_; }

    virtual void OnStylusDown(const Point& p) = 0;
    virtual void OnStylusMove(const Point& p) = 0;
    virtual void OnStylusUp(const Point& p) = 0;
    virtual void OnKeyDown(unsigned /*virtualKey*/) {}
    virtual void OnCancel();
    virtual void OnClear() {}
    virtual void StartPreview() { previewing_ = true; }
    virtual void EndPreview() { previewing_ = false; }

protected:
    void BeginStroke();                 // capture the original pixels
    void RestoreOriginal();             // for shape previews
    void EndStroke(OperationKind kind); // capture final pixels + commit

    ToolContext& context_;
    PaintToolKind kind_;
    unsigned thickness_ = 1;
    double tolerance_ = 0.0;
    double opacity_ = 1.0;
    bool drawing_ = false;
    bool modified_ = false;
    bool previewing_ = false;
    Point start_{};
    Point last_{};
    Rect selection_{};
    PixelPatch patch_{};
};

// A stamp-based drawing tool (pencil, brush, airbrush, calligraphic, eraser).
class CStrokeTool : public CPaintTool {
public:
    CStrokeTool(ToolContext& context, PaintToolKind kind, bool useBackground,
                bool square = false, bool airbrush = false);
    void OnStylusDown(const Point& p) override;
    void OnStylusMove(const Point& p) override;
    void OnStylusUp(const Point& p) override;

protected:
    bool useBackground_ = false;
    bool square_ = false;
    bool airbrush_ = false;
    void Stamp(const Point& p);
    void DrawSegment(const Point& from, const Point& to);
};

class FloodTool : public CPaintTool {
public:
    explicit FloodTool(ToolContext& context) : CPaintTool(context, PaintToolKind::FloodFill) {}
    void OnStylusDown(const Point& p) override;
    void OnStylusMove(const Point&) override {}
    void OnStylusUp(const Point&) override {}
};

class PickColorTool : public CPaintTool {
public:
    explicit PickColorTool(ToolContext& context) : CPaintTool(context, PaintToolKind::PickColor) {}
    void OnStylusDown(const Point& p) override;
    void OnStylusMove(const Point&) override {}
    void OnStylusUp(const Point&) override {}
};

class CLineTool : public CPaintTool {
public:
    explicit CLineTool(ToolContext& context) : CPaintTool(context, PaintToolKind::Line) {}
    void OnStylusDown(const Point& p) override;
    void OnStylusMove(const Point& p) override;
    void OnStylusUp(const Point& p) override;
};

class CRectTool : public CPaintTool {
public:
    CRectTool(ToolContext& context, PaintToolKind kind, bool filled);
    void OnStylusDown(const Point& p) override;
    void OnStylusMove(const Point& p) override;
    void OnStylusUp(const Point& p) override;

private:
    bool filled_;
    void DrawShape();
};

class SelectionTool : public CPaintTool {
public:
    explicit SelectionTool(ToolContext& context) : CPaintTool(context, PaintToolKind::Selection) {}
    void OnStylusDown(const Point& p) override;
    void OnStylusMove(const Point& p) override;
    void OnStylusUp(const Point& p) override;
};

class ZoomTool : public CPaintTool {
public:
    explicit ZoomTool(ToolContext& context) : CPaintTool(context, PaintToolKind::Zoom) {}
    void OnStylusDown(const Point& p) override;
    void OnStylusMove(const Point&) override {}
    void OnStylusUp(const Point&) override {}
    int ZoomFactor() const { return zoom_; }

private:
    int zoom_ = 1;
};

class TextTool : public CPaintTool {
public:
    explicit TextTool(ToolContext& context) : CPaintTool(context, PaintToolKind::Text) {}
    void OnStylusDown(const Point& p) override;
    void OnStylusMove(const Point&) override {}
    void OnStylusUp(const Point&) override {}
};

std::unique_ptr<CPaintTool> CreatePaintTool(ToolContext& context, PaintToolKind kind);
const char* ToolName(PaintToolKind kind);

}  // namespace pr
