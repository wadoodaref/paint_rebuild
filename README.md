# PaintRebuild

A staged, native C++17 / Win32 rebuild of the **Microsoft Paint workflow**.
Same-technology-family approach as the sibling `word_rebuild_v2` project: no
cross-platform toolkits, no Microsoft binaries at runtime, REA/MCP used only as
a read-only evidence source during development.

## Run (canonical)
```powershell
powershell -ExecutionPolicy Bypass -File run.ps1
```
`run.ps1` auto-detects MSVC or portable MinGW-w64, builds, runs the headless
selftest and (from Stage 6) launches the app. `-NoBuild` skips compilation.

## Layout
```
src/core/     document model: color, geometry, surface, history, raster ops, codecs
src/tools/    tool state machine (pencil, brush, fill, shapes, select, ...)
src/app/      Win32 shell: window, tool box, color box, canvas, menus, dialogs
src/tests/    headless selftest (no UI required)
docs/evidence/ REA/MCP observations and unknowns
tools/rea/    helper scripts for REA inspection of the installed Paint apps
```

## Status
See `PROJECT_PLAN.md` for stages and `HANDOFF_CONTINUATION_PROMPT.md` for the
authoritative current state. Work is appended to `chat.md`, one commit+push per
stage.
