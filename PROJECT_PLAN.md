# PROJECT_PLAN.md — PaintRebuild (native Win32 / C++17)

Guiding rule (same family as the sibling Word rebuild): **same technology as the
original** — native Win32 + GDI/GDI+ on Windows, no cross-platform UI toolkit.
Microsoft Paint is the functional/behavioral reference. REA/MCP findings are
recorded in `docs/evidence/` and used as evidence, never as copied code.

## Stages (smallest high-yield unit first; each stage builds, logs, commits, pushes)

| Stage | Target | Acceptance criteria | Status |
|---|---|---|---|
| 0 | Scaffold + rules + toolchain + selftest harness | `build.ps1` runs, `pr_selftest.exe` prints 0 FAIL, `run.ps1` works; push | DONE |
| 1 | Core: `Point/Size/Rect/Color`, `Image` (ARGB pixels, blit/clear/fill, crop/resize/rotate/flip/skew/invert), `CAttributeStore`, `IPaintListener` | headless pixel tests pass; no Win32 UI dependency | DONE |
| 2 | `Operation` stock + undo/redo + dirty tracking (BasePaint operation split) | selftest: N edits, undo/redo restores exact pixels | DONE |
| 3 | `CPaintTool`-style tools: pencil, brush, airbrush, eraser, flood fill, pick colour, line, rect/auto-shape, selection, zoom | selftest verifies pixel geometry and per-tool attributes | DONE |
| 4 | Codecs (`ImageProcessing` analog): native BMP, WIC/GDI+ PNG/JPEG | round-trip selftest (write → read → identical pixels) | BMP DONE; WIC TODO |
| 5 | `ProjectFile` analog: layers + document persistence, selection/clipboard | selftest covers layers, selection and transforms | DONE |
| 6 | Win32 shell (`PaintUI` analog): window, tool box, color box, canvas, status bar, menus | app builds and launches; tools respond to mouse | DONE |
| 7 | Paint dialogs + fidelity pass from REA evidence | Attributes, Edit Colors, Stretch/Skew, Flip/Rotate, Text, Zoom, recovery | TODO |

Stage/module names follow the **observed** shipped module map in
`docs/evidence/PAINT_MODULES.md` (REA evidence IDs included there):
`PaintTools.dll` → tool classes + `CPaintTool`; `BasePaint.dll` → `Image`,
operations, `CAttributeStore`, shape factory, utilities; `ImageProcessing.dll`
→ codec layer; `ProjectFile.dll` → layered project; `RestartRecovery.dll` →
autorecovery. We reimplement these boundaries in fresh C++, not copy them.
| 8 | Final verification: full selftest + smoke script + handoff | zero warnings/errors, all stages pushed | TODO |

## Non-goals (deliberate, user 2026-11-27)
- No copying of Microsoft binaries, icons, strings or source.
- **No copied license/NOTICE files from sibling projects.** The shared rules v7
  file is carried deliberately; nothing else is copied from another repo.
- **No cloud / AI / provenance / telemetry modules.** REA observed
  `AIServices.dll`, `AIStore.dll`, `PaintAIManager.dll`, `AIServicesCS.dll`,
  `c2pa_c.dll`, `Watermarker.dll`, `ProvenanceHelper.dll`, `AIStore` and
  generative-fill/telemetry exports; they remain evidence observations only and
  are **not** reimplemented, linked or copied here.
- No runtime dependency on any installed Paint app; REA artifacts are read-only
  evidence used on the development machine only.
- No cross-platform toolkit.

## REA evidence workflow (user-approved, 2026-11-27)
1. Locate the installed Paint artifact per rules v7 §8 (verify the exact file
   on this machine; record version from the artifact).
2. Use REA/MCP (`inspect_pe_imports`, `inspect_pe_resources`,
   `inspect_managed_artifact`, and native decompilation where useful) to map
   commands, tools and workflow.
3. Write observations (with evidence IDs) in `docs/evidence/`; separate
   observations from inferences and record unknowns.
4. Implement the behavior fresh in C++ and verify with headless tests/app.

## Verification log
- 2026-11-27: Stages 0–3 built with MSVC 14.44: 0 warnings, 0 errors;
  `pr_selftest.exe` 72/72 checks passing; foundation tranche committed+pushed.
- 2026-11-27: Stage 4 BMP round-trip added; 79/79 checks passing, committed and
  pushed.
- 2026-11-27: Stage 5 layered project + clipboard added; 93/93 checks passing,
  committed and pushed.
- 2026-11-27: Stage 6 Win32 shell + WIC codecs added; both targets build with
  0 warnings/0 errors, selftest 93/93, GUI smoke launched successfully.
