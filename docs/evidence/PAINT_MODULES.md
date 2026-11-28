# Paint module map from REA (observations, 2026-11-27)

Source: installed `Microsoft.Paint 11.2609.441.0` (this machine). All facts below
are **static observations** from shipped PE exports; behavior is inferred only
where marked. No Microsoft code or assets are copied into this project.

## Artifact identity (verified)
- `mspaint.exe` SHA-256 `4428e7cd7a4486c41ff8e9a497def906b9e2630ef9603f4018416beacd813db7`
  — imports (evidence `ev_75c406af851fbb60ea1be2acb430fce3b6dc4c65db50c4fd0c12863a63f79d89`,
  complete: 79 DLLs, 1284 imports, 44 delay imports).
- Own modules imported by the executable: `BasePaint.dll`, `PaintTools.dll`,
  `ImageProcessing.dll`, `ProjectFile.dll`, `RestartRecovery.dll`, `PGS.dll`,
  `PaintAIManager.dll`, `AIServices.dll`, `PlatformHelpers.dll`.

## Module → responsibility (from exported symbol names)

| Module | Evidence ID | Exports | Observed responsibility |
|---|---|---|---|
| `PaintTools.dll` | `ev_2771fce7cf12cd281d9fcffcc35cc34f63bfac2045011e79832a4ba5606a264e` | 122 | Tool classes and the `CPaintTool` virtual interface |
| `BasePaint.dll` | `ev_4c7ccd8f9c9b806aba168aeefc967eb63904893b4e4b6ff3ec0f9b762aa10a9b` | 197 | Image model, drawing operations, shape factory, attributes, WIC/GDI+ utilities, telemetry |
| `ImageProcessing.dll` | `ev_550f6d592f6eff763bcf27835a82ce7edd3bc8865b16f9e06f4206cece168fe2` | 11 | Codec/loader layer (`LoadFileImage`, `LoadStreamImage`, encoders/decoders, property store) |
| `ProjectFile.dll` | `ev_97afa9f8475eba627c862ac37a4c44409db97660c5d991b60e91364dbb66ac7f` | 29 | Layered project file (`LayerStack`, `ImageLayer`, `Background`, `Save/Load/Backup/Restore`) |
| `RestartRecovery.dll` | `ev_d587df81b2694c880716d75c00d2f571c95996114cd95c901df3f7d9099d048a` | 40 | Autorecovery (`RecoveryHandler`, `RecoveryData`, `RestartHandler`) |
| `PaintUI.winmd` | `ev_a1d7d75bbff2a93c6e9753a8dd7a78d7d74420f06e0fa35981c16654bd908b89` | metadata complete | WinRT UI contract: 588 types, 4185 methods, 2128 properties |

`PaintTools.dll` imports (evidence `ev_f837b3a3379cbc6abe41ea947fe04610f8105cfd997ef5e995f966c0b10a234e`, complete: 19 DLLs / 210 imports) include **gdiplus.dll**, `BasePaint.dll`, `PGS.dll`, `PlatformHelpers.dll`, `ole32.dll` — GDI+ is part of the tool rendering stack.

## Observed tool classes (PaintTools.dll)
`CPaintTool` (base), `AirBrushTool`, `CAutoShapeTool`, `CBezierTool`, `CBrushTool`,
`CCalligraphicTool`, `CLineTool`, `CPaintPolygonTool`, `CPencilTool`, `CRectTool`,
`CShapeTool`, `CSkeletalStrokeTool`, `CStrokeTool`, `EraserTool`, `FloodTool`,
`FreeFormSelectionTool`, `GenerativeFillTool`, `InactiveTool`, `MagicEraserTool`,
`MaskBasedSelectionTool`, `ModifiableTool`, `NewGenerativeFillTool`,
`ObjectSelectTool`, `PickColorTool`, `SelectionTool`, `TextTool`, `ZoomTool`.

