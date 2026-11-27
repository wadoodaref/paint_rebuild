# PaintRebuild

A staged, native **C++17 / Win32 rebuild of the Microsoft Paint workflow**, in the same technology family as the sibling `word_rebuild_v2` and `ppt_rebuild`: no cross-platform toolkits, no Microsoft binaries at runtime, and reverse-engineering tools used only as a read-only evidence source during development.

## Status

Built in stages; the planned workflow is now complete (100%). Headless self-test: `pr_selftest.exe`, 103 checks / 0 FAIL. Includes the REA-mapped modules (core image, operations/undo, tools, codecs, layers, recovery), a native Win32 shell with source-derived UI (ribbon, rulers, tool box, colour box, status bar, dialogs, 19 shapes, 9 brushes, canvas context menu), clipboard, text rendering, wallpaper, full screen, thumbnail and GDI print/preview. Cloud, AI, provenance and telemetry modules observed in the original are deliberately **not** implemented; the vendored `reference/` bundle is read-only evidence only.

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
