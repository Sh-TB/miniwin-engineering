# MSVC C++ Runtime Base Plan — versions, legality, implementation strategy

**Status:** ACTIVE (ADR-012). **Created:** 2026-10-17. **Driver:** user directive —
(most programs need MSVC C++; solve it across the different versions; the base matters) —
plus the 100-program ladder (`.agent/PROGRAM_LADDER.md`) which tells us exactly
which CRT versions our target programs actually load.

---

## 1. Why this is the base

Every ladder census so far shows the same shape: kernel32 for OS services + one CRT.
Currently MiniWin implements **91 msvcrt.dll imports + 70 kernel32** measured from
source (API_COMPATIBILITY_DATABASE.md), and that subset was enough for UPX, 7-Zip
console, jq, sqlite3. But the ladder's T1–T8 demands at least these families:

| Family | DLLs | Used by (ladder evidence) |
|---|---|---|
| NT-native CRT | `msvcrt.dll` | T1-T3 bulk, T5-T6 old tools (GnuWin32 era) |
| VC7.x | `msvcr70.dll` `msvcr71.dll` | 2002-2004 utilities |
| VC8/9 (SxS) | `msvcr80.dll` `msvcr90.dll` (+`msvcp80/90`) | WinDirStat (MFC8), WinMerge 2.8, xmlstarlet, VLC 1.x |
| VC10-12 | `msvcr100/110/120.dll` (+`msvcp`) | md5deep, xz, SumatraPDF 1.9, MPC-HC 1.3, procmon |
| VC14+ unified | `vcruntime140.dll` + `ucrtbase.dll` + `msvcp140.dll` (+`concrt140`, `vcomp140`) | ffmpeg, magick, openssl 3.x, curl (already proven demand: OPENSRC-CURL-1), Notepad++ modern |

Two structural differences we must model (they are loader work, not just API work):
- **VC8+ manifests**: apps may require SxS activation context (`Microsoft.VC80.CRT`).
  We can short-circuit: map the request directly to our CRT provider, log the manifest parse.
- **VC14 split**: `vcruntime140` (compiler support: EH, RTTI, intrinsic-ish) vs
  `ucrtbase` (the actual C library). Our current 91-import work maps mostly onto
  ucrtbase semantics; `vcruntime140` needs its own export table
  (`__C_specific_handler` lives in vcruntime on VC14+, in msvcrt on older).

## 2. Legality (hard constraint)

MS Learn: *"Redistribution is permitted only for licensed Visual Studio users"* —
therefore **the repo NEVER ships any MSVC runtime DLL**. Three legal acquisition
paths, in preference order (this mirrors what msvc-wine and winetricks already do):

- **A — user-copied**: user takes `msvcr*.dll`/`vcruntime140.dll`/`ucrtbase.dll`
  from their own `C:\Windows\System32` (or SysWOW64) and drops them into
  `~/.miniwin/crt/<version>/`. Zero license exposure for us.
- **B — scripted download-and-extract**: a helper script downloads the official
  VC redist installers from Microsoft's servers (the same URLs winetricks/msvc-wine
  use, user accepts the MS license when running the script) and extracts the DLLs
  with cabextract. We distribute the SCRIPT, never the binaries.
- **C — open-source CRT fallback**: for missing exports on ladder programs,
  implement measure-driven shims in MiniWin itself (our current approach), with
  **MIT-licensed reference code only** where copying is appropriate (wibo).
  Wine/ReactOS implementations (LGPL) = inspiration/design-reading only unless the
  project ever relaunches under LGPL; wibo's optional `encounter/winedll` embed is
  the precedent for *optional external* prebuilt DLLs — if we ever want that, it
  must be a separate non-repo download with the license shipped alongside.

## 3. Implementation strategy (three modes, per-DLL)

- **Mode 1 — measured shims (default, current)**: keep implementing exactly the
  imports real ladder programs demand. Each new import: trace event + implementation
  + a synthetic PE test in `tests/synthetic_pe/` + regression. This is what got
  7-Zip/sqlite3/jq running and it scales predictably.
- **Mode 2 — native-DLL passthrough (new capability, the shortcut)**: MiniWin is a
  PE loader — it can load the *real* `ucrtbase.dll`/`vcruntime140.dll`/`msvcr90.dll`
  from the user-provided dir, resolve the EXE's imports against the real DLL exports
  (using its own export table), and only shim what the DLL itself demands
  (`kernel32` mostly). This single feature closes whole CRT families at once.
  Design notes: needs (a) export-table walk + forwarder chasing (we have the PE
  parser), (b) CRT-init call discipline — the DLL's `DllMain(CRT_ATTACH)` and
  `_initterm` must run in the right order relative to the EXE entry,
  (c) our `map_crt_stream` window must map the *native* DLL's `_iob` instead.
  Sequencing: ucrtbase first (self-contained, no SxS), then vcruntime140,
  then msvcr90/100, then msvcrt (never needed if passthrough works — msvcrt is
  NT-shipped anyway).
- **Mode 3 — version router**: a `crt_resolver` that, given the EXE's import
  table, picks: requested DLL name → Mode 2 if user-provided DLL exists →
  Mode 1 shim set → unresolved trap stubs (existing BUG-032 machinery) with a
  frontier line naming the missing export. All resolver decisions go to the trace.

## 4. Acceptance gates (per CRT family, G4-aligned)

- `CRT-UCRT-1`: a ucrtbase-linked synthetic PE runs 3× exit 0 (shim path).
- `CRT-UCRT-2`: same PE with native ucrtbase passthrough, 3× exit 0.
- `CRT-VCRUNTIME-1`: synthetic PE throwing/catching via vcruntime140 EH path.
- `CRT-MSVCR90-1`: SxS manifest parse + mapped run.
- Ladder tie-in: T1 GnuWin32 wave = msvcrt gate; T4 ffmpeg/magick = ucrt+msvcp gate.

## 5. Sequencing (backlog IDs in Wave-12)

1. `MSVC-001..003` — census scripts: for each ladder binary, dump CRT import
   versions into `artifacts/census/crt-versions.tsv` (machine-readable priority table).
2. `MSVC-004..006` — vcruntime140 export table shim (compiler-support subset:
   `__C_specific_handler`, `__CxxFrameHandler3/4`, `__std_type_info_*`,
   `_except_handler4`, mem/str intrinsics subset).
3. `MSVC-007..009` — ucrtbase naming layer (we implement under msvcrt names;
   add ucrtbase alias exports + `__acrt_*` core set actually demanded).
4. `MSVC-010..012` — native-DLL passthrough prototype for ucrtbase only.
5. `MSVC-013..015` — SxS manifest parse → log + map (no activation contexts).
6. `MSVC-016+` — msvcp140 C++ std subset on demand (ladder T4/T7 drives it).