## Observed `CPaintTool` virtual surface
`ActivateAsync`, `DeactivateAsync`, `CanBePreviousTool`, `ToggleWithPrevious`,
`Kind`, `Id`, `CursorId`, `GetCursorVisual`, `GetThickness` / `SetThickness`,
`GetThicknessItemList`, `GetToleranceAttributeId`, `GetOpacityAttributeId`,
`StartPreview` / `EndPreview`, `OnStylusDown/Move/Up/InRange/OutOfRange`,
`OnDeltaStarted/Movement/Stopped`, `OnKeyDown`, `OnTab`, `OnCancel`, `OnClear`,
`OnFocus`, `OnResize`, `OnRotate`, `OnSkew`, `OnFlipHorizontal/Vertical`,
`OnInvertColor`, `OnInvertSelection`, `OnSystemEvent`, `AdjustPointsForConstraint`,
`Query(TOOLACTION)`, `IsDrawing`, `IsEditing`, `IsCreatingOrEditing`,
`HasBeenModified`, `SelectAll`, `ReplaceContent`, `RegisterPaintAppServices`.

**Inference:** the workflow is a tool state machine driven by stylus/pointer
events with per-tool attributes (thickness, tolerance, opacity) and preview
commits — not a sequence of modal dialogs. Our `src/tools/tool.h` mirrors this.

## Observed operations (BasePaint.dll)
`CDrawStrokeOperation`, `CDrawLineOperation`, `CDrawBezierOperation`,
`CDrawPolygonOperation`, `CDrawShapeOperation`, `CDrawRectAutoShapeOperation`,
`CDrawAutoShapeOperation`, `CapsuleStrokeOperation`, `EraserStrokeOperation`,
`EraserOperation`, `DrawAirBrushOperation`, `FloodFillOperation`,
`StampStrokeOperation`, `TextToolOperation`, `SelectionOperation`,
`MaskBasedSelectionOperation`, `ObjectSelectOperation`, `GenerativeFillOperation`,
`ModifiableToolOperation`.

**Inference:** operations are the undoable units; tools create operations and
commit them. Our `src/core/operation.h` mirrors this split.

## Observed `Image` API (BasePaint.dll)
`Create`, `Bitmap`, `Graphics`, `HBitmap`, `Hdc`, `D2DBitmap`, `WicBitmap`,
`Size`, `Clear`, `Crop(RECT, ...)`, `Resize(SIZE, ResizeMode, ResizeDirection)`,
`Rotate(double)`, `HorizontalFlip`, `VerticalFlip`, `HorizontalSkew(int)`,
`VerticalSkew(int)`, `InvertColors`, `HasTransparentPixels`,
`CommitD2DBitmapChanges`, `InvalidateD2DBitmap`.

## Observed shape factory (BasePaint.dll)
`CreateRectangle`, `CreateRoundedRectangle`, `CreateEllipse`, `CreateTriangle`,
`CreateDiamond`, `CreatePentagon`, `CreateHexagon`, `CreateFivePointedStar`,
`CreateFourPointedStar`, `CreateSixPointedStar`, `CreateHeart`, `CreateLightning`,
`CreateLeftArrow`, `CreateRightArrow`, `CreateDownArrow`, `CreateUpArrow`,
`CreateCloudCallout`, `CreateOvalCallout`, `CreateRoundedRectangularCallout`.

## Observed command surface (telemetry names, BasePaint.dll)
`TraceFileNew`, `TraceFileOpen`, `TraceFileSave`, `TraceFileSaveAs`,
`TracePrint`, `TracePrintPreview`, `TracePrintSetup`, `TraceProperties`,
`TraceRedo`, `TraceDelete`, `TraceCrop`, `TraceFlip`, `TraceGrid`,
`TraceRulers`, `TraceThumbnail`, `TraceFullScreen`, `TraceInvertColor`,
`TraceImageTransparent`, `TraceSetWallpaper`, `TraceColorChanged`.

## Utilities observed
`NormalizeAngle`, `ToDegree`, `ToRadian`, `RotatePoint`, `IsValidSkewValue`,
`ComputeRotatedDestinationSizeAndPoints`, `DIBNumColors`, `PaletteSize`,
`IsWin30Dib`, WIC/GDI+ conversions (`CreateWicBitmap`, `CreateHBitmap`, `ToColor`,
`WithAlpha`).

