# PaintRebuild

A staged, native **C++17 / Win32 rebuild of the Microsoft Paint workflow**, in the same technology family as the sibling `word_rebuild_v2` and `ppt_rebuild`: no cross-platform toolkits, no Microsoft binaries at runtime, and reverse-engineering tools used only as a read-only evidence source during development.

## Status

Built in stages; the planned workflow is complete. Headless self-test: `pr_selftest.exe`, 117 checks / 0 FAIL. Includes the REA-mapped modules (core image, operations/undo, tools, codecs, layers, recovery) and a native Win32/GDI shell with source-derived command groups, icons, 23-shape gallery, sliders, rulers, status bar and modal dialogs. The shell reproduces Paint 11's workflow categories but is **not** a pixel/control-level WinUI3 copy; the evidence and remaining fidelity gaps are listed in `docs/evidence/PAINT_MODULES.md`. Cloud, AI, provenance and telemetry modules observed in the original are deliberately **not** implemented; the vendored `reference/` bundle is read-only evidence only.

## Layout

| Path | Purpose |
|---|---|
| `src/core/` | Image model, operations, undo/redo and codecs. |
| `src/app/` | Win32 application. |
| `src/tools/`, `tools/rea/` | Paint tools and the evidence helper. |
| `src/tests/` | Self-test sources. |
| `docs/evidence/` | Evidence gathered from the original. |

## Build and run

```powershell
powershell -ExecutionPolicy Bypass -File run.ps1    # detects MSVC or MinGW-w64, builds, runs the self-test, launches
.\run.ps1 -NoBuild                                  # skip compilation
```

State: `HANDOFF_CONTINUATION_PROMPT.md`, plan: `PROJECT_PLAN.md`, log: `chat.md`. The earlier README is `README2.md`.
