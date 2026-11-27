# Vendored REA reference bundle — Microsoft Paint 11.2609.441.0

Purpose: so a session on **another PC never has to search for the reference
app again**. These are read-only reverse-engineering evidence copies, kept in
this private repo. They are **never linked into, loaded by, or shipped with**
`paintrebuild.exe`; the C++ source is clean-room and contains no Microsoft code.

**Source (this machine, 2026-11-27):** verified installed AppX package
`Microsoft.Paint_11.2609.441.0_x64__8wekyb3d8bbwe`, directory `PaintApp\`.
The executable was also confirmed via
`HKCU\Software\Microsoft\Windows\CurrentVersion\App Paths\mspaint.exe`.

| File | Bytes | SHA-256 | REA use |
|---|---:|---|---|
| `mspaint.exe` | 4,303,360 | `4428e7cd7a4486c41ff8e9a497def906b9e2630ef9603f4018416beacd813db7` | PE imports: own module map |
| `BasePaint.dll` | 355,328 | `1779dd5cbfa3fdf4bde64b92ce102ecfa0872c683d5b75532ed4a365abf59cf5` | Image API, operations, attributes, shape factory, utilities |
| `PaintTools.dll` | 310,784 | `98044c3e48b05a5efa70376a4d6cb84ee3e4a825e28d8662c6f7d1304abbd81e` | Tool classes + `CPaintTool` virtual surface |
| `ImageProcessing.dll` | 29,696 | `142f6681ce6a90f607b6ff54d0dafaaf08d627a4f11c0a3d274591cb1e039400` | Codec/loader layer |
| `ProjectFile.dll` | 268,800 | `68ebd66bf891ef757d180bc8fbdd50c76beb8bfa5a110f065ad697095ab96f8b` | Layers / project persistence |
| `RestartRecovery.dll` | 57,856 | `3ac939c01682bf54647209621a977276822e0bef04c54283b3b91a2a81c607c8` | Recovery model |
| `PaintUI.dll` | 10,632,192 | `66b18e89c9a2d573872d0a6abf6aa169a2dd285818806e24b6fa102c95370b12` | UI module (future fidelity passes) |
| `PaintUI.winmd` | 302,080 | `07a3b1340bc0e04d0be68aab146eedd1eaaec5ea85eee3660148ef195418376c` | WinRT UI metadata (588 types / 4185 methods) |
| `EnumHelper.dll` | 62,464 | `b1b0a7ef0234f6ea60e2d7bafb41b93c05dc7b6643e34e20139f575fa6ec36d3` | Helper module |
| `PlatformHelpers.dll` | 428,032 | `6aced20d67d8019176248b0f52dd48e54d3e87d3a22ad4012337c97abebbcd14` | Helper module |
| `PGS.dll` | 86,528 | `2635ff56137ab5fd4707453718831a1cabc3acc21db9931fd18fbc678aba4f60` | Geometry/rendering helper |

A machine-readable copy is `manifest.json`. Verify integrity with
`powershell -ExecutionPolicy Bypass -File tools\rea\verify_reference.ps1`.

## Deliberately NOT vendored (user instruction, 2026-11-27)
- **Cloud / AI / provenance modules:** `AIServices.dll`, `AIServicesCS.dll`,
  `AIStore.dll`, `PaintAIManager.dll`, `PaintProvenanceNet.dll`, `c2pa_c.dll`,
  `Watermarker.dll`, `ProvenanceHelper.dll`, `Reporting.dll`, all `.onnxe`
  models and `GenerativeFill*` tooling.
- **Licence / NOTICE files** from this or any sibling project.
- UI assets, shaders (`.cso`), `resources.pri`, AppX manifests/block maps.

## Offline use
`tools\rea\inspect_local_reference.ps1` lists the local paths and the exact
REA/MCP calls to run against them. Findings belong in
`docs/evidence/PAINT_MODULES.md`; keep observations, inferences and unknowns
separate and cite evidence IDs.