## PaintUI.winmd type inventory (REA CLI, 2026-11-27)
The MCP response could not carry the complete winmd member inventory (10 MiB
transport budget), so the REA CLI was used on the **vendored** copy:
`rea inspect-managed-members reference\paint-11.2609.441.0\PaintUI.winmd
--detail summary --evidence-bundle <path>`. Parent evidence
`ev_87e30a63adcf9e19b82eef55126af169a685bcb6270df94f16b4b6f1c77c35a5`
(complete: 588 TypeDefs, 4185 MethodDefs, 2128 properties, MVID
`9f3e7b49-189c-4939-8e08-1371ae93e3b0`). Observations relevant to our shell:
- Command/UI surfaces: `PaintCommand`, `PaintCommandOrigin`, `CanvasAction`,
  `CanvasActionsViewModel`, `CanvasContextMenuViewModel`, `CommandKeyboardAccelerator`.
- Toolbars/tool state: `ToolsToolbar`, `ToolsViewModel`, `ToolKind`,
  `ColorsToolbar`, `ColorsViewModel`, `ShapesToolbar`, `SelectionToolbar`,
  `TextToolbar`, `BrushSize`, `BrushSizeSlider`, `ZoomSlider`, `ZoomMode`,
  `Ruler`, `StatusBar`, `StatusBarViewModel`, `Canvas`, `CanvasThumbnail`.
- Dialogs: `EditColorsDialog`, `ResizeAndSkewDialog`, `RotateCustomDialog`,
  `ContentDialogService`, `MessageBoxDialog`.
**Inference:** the command set and toolbar/dialog layout we implemented matches
the reference UI categories. Names were read from metadata only; no CIL body
was executed and no command ID values are claimed. Cloud/AI types present in
the metadata (e.g. `GenerativeFillCommandBar`) are observation-only and remain
excluded from our implementation.

## UI labels / tool set from the shipped resources (2026-11-27)
- `inspect_pe_resources` on vendored `PaintUI.dll`:
  `ev_6a1ae6c4c6f3937f5a3e25053d4e00d98f00437c7341a8cde4f339ea145eecaf`
  (2 resources; no icon groups — UI definitions live in `PaintUI.pri`).
- Read-only string inventory of the installed `PaintUI.pri` gave the source's
  tool/shape names: `PencilTool`, `AirBrush`, `CalligraphyPen`, `EraserTool`,
  `FillTool`, `ColorPickerTool`, `MagnifierTool`, `TextTool`, `LineTool`,
  `CurveTool`, `PolygonTool`, `RectangleTool`, `OvalTool`, `RoundedRectTool`,
  `RectangularSelectionLarge`, `FreeFormSelectionLarge`, plus 14 auto-shapes
  (`RightTriangleTool`, `DiamondTool`, `PentagonTool`, `HexagonTool`,
  `FivePointStarTool`, `FourPointStarTool`, `SixPointStarTool`, `HeartTool`,
  `LightningTool`, four arrow tools, three callout tools).
