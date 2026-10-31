# SWEEP-096 — SW-1474/SW-1475: riched known-DLL plane + mode-aware resource layer (PE32 guests regain resources)

Round type: engineering train. Target: the Notepad++ installer chain that
SWEEP-095 left at "clean DLL names, unresolved RichEd20, dialog with zero
controls, undecoded crash".

## SW-1474 — riched20/riched32 in the known-DLL plane

NSIS unicode installers late-load `RichEd20.dll`/`RichEd32.dll`
(LOAD_WITH_ALTERED_SEARCH_PATH) purely so DllMain registers the
RichEdit20W/50W window classes — the real DLLs export nothing public. Both
names are now in `g_known_dlls[]` (pseudo handles, GetProcAddress resolves
against the dispatch table, honest NULL for unknown names). The RichEdit
child class itself already exists (SWEEP-021 SW-906..910, prefix
"richedit" 0x8C). Result: the NPP installer's RichEd20 load succeeds and the
ERROR 126 retry loop is gone.

## SW-1475 — the resource layer now serves BOTH worlds (root cause)

`w3_res_find`, `mw_LoadStringA/W` and `mw_dlg_from_resource` all gated on
`g_image_base` — the x64-native mmap base. For PE32/cpu32 guests that global
is never set (the interpreter world tracks `g_image_base32`), so **every
PE32 guest lived in a resources-free world**: FindResource always failed
(ERROR 1814), LoadString always returned 0, and DialogBoxParamW resolved
zero children.

Fix: the gate and the default PE base now go through the active-world helper
`mw_main_image_base()` (SW-1332): `g_image_base` for x64-native,
`g_image_base32` for the x86 interpreter (the image is identity-mapped, so
the existing PE-header walk works unmodified). Touchpoints: `w3_res_find`
entry gate, `mw_res_pe_base` default base, `mw_LoadStringA`, `mw_LoadStringW`,
`mw_dlg_from_resource`. The x64 unwind-info gates (`scan_prolog*`) are
native-only machinery and intentionally untouched. x64 behavior is
bit-identical by construction (same base returned).

## Measured results (NPP installer 8.9.8.1, full battery env)

| Stage | before SWEEP-096 | after |
|---|---|---|
| RichEd20/RichEd32 load | ERROR 126 retry loop | success (pseudo handle), no retry |
| DialogBoxParamW(105) template | unresolved (no children) | resolved: DLGTEMPLATEEX cdit=14, size 512 @ data 0x46a108 |
| Dialog children | 0 | 14/14 created (id=3/1/2 BUTTON, id=1018/1044/1035/1036/1045 STATIC, ...) |
| GetDlgItem(id=1/2) | NULL | HWND 0x30002 / 0x30003 |
| crash | rc=139 @ 139,972,897 steps | rc=139 @ 139,972,856 steps (unchanged path) |

The remaining crash decoded (was "undecoded"): the guest faults at EIP
0x406740 in the NSIS exehead string resolver — pattern
`mov eax,[esp+8]; test eax,eax; jge +; mov ecx,[0x428a3c]; lea
eax,[eax*4+4]; sub ecx,eax; mov eax,[ecx]` — the NEGATIVE string-index
branch (stack-string path) reading the global at `0x428a3c` (NSIS
exehead `g_stacktop` slot), which is 0 at that point, then dereferencing
the derived pointer. The path is identical before and after SW-1475
(pre-existing, independent of the dialog fix). Decode of WHY g_stacktop is
0 at first-page time is the queued entry point for the next train.

## Gates

- G1 pin: `evidence/master/g1/20261028_054121` — binary sha
  `bb0ac7e17a2e1f41...` (4,652,256 B), regression 12/12, no crash.
- Corpus gate (`scripts/corpus_gate.sh sweep096`): 17 rows, rc=0 in 12,
  crash total 0 — same shape as SWEEP-094/095.
- Battery (SWEEP=sweep096, 3 runs each): idman rc=0, npp64 rc=0, winrar
  rc=0, winrarinstd alive-at-timeout 124 crash=0, resourcesextract rc=0,
  7zsetup rc=86 (own decision), everything/tcpview64/produkey
  alive-at-timeout 124 crash=0, nppsetup rc=139 (decoded frontier above).
  Zero unexpected outcomes. (One mid-round procedure note: an interrupted
  gate run briefly removed `minwin_loader` via `make clean`; three plugins
  recorded runner-level rc=127 during that window and were re-run clean
  after the gate finished — the 127s were env contamination, not guest
  behavior.)
- `repo_size_guard.sh`: GUARD OK (2 documented re-pin WARNs unchanged).

## Next trains (queued, not implemented)

- NSIS exehead: why `g_stacktop` (0x428a3c) is 0 when the license page's
  WM_INITDIALOG handler resolves a negative string index (stack-string
  path) — first NSIS-exec decode target.
- Main-window content paint (children round-robin) for NPP/WinRAR.
- `Program Filesry` destination-path join decode (SWEEP-095 observation).
- ReadEventLogA honest empty-log contract; CW_USEDEFAULT positioning.
