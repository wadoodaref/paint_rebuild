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
    "line": "LineTool.svg",
    "curve": "CurveTool.svg",
    "polygon": "PolygonTool.svg",
    "rectangle": "RectangleTool.svg",
    "oval": "OvalTool.svg",
    "rounded_rect": "RoundedRectTool.svg",
    "tools": "Tools.svg",
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

    for name, svg in ICONS.items():
        src = source / svg
        if not src.exists():
            print(f"MISSING {src}")
            continue
        shutil.copy2(src, reference / svg)
        png = resvg_py.svg_to_bytes(
            svg_path=str(src), width=args.size, height=args.size,
            background=None, skip_system_fonts=True,
        )
        (output / f"{name}.png").write_bytes(png)
        print(f"OK {name}.png <- {svg}")


if __name__ == "__main__":
    main()
