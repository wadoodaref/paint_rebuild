# PaintRebuild — Handoff / Continuation Prompt

**Repo:** `wadoodaref/paint_rebuild` (branch `main`, private)
**Nature:** Native Win32/C++17 faithful rebuild of the Microsoft Paint workflow.
**Rules in force:** `general_rules_read_first_v7.md` (append-only `chat.md`,
one coherent change = one commit + one push, `git fetch` before work and before
every push, regenerate per-machine `GITIGNORED_FILES_<machine>.md` on every
push).

## Current state (2026-11-28)
- **Machine note:** this PC (`C4-CLINIC`) has no Paint 11 install and no MSVC;
  the portable MinGW-w64 toolchain is used, the app is built by `build.ps1`
  (which stages `assets\icons` beside the exe). The vendored `reference/`
  bundle is the only Paint 11 source here; its `mspaint.exe` is absent.
- **UI fidelity (audited 2026-11-28):** the shell is a hand-built Win32/GDI
  approximation of Paint 11's workflow, *not* a pixel/control-level copy. REA
  evidence for the source UI contract is in `docs/evidence/PAINT_MODULES.md`
  (UI fidelity audit section). Icons now load and labels are not truncated;
  remaining gaps: WinUI3 visuals and exact palette RGBs (Office 16 tokens and
  clipboard icons from the sibling rebuilds were reused in Session 41 with
  owner permission). Custom rotate accepts
  an arbitrary angle (Session 38) and shape outline/fill brush styles are
  implemented (Session 39).
- Stages 0–8 DONE and verified, plus UI & module fidelity passes: scaffold,
  rules v7, REA evidence map, core `Image`/attributes/geometry, 20 auto-shapes
  (including `RightTriangle`), operation stock with undo/redo, full
  `CPaintTool` set (`CBezierTool` 3-step Curve, `CPaintPolygonTool`,
  `FreeFormSelectionTool`, 9 differentiated brush presets with opacity
  blending), native BMP + WIC codecs, layered project + system `CF_DIB`
  clipboard, Paint 11 Win32 shell (dynamic command bar, 8×3 shape gallery,
  selectable Color 1/2, responsive draggable Size/Opacity/Zoom sliders,
  margin rulers, centered/pannable canvas), modal `Segoe UI` dialogs, and
  autorecovery. `pr_selftest.exe`: 117 checks, 0 FAIL.
- Explicitly out of scope by user instruction: copied licence/NOTICE files and
  cloud/AI/provenance/telemetry modules (observed in REA, never implemented).
- MSVC 14.44 auto-detected from
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\...\vcvars64.bat`;
  portable MinGW-w64 staged at `G:\PROJECTS\.toolchain\mingw64` as fallback.
- Remote `wadoodaref/paint_rebuild` created (private).

## Verified local facts (this machine, 2026-11-27)
- Paint reference apps installed (read-only REA evidence sources):
  - `Microsoft.Paint_11.2609.441.0_x64__8wekyb3d8bbwe` (new Paint)
  - `Microsoft.MSPaint_6.2410.13017.0_x64__8wekyb3d8bbwe` (classic-style Paint)
  under `C:\Program Files\WindowsApps\...`
- GitHub credential stored in Windows Credential Manager (`git:https://github.com`);
  do not hardcode it.
- No MSVC or MinGW detected on PATH at project start; `build.ps1` auto-detects
  both and accepts `PR_MINGW_GXX` / `PR_VCVARS` overrides.

## Build & verify
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1   # builds selftest + app
build\pr_selftest.exe                                # 93 checks, 0 FAIL
build\paintrebuild.exe                              # GUI smoke verified
powershell -ExecutionPolicy Bypass -File run.ps1
```

## Final state / remaining backlog
- Stages 0–6 complete and pushed; stage 8 verification complete (see
  `PROJECT_PLAN.md`). All 8 documented stages have a committed article in
  `chat.md`.
- Done in the latest stage: system clipboard `CF_DIB` bridge and real modal
  Attributes / Stretch-Skew / Flip-Rotate / Edit Colors dialogs.
- Autorecovery is now implemented (60s snapshot, startup recovery prompt,
  cleanup on save/close) — the `RestartRecovery.dll` analog is complete.
- All 8 planned stages are complete; text now renders into the image via an
  in-place Edit control + GDI DIB commit.
- The `PaintUI.winmd` type list was inventoried via the REA CLI on the vendored
  bundle (588 types; evidence `ev_87e30a63...`). UI labels, 19 auto-shapes, nine
  brush presets, ribbon, rulers, status-bar controls, wallpaper, full screen,
  thumbnail, print/preview, exact palette and canvas context menu are all
  implemented. Planned workflow is complete (100%); only evidence-only details
  remain: palette hex literals are not present in the artifacts (documented
  inference) and numeric command IDs/CIL behaviour were not inspected.
- **Offline reference bundle** (no search needed on another PC):
  `reference/paint-11.2609.441.0/` — 11 verified core artifacts + `manifest.json`.
  Verify with `tools\rea\verify_reference.ps1`, list REA calls with
  `tools\rea\inspect_local_reference.ps1`. Cloud/AI modules, licence files and
  assets are deliberately excluded; these files are read-only evidence only.
- REA locator for this machine: `tools\rea\locate_paint.ps1 -Json`.
