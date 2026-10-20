# SWEEP-008: named-stub batch + data-driven gate repair (SW-555..561)

**Date**: 2026-10-20 (session: 60-task elephant run)
**Binary**: minwin_loader @ repo HEAD (post SWEEP-007 b783c44)

## What changed (one-edit batches, each with a single make + single rerun)

### Batch 1 — named-stub infrastructure (SW-555) + 6 gate APIs
- `iat_stub_name_register()`: every unresolved IAT slot records its
  `dll!import` name at patch time; `mw_unresolved_import_stub` decodes its
  own call site (`FF 15 disp32` -> IAT slot) and prints the exact name.
  Batch 4 added `FF 25` (ILT jmp thunk) support (rammap64 /INCREMENTAL).
- `GetVersionExW` (OSVERSIONINFOW, delegates to pinned GetVersionExA shape)
- `RegOpenKeyW` (honest ERROR_FILE_NOT_FOUND=2)
- `GlobalAddAtomW/A`, `GlobalFindAtomW/A`, `GlobalDeleteAtom` (real 0xC000+
  atom table, unique-per-string, findable)
- `ColorRGBToHLS` / `ColorHLSToRGB` (shlwapi HLSMAX=240 math; procexp64
  FUN_14007baf0 palette round-trip)
- `DialogBoxIndirectParamW/A` + `EndDialog`: WM_INITDIALOG then
  WM_COMMAND(IDOK) — the guest's own "I Agree" path; returns the guest's
  EndDialog value (IDOK default)
- `lstrcmpW/lstrcmpiW`, `RegisterWindowMessageW`

### Batch 2 — disassembly-driven (autoruns64 RIP=0x0 root cause)
- Autoruns64 crashed calling NULL through slot 0x1400c7fd8 — a .rdata
  function-pointer var filled by `GetProcAddress(GetModuleHandleW("ntdll"),
  "RtlGetVersion")`. Fixed:
  - `GetModuleHandleW`: known system DLLs -> pseudo-handle 0xdead0001
  - `GetProcAddress`: named native export hook `RtlGetVersion` (real
    ms_abi impl filling OSVERSIONINFOW, STATUS_SUCCESS)
- `RegCreateKeyW`: honest ERROR_ACCESS_DENIED=5 (stub's success-without-
  handle lie sent guests down a NULL-handle path)
- `SetWindowTextW/A` -> TRUE; `GetDlgItem` -> id-tagged pseudo handle;
  `SendMessageW` -> real WNDPROC delivery for the main window;
  `CreateDialogParamW/A` -> WM_INITDIALOG + stable pseudo HWND;
  `GetSystemMetrics` -> real metric table; `ImageList_Create/Destroy/Add/
  AddMasked/GetImageCount` pseudo handles; `SetWindowLongPtrW/A`,
  `SetWindowLongW/A`

### Batch 3 — round-2 crash forensics
- autoruns64 Signal 11 inside libc memset: `RtlGetVersion` hook was reached
  only AFTER the pseudo-range branch returned the generic optional stub,
  leaving the caller's OSVERSIONINFOW size field as stack garbage. Moved
  the named-export hook BEFORE the pseudo-range branch; `RtlGetVersion`
  now fills the struct itself with a clamped size (0<s<=560).
- `mw_GetVersionExA`: same garbage-size clamp (memset bomb guard).
- rammap64 crash at PE 0x14000df5a (`mov r10,[rcx+8]`, RCX=0x10): the
  SetWindowLongPtr/GetWindowLongPtr pair was always-0 — guest stored
  `this` at GWLP_USERDATA and read back 0. Replaced with a REAL per-
  (hwnd,index) store (512 entries, shared by Set/GetWindowLongPtrW/A,
  Set/GetWindowLongW/A, GWLP_WNDPROC sees callable g_wndproc_fwd,
  GWLP_HINSTANCE = 0xdead0001).
- `SetPropW/GetPropW/RemovePropW` share the store.

