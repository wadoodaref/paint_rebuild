# REA evidence sources (per-machine; verified 2026-11-27 and 2026-11-28)

Paint is the behavioral reference. Per rules v7 §8, these paths are facts about
**the machine on which they were checked**, not about the repo; a path in any
doc is a hint to verify, not an assumption.

## Verified on 2026-11-27 (source machine `DR-WADOOD-HP15`)
| Artifact | Version (from package name) | Location on `DR-WADOOD-HP15` |
|---|---|---|
| Microsoft Paint (new) | 11.2609.441.0 | `C:\Program Files\WindowsApps\Microsoft.Paint_11.2609.441.0_x64__8wekyb3d8bbwe` |
| Microsoft Paint (MSPaint bridge) | 6.2410.13017.0 | `C:\Program Files\WindowsApps\Microsoft.MSPaint_6.2410.13017.0_x64__8wekyb3d8bbwe` |

Both were confirmed there via `Get-AppxPackage` and a direct directory listing.

## Verified on 2026-11-28 (current machine `C4-CLINIC`)
- Paint 11.2609.441.0 is **not installed**: the recorded AppX directory does
  not exist and a bounded scan of all fixed drives found no `Microsoft.Paint*`
  package directory. Do not assume the 2026-11-27 path is valid here.
- `Microsoft.MSPaint_6.2410.13017.0_x64__8wekyb3d8bbwe` **is** installed.
  A classic `C:\Windows\System32\mspaint.exe` also exists here.
- The vendored `reference\paint-11.2609.441.0\` bundle is therefore the only
  local Paint 11 source. Note that its `manifest.json`/`README.md` list an
  `mspaint.exe` that is **not actually present** in the folder; the PE-import
  evidence for it cannot be re-verified on this machine.
- No UIA capture/control tree or screenshot of Paint 11 is stored in this repo;
  the "UIA-observed" layout notes in `chat.md` are historical assertions, not
  reproducible evidence.

### Offline bundle (added 2026-11-27)
The 11 analyzable core artifacts listed above are now vendored in
`reference/paint-11.2609.441.0/` with `manifest.json` (SHA-256 for every file),
`README.md`, and the offline helpers `tools/rea/verify_reference.ps1` and
`tools/rea/inspect_local_reference.ps1`. A future session on another PC should
use that bundle first and **does not need to search for the installed app**.
Cloud/AI modules, licence/NOTICE files, assets and shaders are deliberately
excluded. These files are read-only evidence, never loaded by our app.

### Resolved candidate search (2026-11-27, this machine)
A deeper all-drive scan and registry check settled where Paint actually lives:
- `HKCU\Software\Microsoft\Windows\CurrentVersion\App Paths\mspaint.exe`
  resolves to the AppX `PaintApp\mspaint.exe` above — this is the installed,
  analyzable reference (SHA-256 `4428e7cd7a4486c4…`).
- `C:\Users\lojain\AppData\Local\Microsoft\WindowsApps\mspaint.exe` exists but
  is a **0-byte reparse point / App Execution Alias** (`Archive, ReparsePoint`);
  it cannot be read or hashed and is not an analyzable artifact.
- Four `mspaint.exe` files in
  `D:\WUDownloadCache\59b763ec8e7291a1\Package_for_RollupFix…\…mspaint_10.0.26100.*\f\`
  are 365 B – 10,249 B update payloads with no version info — patch stubs, not
  full binaries. Per rules v7 §8 these cache/partial copies are excluded as
  reference material; do not substitute them for the installed app.
No Start Menu or Quick Launch Paint shortcut exists here (Store app).

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
