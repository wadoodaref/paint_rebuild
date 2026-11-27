# REA evidence sources (verified on this machine, 2026-11-27)

Paint is the behavioral reference. Per rules v7 §8, these paths are facts about
**this machine**, not about the repo; a path in any doc is a hint to verify, not
an assumption.

## Verified installed artifacts
| Artifact | Version (from package name) | Location (this machine, 2026-11-27) |
|---|---|---|
| Microsoft Paint (new) | 11.2609.441.0 | `C:\Program Files\WindowsApps\Microsoft.Paint_11.2609.441.0_x64__8wekyb3d8bbwe` |
| Microsoft Paint (MSPaint bridge) | 6.2410.13017.0 | `C:\Program Files\WindowsApps\Microsoft.MSPaint_6.2410.13017.0_x64__8wekyb3d8bbwe` |

Both were confirmed via `Get-AppxPackage` and a direct directory listing.
No `mspaint.exe` exists in `C:\Windows\System32` on this machine — do not use
the traditional path here.

## Planned REA reads (map → model → implement → verify)
1. `inspect_pe_imports` on the main Paint executable(s) — which OS imaging and
   UI APIs the reference uses (WIC, GDI+, Direct2D, RichEdit, clipboard).
2. `inspect_pe_resources` / managed metadata on ViewModel DLLs — tool and
   command names, string tables that reveal the command surface.
3. Native decompilation only for small, high-value functions (e.g. tool size /
   tolerance handling); record evidence IDs and mark inferences.

## Observations (with REA evidence IDs)
_To be filled as tools are run. Nothing below is claimed until an evidence ID is
recorded._

## Unknowns
- Which exact tool set is present in each installed version.
- Whether command IDs from the new Paint UI match the classic menu command set.
- Default brush sizes, palette ordering and zoom steps (to be confirmed from
  resources rather than guessed).

## Reuse from sibling rebuilds
- `word_rebuild_v2/build.ps1`: toolchain auto-detection (MSVC `vcvars64` +
  portable MinGW) and one-command `run.ps1`.
- `word_rebuild_v2/docs/engine/evidence/`: evidence-file discipline
  (observation / inference / unknown, evidence IDs).
- No Word DLL or decompiled Word code is used by this project.
