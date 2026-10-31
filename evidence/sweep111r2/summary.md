# SWEEP-111r2 — rebar plane + registry hierarchy + the IDM ghost-writer root cause

Binary sha256 828b6144 (G1 PIN 12/12 evidence/master/g1/20261030_192811 + 20261030_200335).
Baseline reconciled: local 242af84 (SWEEP-030, sandbox reset) -> origin 2ec621f (SWEEP-111),
rebuilt byte-exact fdee1014 first, then the round's fixes.

## Problem train (measured, all engine-generic, no per-app hacks)

1. Baseline re-establishment after the sandbox wipe: WinRAR SFX INSTALL_OK rc=0,
   winrarinstd ALIVE_PUMP rc=124 x3, npp64 PASS_INPUT_STATE_CHANGE, idman
   FRONTIER_SW crash=1 steps=17302 reproduced (SW-1539 shape).
2. SW-1551 REBAR BAND MODEL (wine dlls/comctl32/rebar.c, include/commctrl.h ids):
   ReBarWindow32/msctls_rebar32 got class id 0x8F (was the SW-1521 TOOLBAR
   identity). RB_INSERTBANDW(0x40A)/A(0x401) validate cbSize>=0x30, store the
   RBBIM-gated fields, layout (MoveWindow the band child per cx/cxMinChild +
   cyMinChild), return TRUE; RB_SETBANDINFOA(0x406)/W(0x40B) update+TRUE;
   RB_GETBANDCOUNT(0x40C) -> n; RB_GETBARINFO fills cBands; RB_GETROWCOUNT/
   GETROWHEIGHT/GETBARHEIGHT/GETRECT/IDTOINDEX/SIZETORECT/DELETEBAND/MIN/MAX.
   Measured: WinRAR's 4 inserts -> TRUE; band children sized (GetWindowRect
   child 1x1 -> 1000x37@0,20; the toolbar/rebar/combo bands lay out).
3. SW-1551 COMBOBOXEX32 (class 0x90): CBEM_INSERTITEMW/A parse
   COMBOBOXEXITEMW, store the item, return the index; CBEM_GETCOMBOCONTROL/
   GETEDITCONTROL return the control hwnd; plain CB_* forwarded
   (RESETCONTENT/SETCURSEL/GETCOUNT/GETCURSEL/SHOWDROPDOWN/ADDSTRING).
4. SW-1552 REGISTRY ANCESTOR MATERIALIZATION: RegOpenKeyExW on a path whose
   DESCENDANTS exist now materializes the missing intermediate key (Windows
   hierarchy invariant; the MWREG001 flat snapshot stores leaves only).
   Measured: 'HKCU\Software\WinRAR\Profiles' / '...\WinRAR' / '...\General\Toolbar'
   open (the guest's parent-first probes worked; 166 Toolbar-miss -> 0).
5. SW-1554 toolbar seed values: Layout\Band76_0..4 = 100 (real first-run
   values measured from 3 independent any.run WinRAR traces; cross-checked
   with the guest's 0x14586d normalize loop capping at 0x64); Buttons\View*
   + Lock + LargeButtons/ButtonsText flags (the guest binary's own name set).
6. SW-1553/1558 instruments: SW-1503 guest backtrace gate extended to
   'Band76_0'; MW32_PROTECT_AT=<eip> deferred arming of the SW-1550 read-only
   guard (the boot shares the hunted page); MW32_SENTINEL="va:expect" per-step
   verify with tagged probes (gate head/pop/widen/pre-call + Reg* internals).
7. SW-1557 m32_st64 watch-visible (x87 FSTP/FISTP + SSE2 MOVSD were the last
   unwatched store classes).
8. SW-1559 THE IDM GHOST-WRITER ROOT CAUSE (issue #25, SW-1539 closed):
   `mw_RegOpenKeyExW`'s `*result = handle` is an 8-byte host void* store; the
   guest's HKEY local is 4 bytes, so the high dword (0) landed on the caller's
   saved-EBP slot 4 bytes above. The SWEEP-111r2 watch saw NO store primitive
   because it is a raw host C store, not an interpreter op. Sentinel probe
   trail: RegOpen-pre-out GOOD -> RegOpen-post-out FLIPPED (0x3fefef60 -> 0).
   Fix: mw32_out_handle writes 4 bytes for 32-bit guests (RegOpenKeyExW,
   RegCreateKeyExW, the SW-1552 ancestor path).
9. SW-1559 CreateProcessW PROCESS_INFORMATION: the x64 fill wrote 24 bytes
   into the 16-byte Win32 struct (hThread's high dword clobbered dwProcessId,
   the pids landed past the struct). Width-aware fill added.
10. SW-1560 AllowSetForegroundWindow arity=4 (the unknown-arity 0-pop drifted
    the parent's esp +4 -> /GS cookie fail-fast 0xC0000409).

## Measured results (binary 828b6144)

- idman (issue #25): the child IDM1.tmp now runs the second-stage installer
  END-TO-END: cpu32_run rc=0 steps=17105 (was crash steps=16872, crash=1,
  saved-EBP=0). The parent spawns + closes the child handles + destroys its
  dialog cleanly (crash=0; was SIGSEGV crash=1). rc=9 x3 deterministic now
  comes from the parent's OWN epilogue: it raises 0xC0000409 through its own
  SetUnhandledExceptionFilter(NULL) + UnhandledExceptionFilter +
  TerminateProcess — a guest-side termination, NEW named frontier SW-1561
  (which failure the installer's epilogue encodes), NOT an engine crash.
- winrarinstd (issues #23/#26): ALIVE_PUMP rc=124 x3 deterministic, init
  completes (window tree rebar+2 toolbars+comboex+statusbar+listview with
  LoadStringW-fed columns), battery input pumped. Remaining named frontier:
  the toolbar button-add loop (TB_ADDBUTTONSW send site 0x145b0c, caller
  0x145948) walks the DYNAMIC button list whose counts live at guest
  0x24B1D0/D4 — the compiled-in constructor 0x144c84 (20 nodes, stride 0x60)
  has NO callers in 7.12 (dead code), so the list fill path is the next
  decode (SW-1562).
- npp64 (issue #24): PASS_INPUT_STATE_CHANGE x3 (frames=28 uniq=13) unchanged.
- Gates: G1 PIN 12/12 x2; corpus_gate 15 rows rc=0-in-9 crash total 0
  (xp.* SEED rows still MISSING from the sandbox wipe, owed).

Evidence: evidence/sweep111r2/{idm_child_idm1tmp_trace.json,
idm_parent_trace.json,winrarinstd_frame.png}; plugins/status.tsv rows.
No force-push. No guest binaries committed.
