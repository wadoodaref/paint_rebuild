"""Reproduce the Paint tool-box icons from the installed source theme.

Usage:
    python tools/rea/make_icons.py [--source DIR] [--size 24]

Resolves the installed Microsoft.Paint AppX asset directory on this machine
(override with --source or PAINT_ASSETS), copies the selected SVG references
into reference/<version>/ui-assets/, and renders matching transparent PNGs into
assets/icons/ with resvg-py. No licence/cloud module or NOTICE text is copied.
"""
import argparse
import glob
import os
import shutil
from pathlib import Path

import resvg_py

ROOT = Path(__file__).resolve().parents[2]

ICONS = {
    # Tools
    "select": "RectangularSelectionLarge.svg",
    "freeform": "FreeFormSelectionLarge.svg",
    "eraser": "EraserTool.svg",
    "fill": "FillTool.svg",
    "picker": "ColorPickerTool.svg",
    "zoom": "MagnifierTool.svg",
    "pencil": "PencilTool.svg",
    "brush": "BrushIcon.svg",
    "airbrush": "AirBrushIcon.svg",
    "calligraphy": "CalligraphyBrushIcon.svg",
    "text": "TextTool.svg",
    # Shapes
    "line": "LineTool.svg",
    "curve": "CurveTool.svg",
    "polygon": "PolygonTool.svg",
    "rectangle": "RectangleTool.svg",
    "oval": "OvalTool.svg",
    "rounded_rect": "RoundedRectTool.svg",
    "triangle": "TriangleTool.svg",
    "right_triangle": "RightTriangleTool.svg",
    "diamond": "DiamondTool.svg",
    "pentagon": "PentagonTool.svg",
    "hexagon": "HexagonTool.svg",
    "five_point_star": "FivePointStarTool.svg",
    "four_point_star": "FourPointStarTool.svg",
    "six_point_star": "SixPointStarTool.svg",
    "right_arrow": "RightArrowTool.svg",
    "left_arrow": "LeftArrowTool.svg",
    "up_arrow": "UpArrowTool.svg",
    "down_arrow": "DownArrowTool.svg",
    "heart": "HeartTool.svg",
    "lightning": "LightningTool.svg",
    "rounded_rect_callout": "RoundedRectCalloutTool.svg",
    "oval_callout": "OvalCalloutTool.svg",
    "cloud_callout": "CloudCalloutTool.svg",
    # Command bar / toolbar
    "tools": "Tools.svg",
    "rotate": "RotateRight.svg",
    "flip": "Flip.svg",
    "resize": "Resize.svg",
    "size": "SizeButtonIcon.svg",
    "shapes": "Shapes.svg",
    "shape_stroke": "ShapesStrokeSize.svg",
    "zoom_in": "ZoomIn.svg",
    "zoom_out": "ZoomOut.svg",
    "fit_window": "OnePage.svg",
    "save": "Save.svg",
    "colors": "Colors.svg",
    # PNG sources
    "shape_fill": "SolidFill.png",
    "shape_outline": "SolidOutline.png",
    "shape_nofill": "NoFillOutline.png",
}


def find_source(explicit):
    if explicit:
        return Path(explicit)
    env = os.environ.get("PAINT_ASSETS")
    if env:
        return Path(env)
    candidates = glob.glob(
        r"C:\Program Files\WindowsApps\Microsoft.Paint_*\Assets\theme-light"
    )
    if not candidates:
        raise SystemExit("Paint asset directory not found; pass --source")
    return Path(sorted(candidates)[-1])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=None)
    parser.add_argument("--size", type=int, default=24)
    args = parser.parse_args()
    source = find_source(args.source)
    version = source.parent.parent.name.split("_", 1)[-1].split("_")[0]
    reference = ROOT / "reference" / f"paint-{version}" / "ui-assets"
    output = ROOT / "assets" / "icons"
    reference.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)

    from PIL import Image as PILImage
    for name, asset in ICONS.items():
        src = source / asset
        if not src.exists():
            print(f"MISSING {src}")
            continue
        shutil.copy2(src, reference / asset)
        if asset.lower().endswith('.svg'):
            png = resvg_py.svg_to_bytes(
                svg_path=str(src), width=args.size, height=args.size,
                background=None, skip_system_fonts=True,
            )
            (output / f"{name}.png").write_bytes(png)
        else:
            img = PILImage.open(src).convert('RGBA')
            img = img.resize((args.size, args.size), PILImage.LANCZOS)
            img.save(output / f"{name}.png")
        print(f"OK {name}.png <- {asset}")


if __name__ == "__main__":
    main()
