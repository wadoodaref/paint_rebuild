# paint_rebuild â€” append-only session log

Newest entries are appended at the bottom. One coherent change = one article +
one commit + one push. Rules: `general_rules_read_first_v7.md`.

## 2026-11-27 â€” Session 1 â€” Stage 0: scaffold, rules v7, toolchain-aware build

**Request:** create a C++ `paint_rebuild` project faithful to the Microsoft Paint
workflow, built in stages (core modules first, small high-yield first), with a
log + push after every stage; carry rules v7 over from the sibling rebuild
projects and reuse the Word/PowerPoint-rebuild approaches where useful; use
REA/MCP as an evidence source; do not ask questions until finished.

**Decisions / rationale:**
- Location `E:\projects\paint_rebuild` (sibling of `word_rebuild_v1/v2`).
- Remote created on GitHub as `wadoodaref/paint_rebuild` (private, matching
  `word_rebuild_v2`) using the stored GitHub credential on this machine; no
  credential is written into the repo.
- `general_rules_read_first_v7.md` copied verbatim from `LightWord_v4`, the same
  file the sibling rebuild projects carry. `chat.md`, `PROJECT_PLAN.md`,
  `HANDOFF_CONTINUATION_PROMPT.md`, `run.ps1` and `build.ps1` created at project
  start per rules Â§6.
- Build script mirrors the Word-rebuild approach: auto-detect MSVC `vcvars64`
  or a portable MinGW-w64 `g++`, zero-dependency C++17 core, headless selftest
  first, Win32 app added in a later stage.
- Stage 0 deliberately builds only the selftest harness: it is the smallest
  end-to-end verification of the toolchain and the commit/push loop.

**Files:** `.gitignore`, `general_rules_read_first_v7.md`, `chat.md`,
`PROJECT_PLAN.md`, `HANDOFF_CONTINUATION_PROMPT.md`, `interrupted.md`,
`README.md`, `build.ps1`, `run.ps1`, `src/tests/selftest.cpp`,
`docs/evidence/REA_SOURCES.md`.

## 2026-11-27 â€” Session 2 â€” Foundation tranche (Stages 0â€“3): REA evidence + core model + operations + tools

**Request continuation:** "use rea/mcp and be faithful to source workflow and
modules." No questions asked, per instruction.

**REA work (before writing code):** inspected the installed Paint 11.2609.441.0
artifacts with REA/MCP. `inspect_pe_imports` on `mspaint.exe` revealed Paint's
own modules; `inspect_native_exports` on `PaintTools.dll` (122 exports),
`BasePaint.dll` (197), `ImageProcessing.dll` (11), `ProjectFile.dll` (29),
`RestartRecovery.dll` (40) gave the tool classes, `CPaintTool` virtual surface,
`Image` API, shape factory, operation classes, telemetry command names and
layer/recovery models. All observations, evidence IDs and unknowns are in
`docs/evidence/PAINT_MODULES.md`. `PaintUI.winmd` metadata was inspected; its
full type list exceeded the MCP response budget and was left as an explicit
unknown rather than guessed.

**Implementation (fresh C++, same module boundaries):**
- `src/core/geometry.h`, `color.h`, `event.h` â€” POINT/SIZE/RECT analogues, ARGB
  helpers, `IPaintListener`.
- `src/core/image.h/.cpp` â€” `Image` with Crop/Resize/Rotate/Flip/Skew/
  InvertColors/Blit and the observed geometry utilities.
- `src/core/attributes.h` â€” `CAttributeStore` (thickness/tolerance/opacity).
- `src/core/operation.h/.cpp` â€” `PatchOperation`/`SnapshotOperation` and
  `COperationStock` undo/redo with dirty tracking (BasePaint operation split).
- `src/tools/tool.h/.cpp` â€” `CPaintTool` with `OnStylusDown/Move/Up`,
  `OnKeyDown`, `OnCancel`, preview, thickness/tolerance/opacity; concrete
  pencil, brush, airbrush, calligraphic, eraser, flood fill, pick colour, line,
  rect/ellipse, selection, zoom and text tools.
