# PaintRebuild

A staged, native **C++17 / Win32 rebuild of the Microsoft Paint workflow**, in the same technology family as the sibling `word_rebuild_v2` and `ppt_rebuild`: no cross-platform toolkits, no Microsoft binaries at runtime, and reverse-engineering tools used only as a read-only evidence source during development.

## Status

Built in stages with a headless self-test (`pr_selftest.exe`, 93 checks at stage 5). Done: scaffold and rules, evidence map, core image, attributes and geometry, an operation stock with undo and redo, the Paint-style tool set, a native BMP codec, layered project files and clipboard support; later stages added fixes such as Select All and a locator tool for the evidence. Cloud, AI, provenance and telemetry modules observed in the original are deliberately **not** implemented.

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
