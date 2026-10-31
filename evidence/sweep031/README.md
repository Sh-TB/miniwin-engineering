# SWEEP-031 — Notepad family on real Wine source (user mandate: «از وینه کمک بگیر»)

**Date**: 2026-10-30 · **RECONCILED onto origin/main 08f1c5a** (SWEEP-032 a/b/c + SWEEP-107 were pushed concurrently; per repo rule the wagon rebased, upstream kept where it landed the same levers first — SWEEP-035 blocking pump, SW-1265 RT_STRING, APP-TEST-2 RT_MENU, SW-1271 accel translate, SW-1317 accel free, SW-1328 probe NULL-dst fix — and **only the genuinely-new deltas were re-applied**: SW-1203b LoadStringW cch==0 pointer mode (port onto upstream SW-1265), SW-1213 date/time NULL=now, SW-1214 FormatMessageW ALLOCATE_BUFFER + failure-path NUL, SW-1212b PathAddExtensionW) · **G1 regression: 12/12 PASS on the merged engine**

## What the user demanded

1. **Really use Wine** (not just claim it) — «قرار شد از وینه کمک بگیر»
2. **Really load notepad / notepad++ end-to-end** — «چند ده بار نتونستی یدونه برنامه ساده رو لود بکنی حتی نوت پد نوت پلاس»
3. Keep the train moving (commit + push).

## What was actually done (evidence-first)

### A. Wine as a REAL reference (downloaded, compiled, executed)

| Item | Detail |
|---|---|
| Wine source | `wine-9.0.tar.xz` from dl.winehq.org (30 MB), extracted to `workspace/wine-ref/wine-9.0` |
| Wine notepad as a real guest | `programs/notepad` (main.c 892L + dialog.c 1326L + notepad.rc) cross-compiled with user-space mingw-w64 gcc 14.2 → **notepad_wine.exe, real PE32+ GUI, 358,723 bytes** (recipe: `scripts/sweep031_build_notepad_wine.sh`) |
| Wine oracles ported | `dlls/user32/menu.c` MENU_ParseResource · kernel32 load_string RT_STRING walking · `dlls/comctl32/commctrl.c` SetWindowSubclass/DefSubclassProc stack model · `dlls/user32/resource.c` LoadAcceleratorsW PE_ACCEL · `dlls/ntdll/rtlstr.c` RtlIsTextUnicode · `dlls/comdlg32/filedlg.c` GetFileTitleW |

### B. Engine work (src/loader/loader.c, all Wine-referenced)

- **SW-1203 LoadStringW/A REAL** — RT_STRING blocks, NO count word (16 × {WORD len; WCHAR str[len]}), cch==0 read-only-pointer mode. First attempt wrongly assumed a count word; the block hexdump of the real binary corrected it.
- **SW-1205 LoadMenuW/A REAL** — v0 MENUTEMPLATE parser (MF_END=0x80 / MF_POPUP=0x10, recursion into CreatePopupMenu) feeding the existing menu store. notepad's full menu bar (File/Edit/Search/Help + 5 popups) parses from its own .rsrc.
- **SW-1204 window subclassing** — per-hwnd {proc,id,ref} stack, newest-first, master-entry walk reset, 6-arg subproc signature; routed in BOTH dispatch and child SendMessageW.
- **SW-1206 class menu binding** — WNDCLASS(W/Ex).lpszMenuName captured (offset 56), loaded at CreateWindowEx when menu==NULL on a top-level window (notepad passes menu=NULL).
- **SW-1208 accelerator tables REAL** — LoadAcceleratorsW parses PE_ACCEL (8-byte entries) into the 0x00AC0000 store (SWEEP-025 identity); TranslateAcceleratorW/A now match FVIRTKEY/FALT and deliver WM_COMMAND.
- **SW-1209/1212 import frontier batches** — 15 (notepad) + 52 (notepad++) unresolved → **0**, every group typed per its Wine counterpart (path surgery, crypt32/wintrust honest no-signature world, IME closed, offline-honest network, ImageNtHeader REAL MZ/PE validation).
- **SW-1207 monitor identity fix** — MonitorFromWindow/Point/Rect now return 0x00080001, the identity GetMonitorInfoW actually accepts (round-trip was broken).
- **SW-1210 probe bug (engine-wide)** — `mw_probe_copy(src,n,NULL)` always failed (process_vm_readv EFAULT on the LOCAL iovec) → every SetRect/OffsetRect/IntersectRect silently no-op'd (notepad window was 0×0 px). New `mw_probe_readable()`; 4 call sites fixed.
- **SW-1211 pump WM_QUIT contract** — the no-event-source path now fills MSG with WM_QUIT(code=0) like Win32 (guests do `return msg.wParam`); notepad previously exited with garbage.
- **SW-1213 date/time NULL contract** — GetDateFormatW/GetTimeFormatW: lpDate/lpTime==NULL now means "use now" (NPP passed NULL and wcslen()ed the result).
- **SW-1214 FormatMessageW buffer contract** — always NUL-terminates; **FORMAT_MESSAGE_ALLOCATE_BUFFER (0x100) allocates via LocalAlloc and stores the POINTER at *(wchar_t\*)out** (writing out[0] there corrupted the guest pointer — two distinct crash classes fixed on the same NPP RVA 0xac4d5).

### C. Measured results (x3 deterministic, `artifacts/sweep031/sweep031_sweep.tsv`)

| program | exit ×3 | unresolved ×3 | runtime API events | verdict |
|---|---|---|---|---|
| **notepad_wine.exe** (Wine 9.0 notepad, real PE64) | **0 / 0 / 0** | 0 / 0 / 0 | 184 | **FULL END-TO-END on the merged engine**: class menu (RT_MENU, 5 popups) → 1026×769 surface (SW-1328 probe fix landed upstream independently) → EDIT child + subclass → statusbar via LoadStringW(cch=0) → accelerators → real pump → clean exit(0) via msg.wParam |
| **notepad++.exe** (real 8.6.9 x64, 8.4 MB, 584 imports) | 9 / 9 / 9 (deterministic) | 0 / 0 / 0 | 25,259 | Both config dialogs rendered as engine BMPs ("Load langs.xml failed!", "Load stylers.xml failed: C:\Users\MiniWin\AppData\Roaming\Notepad++\stylers.xml") → deterministic frontier = /GS fail-fast (FASTFAIL code=5) in the MFC C++ EH chain — same fail-fast class winmergeu had before SWEEP-030's EstablisherFrame fix (SW-1201 family, already proven solvable) |

**Graphics evidence** (user mandate «گرافیک رو لود»): `messagebox_1.bmp`, `messagebox_2.bmp` — genuine Notepad++ guest output (its strings, its MessageBoxW calls) rasterized by the engine.

### D. Honest gates

- VERIFIED: notepad_wine end-to-end exit0 x3; NPP 0-unresolved x3; G1 12/12 before/after; census determinism x3.
- PARTIAL: notepad++ GUI lifecycle — blocked at guest C++ EH unwind during CRT init (deterministic RIP 0x14006f439 after RtlUnwindEx resume; identical failure class to winmergeu SW-1201, already on the worklist).
- Known toolchain note: binutils windres needs `--preprocessor ...cpp-posix -DWIN32 -DRC_INVOKED` and the wine msgctxt accelerator strings rewritten (done in the recipe).