- `build.ps1`/`run.ps1` fixed to build from `build\`; MSVC `vcvars64` auto-
  detected at the x86 BuildTools path (a portable MinGW-w64 was also staged
  as fallback because none was initially on PATH).

**Verification:** `build.ps1` â€” 0 warnings, 0 errors (MSVC 14.44);
`pr_selftest.exe` â€” **72 checks, 0 FAIL** (geometry, colour, attributes, image
transforms, undo/redo, all core tools). REA evidence IDs cited in the docs.

**Commit:** `195b0a2` (pre-amend hash `6f47fa7` was superseded when this
entry's hash was written; the pushed tip is `195b0a2`).

## 2026-11-27 â€” Session 3 â€” Stage 4: BMP codec + explicit no-license/no-cloud rule

**Request continuation:** user instructed "donot copy license or cloud
modules." No licence/NOTICE file from a sibling project was ever copied, and
no cloud/AI/provenance/telemetry module is implemented or linked. This is now
an explicit non-goal in `PROJECT_PLAN.md` and marked in
`docs/evidence/PAINT_MODULES.md`; the observed AI/cloud names remain evidence
only.

**Change:** added `src/core/codec.h/.cpp`, a dependency-free 32-bit BMP writer
and 24/32-bit BI_RGB reader (GDI bottom-up layout, top-down DIB support), plus
`DetectFormat`/`FormatName`. WIC/GDI+ PNG/JPEG is deferred to the app stage.
Added round-trip selftests (pixel-exact save â†’ load â†’ compare).

**Verification:** MSVC build 0 warnings/0 errors; `pr_selftest.exe` â€” **79
checks, 0 FAIL** (72 foundation + 7 codec checks).

**Commit:** 82f28177788a42aded1311dd54989d454e7b811b (`0420cfc`)

## 2026-11-27 — Session 4 — Stage 5: layered project + clipboard

**Change:** `src/core/project.h/.cpp` implements the `ProjectFile.dll` analog —
`LayerStack`, `ImageLayer`, `LayerMetadata`, `Background`, DPI, `Flatten()` and
our own binary `SaveProject`/`LoadProject` format (no Microsoft project format
or asset copied). `src/core/clipboard.h` adds copy/cut/paste on an `Image`
selection; the Win32 stage will map it to `CF_DIB`.

**Verification:** MSVC build 0 warnings/0 errors; `pr_selftest.exe` — **93
checks, 0 FAIL** (14 new project/clipboard checks, including save → load →
flatten pixel comparison and cut/paste).

**Commit:** bb180bf3d4bda12de71d954b488564a4c9e24978 (`aca5be6`)

## 2026-11-27 — Session 5 — Stage 6: Win32 Paint shell + WIC codecs

**Change:** added `src/app/main.cpp` (native Win32 `PaintUI` analog) and
`src/app/codec_wic.cpp` (PNG/JPEG/GIF/TIFF/BMP through public WIC APIs in
`windowscodecs.dll`, same DLL approach as the Word rebuild, no cloud/AI code).
The shell has the classic Paint layout: tool box down the left (18 tools from
the REA list), 28-colour colour box with foreground/background swatches,
GDI canvas with zoom 100–800% and optional grid, status bar with tool name /
cursor coordinates / image size, and File/Edit/View/Image/Help menus with the
observed command set (New/Open/Save/Save As, Undo/Redo, Cut/Copy/Paste, Clear
Image, Select All, Invert, Flip, Rotate 90°, Attributes, About). Mouse events
drive the `CPaintTool` state machine; whole-image commands commit undoable
`SnapshotOperation`s. `build.ps1` now builds both `pr_selftest.exe` and
`paintrebuild.exe`; MSVC links user32/gdi32/comctl32/comdlg32/shell32/ole32/
windowscodecs/uuid. `NOMINMAX` added before Windows headers to keep `std::max`.

**Verification:** MSVC build 0 warnings/0 errors for both targets; selftest
93 checks, 0 FAIL; GUI smoke: process stayed alive with window title
`PaintRebuild - Untitled`, then was stopped. No Microsoft Paint binary or
asset is loaded at runtime.

**Commit:** 47533081f440b73c7676ee0b6dad93e868c8ab69 (`f6926ab`)

## 2026-11-27 — Session 6 — Stage 7/8: fidelity fix, REA locator tool, final verification

**Change:** Select All now drives an actual `SelectionTool` instead of the
currently active drawing tool. Added `tools/rea/locate_paint.ps1`, which
implements rules v7 section 8 as a runnable tool: it enumerates installed Paint
AppX packages, verifies the exact executables, reports artifact SHA-256, and
prints the REA/MCP follow-ups; it warns rather than guessing when nothing is
found. Verified output matches the earlier REA evidence
(`mspaint.exe` SHA-256 `4428e7cd...`, package 11.2609.441.0).

**Final verification:** `run.ps1` rebuilt both targets (0 warnings, 0 errors),
`pr_selftest.exe` 93 checks / 0 FAIL, GUI smoke launched with title
`PaintRebuild - Untitled`. No licence file, no cloud/AI/provenance module and
no Microsoft binary is shipped or loaded by the project.

**Commit:** 052365dbbb0efa18417e640ee372861fb086b006 (`b88f019`)

## 2026-11-27 — Session 7 — Resolve the user's "Paint is on this PC" pointer

**Request:** user pointed at
`C:\Users\lojain\AppData\Local\Microsoft\WindowsApps\mspaint.exe` and later at
the AppX `...\PaintApp\mspaint.exe`. Searched registry App Paths, uninstall keys,
Start Menu / Quick Launch, and a depth-7 all-drive `mspaint.exe` scan.

**Findings (all verified on this machine):**
- The `AppData\Local\Microsoft\WindowsApps\mspaint.exe` path is a **0-byte
  App Execution Alias** (`Archive, ReparsePoint`), unreadable and unhashable —
  not an analyzable artifact.
- Four `mspaint.exe` files in the WU download cache are 365 B – 10 KB update
  payloads without version info — patch stubs, excluded per rules v7 §8.
- `HKCU\...\App Paths\mspaint.exe` resolves to
  `C:\Program Files\WindowsApps\Microsoft.Paint_11.2609.441.0_x64__8wekyb3d8bbwe\PaintApp\mspaint.exe`,
  verified: 4,303,360 bytes, file version **11.2609.441.0**, SHA-256
  `4428e7cd7a4486c41ff8e9a497def906b9e2630ef9603f4018416beacd813db7` — the
  exact artifact already inspected with REA/MCP. No other analyzable Paint
  binary exists here, so the module/tool/workflow evidence is unchanged.

**Change:** recorded these resolved candidates in
`docs/evidence/REA_SOURCES.md` and taught `tools/rea/locate_paint.ps1` to report
the alias as `analyzable=false` (script re-run and verified).

**Commit:** 51fee5b






