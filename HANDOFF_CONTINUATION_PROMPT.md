# PaintRebuild — Handoff / Continuation Prompt

**Repo:** `wadoodaref/paint_rebuild` (branch `main`, private)
**Nature:** Native Win32/C++17 faithful rebuild of the Microsoft Paint workflow.
**Rules in force:** `general_rules_read_first_v7.md` (append-only `chat.md`,
one coherent change = one commit + one push, `git fetch` before work and before
every push, regenerate per-machine `GITIGNORED_FILES_<machine>.md` on every
push).

## Current state (2026-11-27)
- Stages 0–5 DONE and verified: scaffold, rules v7, REA evidence map, core
  `Image`/attributes/geometry, operation stock with undo/redo, the
  `CPaintTool`-style tool set, a native BMP codec, and layered project +
  clipboard. `pr_selftest.exe`: 93 checks, 0 FAIL.
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
powershell -ExecutionPolicy Bypass -File build.ps1
build\pr_selftest.exe
powershell -ExecutionPolicy Bypass -File run.ps1
```

## Next up
- Stage 1: implement `src/core/` geometry/color/surface and expand the selftest.