**Inference:** our tool labels were aligned to those names (`Oval`,
`Calligraphy pen`, `Color picker`, etc.) and the 19 auto-shapes were added as a
`Shapes` menu. Per the later user request for an exact UI copy, 18 light-theme
tool SVGs from `Assets\theme-light\` are now vendored in
`reference\paint-11.2609.441.0\ui-assets\`, and `tools\rea\make_icons.py`
renders them to `assets\icons\*.png` (24×24, transparent) used by the Win32
tool box and ribbon. No cloud/AI artwork, licence or NOTICE text was copied.

## Explicitly excluded by user instruction (2026-11-27)
No licence or cloud modules are copied or reimplemented. The installed app also
contains `AIServices.dll`, `AIServicesCS.dll`, `AIStore.dll`,
`PaintAIManager.dll`, `c2pa_c.dll`, `Watermarker.dll`, `ProvenanceHelper.dll`,
`Reporting.dll` and generative-fill tools/operations. These were observed only
as names during PE inspection; they are out of scope for PaintRebuild. No
LICENSE/NOTICE file from a sibling project is carried in this repo.

## Unknowns (do not claim without evidence)
- Exact default thickness/tolerance values and palette colors are not in the
  export table; they must come from resources or runtime observation.
- `PaintUI.winmd` **type names** are now inventoried above via the REA CLI;
  exact numeric command/resource IDs and CIL behaviour remain unknown (no
  decompilation or runtime observation was performed).
- Version 6.2410 (MSPaint bridge) module layout not yet compared.

## UI fidelity audit (2026-11-28, current machine `C4-CLINIC`)

**Source availability (observation):** Paint 11 is not installed here; only the
vendored bundle is available, and it lacks the `mspaint.exe` named in its
manifest. The source machine was `DR-WADOOD-HP15` (see `REA_SOURCES.md`). No
UIA tree or screenshot of the reference app is stored in the repo, so claims of
a pixel-exact copy are **not provable** from local evidence.

**New REA observations:**
- `inspect_pe_resources` on vendored `PaintUI.dll`
  (`ev_7dd1b23f4ca6250de15705c35192a3eeda66dd39be01ccf5f51489e5178150ab`,
  complete) found exactly two resources: RCDATA #387 (77,765 bytes) and
  RT_VERSION #1, no icon groups. The RCDATA payload was read once for
  classification and is third-party licence text — not XAML/layout — and is
  deliberately not copied anywhere in this project.
- `inspect_managed_artifact` on `PaintUI.winmd`
  (`ev_d339ea33e8b4c772c7562b9971e657f3fcd6db90ddd98aebbfc7c27912227e56`,
  complete): 588 TypeDefs / 4185 MethodDefs / 2128 properties; MVID
  `9f3e7b49-189c-4939-8e08-1371ae93e3b0`.
- Full type inventory (`ev_faf81e8c40ffa72b15afde9b1aa25548817a9e9ed4b53dfbe7e49fd693165168`)
  and focused UI-type member inspection
  (`ev_ffbc7e541f699b0558b50d31a48024ae87108aa05b337c4e3939326cefa6a9e6`,
  276 selected methods) confirm source UI contracts including
  `SelectionCommandBarViewModel` (Cut/Copy/Paste/SelectAll/ClearSelection),
  `ImageToolbarViewModel` (CopyMerged, ShowResize, Rotate90Right/Left/180,
  RotateCustom, FlipHorizontal/Vertical, InvertColors), outline/fill style
  commands (None/Color/Crayon/Marker/Oil/NaturalPencil/Watercolour) and
  `RotateCustomDialogViewModel.RotationAngle`.

**Observed defects in our UI (screenshot + gdb, 2026-11-28):** icons were blank
because `GetIcon()` looked only in `<exe dir>\assets\icons` while the PNGs are
at the repo root; compact labels were truncated; enabling icons exposed a
4x source overrun in `DrawToolIcon` that crashed the app. All three are fixed
in this session, and the REA-observed Cut/Copy/Paste/Select all/Clear image
commands were added to the Select dropdown.

**Remaining gaps (inference/unknown, not yet implemented):**
- WinUI3/XAML visual fidelity (rounded corners, animations, exact typography)
  is not achievable with the current hand-built GDI shell; no XAML resource is
  present in the vendored bundle.
- Exact palette RGB literals remain unavailable from artifacts (previous
  unknown, unchanged). Session 41 adopted measured Office 16 visual tokens and
  three generic clipboard icons from the sibling rebuilds with owner
  permission (`src/app/ui_theme.h`, `assets/icons/office_*.png`). That is a
  closer Microsoft basis than invented literals, but it is not Paint-verified
  and is recorded as inference, not source evidence.

**Closed in Session 40:** `CopyMerged` is exposed as Edit > Copy merged and
copies the whole image regardless of selection.

**Closed in Session 39:** shape outline/fill brush styles. `ShapeBrushStyle`
and deterministic coordinate-noise rendering implement None/Color/Crayon/
Marker/Oil/NaturalPencil/Watercolour for both outline and interior, exposed in
the Outline/Fill dropdowns. This is a clean-room interpretation of the
metadata command names; the exact CIL rendering algorithms were not inspected.

**Closed in Session 38:** `RotateCustomDialogViewModel.RotationAngle` now has an
equivalent — "Custom rotate..." opens a signed-angle dialog with optional
horizontal/vertical flips and commits one undoable `Image::Rotate` operation.
No CIL body or numeric resource ID was inspected; the dialog is a clean-room
Win32 reconstruction of the metadata-described contract, not a copied XAML UI.