### Batch 4 — round-3 crash forensics
- rammap64 SIGSEGV inside `mw_GetPropW`: guests pass MAKEINTATOM(atom)
  (< 0x10000) as the property name — `*s` dereferenced 0xc001. Property
  keys now use the atom value directly, no deref.
- Stub call-site decode accepts `FF 25` ILT thunks.

## Gate results (baseline -> after)

| program   | baseline exit | after | named stubs | classification |
|-----------|---------------|-------|-------------|----------------|
| autoruns64| 1 (EULA gate) | **0** | 10 named    | full-process clean exit, UI-init phase reached (GetSysColor x7, LoadString/LoadMenu/LoadAccelerators), message loop ALIVE under MW_GUI_EVENTS=1 (runs until kill) |
| rammap64  | 1 (EULA gate) | **0** | 14+ named   | full-process clean exit with REAL window work: CreateWindowExW x11, ShowWindow x8, GDI chain (CreateCompatibleDC/Bitmap, FillRect, SelectObject x28, DeleteDC x14) |
| procexp64 | 2             | 2     | 1 named     | OPEN: exit decision comes from a resolved API; got past GetSystemMetrics/ImageList cliff (21 -> 1 stub calls to reach exit) |

3x deterministic: autoruns64=0, rammap64=0, procexp64=2 (three consecutive runs each).

## Full-corpus resweep (74-tag list; 16 dirs + 5 re-acquired survive on disk)

Highlights (baseline -> now):
- ns-produkey: 1 -> **0** (RegisterClassA -> ATOM, CreateWindowExA -> HWND,
  full GUI lifecycle, clean exit)
- ns-cports: 1 -> 139 (moved deeper; pump crash cliff documented SWEEP-007)
- putty-gui: dialog BMP rendered (sha f953a2f3...)
- putty-pageant: dialog BMP rendered (sha 3ce3b3a6...)
- diskmon64: exit 0; procmon64: exit 0 (pre-window class, honest label)
- coreinfoex64: 1 (SW-561 version drift, unchanged)
- zoomit64: 133; bginfo64/winmergeu: 9; searchmyfiles/puttygen: 139 (frontier)

Full table: `artifacts/sweep50/runs_stubfix/sweep008_summary.tsv`
(corpus itself was pruned to 16 dirs + SWEEP-007 re-acquisition; the
missing 60 tags are marked MISSING = exe absent on disk, NOT tested.)

## 5 GUI programs — concrete evidence (the user's hard question)

1. **produkey** — exit 0; trace: RegisterClassA -> ATOM 0xc001,
   CreateWindowExA -> HWND 0x10001; full lifecycle.
2. **putty** — dialog rendered to BMP (221,118 bytes, sha256
   f953a2f3ec131b3ab3e34a3d572999ac3c57115ddf3886f896b822e992c0dce8).
3. **pageant** — dialog rendered to BMP (306,774 bytes, sha256
   3ce3b3a6b6ca5e7bb666121d1e8925457d94a288a6e8338d2cb29a4f69d71ffc).
4. **rammap64** — exit 0 with CreateWindowExW x11 + ShowWindow x8 + real
   GDI draw chain (trace evidence, api_trace.json 1258 lines).
5. **autoruns64** — exit 0 full-process; under MW_GUI_EVENTS=1 the message
   loop runs until externally killed (timeout 124) = live pump.

Honest baseline before this sweep: 0 gate programs completed (exits 1/1/2),
no autoruns/rammap exit-0 ever. procexp64 exit 2 remains open (exit
decision inside resolved API path).

## New scripts
- `scripts/sweep008_resweep.sh` — full-corpus re-sweep + delta table
- `scripts/sweep008_delayload_decode.py` — PE delay-load directory decoder
- `scripts/sweep008_slot_owner.py` — IAT slot -> dll!import owner lookup

## Open items
- procexp64 exit(2): origin inside resolved API path (no stub involved).
- rammap64 under MW_GUI_EVENTS=1: crash in pump path (works clean without).
- autoruns64 message loop has no paint path yet (no BMP under pump).
- Corpus re-download for the 60 MISSING tags (rust/go/ssh/putty-console).
