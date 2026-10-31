# SWEEP-082 — the user-named trio driven to full graphical init; the ms_abi callback-class fix; WinNc/renamer/notepad2mod un-crashed

## Baseline
- Resumed from origin/main 12d7c8b (SWEEP-081 addendum) == local after sandbox reset;
  clean rebuild byte-exact 92e7df46 BEFORE any change; G1 pre-change PIN 12/12
  (evidence/master/g1/20261026_171541).
- Final engine sha256 0eaff275... (G1 post-change PIN 12/12, evidence/master/g1/
  20261026_181029); loader grew by the fix set below.

## The user-named trio (plugins/status.tsv, sweep082 rows)
- **npp64 8.9.8.1 x64**: SW-1298-x64 RESOLVED. Verdict PASS_INPUT_STATE_CHANGE,
  rc=0 x3, ZERO crashes (was FRONTIER crash@RVA 0x1099b3 x3). The full init path
  runs: main window class proc registers its C++ object via WM_NCCREATE
  (SetWindowLongPtrW(0x10001,-21,obj) — previously absent from every trace),
  Scintilla children 0x30001/0x30003 created with working SCI direct-function
  handshake, font/style init completes, ExitProcess(0) after 13,876 API calls.
- **winrar 7.12 x64**: unchanged PARITY_FRONTIER_SW-1361 (rc=0 x3, no regression).
- **idman 6.42b2 x86**: unchanged PASS_LAUNCH_RENDER (honest dialog frame).

## Engine fixes landed (all generic; no app special-casing)
1. **SW-1380 — top-level WM_NCCREATE delivery**: real CreateWindowEx sends
   WM_NCCREATE(0x81) with &CREATESTRUCT -> WM_NCCALCSIZE(0x83) -> WM_CREATE(0x01).
   The child path has done this since SW-1335; the TOP-LEVEL paths
   (mw_gui_send_create / mw_gui_send_create_h, x64 + i386 staged) never sent
   0x81 — so every guest framework proc of the "staticProc: WM_NCCREATE ->
   SetWindowLongPtr(GWLP_USERDATA, CS->lpCreateParams)" family never registered
   its window object. FALSE return cancels the creation (real contract).
2. **SW-1381 — two honest imports for npp64**: COMCTL32.411 =
   GetWindowSubclass (searches the engine subclass chain g_sub for
   (hwnd,proc,id), stores the ref, real TRUE/FALSE) + SHELL32.165 =
   SHCreateDirectory (already implemented SWEEP-029; wired by name). x86 arity
   row added.
3. **SW-1381 CORRECTION — SHELL32.155 mis-map fixed**: the live wine
   shell32.spec says ordinal 155 = ILFree (the SWEEP-029 map claimed
   SHCreateDirectory — the wrong function CLASS for a deallocator call).
   ILFree implemented (trace-honest PIDL free) and 155 re-pointed; 165 =
   SHCreateDirectory kept.
4. **SW-1382 — top-level identity minting (window #2+)**: real Windows gives
   every top-level its own HWND. Only nested (WM_CREATE-reentrant) creations
   minted since SW-1256; a plain later top-level creation collapsed onto
   MW_GUI_HWND. Window #1 keeps the legacy identity; g_toplvl_main_taken gates
   window #2+ through mw_toplevel_register with the full SW-1380 creation
   sequence addressed to the minted handle. The surface store / class-proc
   snapshot now runs for window #1 only (a 0x0 helper window used to shrink
   the main surface to 32x32).
5. **SW-1383 — GetWindowLongPtrW caller-RVA observability** (Phase-5 class):
   every trace line now carries caller= (the same pattern SetLastError has);
   this pinned the SW-1384 producer in ONE rerun.
6. **SW-1384 — the ms_abi guest-callback ABI class fix**: mw_call_cb2/cb4
   (SW-1354e) typed their function pointers WITHOUT ms_abi — the host emitted
   SysV args (rdi,rsi,rdx,rcx) while MSVC guest procs read rcx,rdx,r8,r9: the
   guest's arg0 was our arg3, arg1 our arg0. EVERY 2/4-arg guest callback
   (wndproc dispatch, dialog procs, enum procs, subclass chain) received
   shifted arguments. Evidence: the SW-1334 dispatch passed h=0x30001 and the
   guest's first API saw (nil) (caller=0x140235125 pinned inside Scintilla's
   SWndProc). Fixed to ms_abi cb_t + **SW-1384b 64-bit returns** (an
   int-returning cb_t truncated Scintilla's real DirectFunction pointer
   0x1402350b0 -> 0x402350b0 — found by the resulting clean crash at the
   truncated jump). Pointer-typed args widened to uintptr_t: the font-enum
   family (EnumFontFamilies W/A/ExW/ExA) used to truncate HOST STACK pointers
   of the staged LOGFONT/TEXTMETRIC into uint32_t — the guest FONTENUMPROC
   scanned lfFaceName(+0x1c) of a 32-bit-truncated stack address (crash
   RVA 0x152f13, si_addr=0x68a9e20c = stack 0x7ffc68a9e1f0+0x1c).
7. **SW-1385 — cpu32 o32 immediate-size family fix**: 66-prefixed
   `mov r16,imm16` (B8-BF), `mov r/m16,imm16` (C7) and the accumulator ALU
   family (05/0D/15/1D/25/2D/35/3D) read imm32 unconditionally — the
   instruction stream desynced mid-block and the garbage decode ended in a
   NULL-ea store (winnc: crash at case 0xC6 after 551 steps inside the Delphi
   CRT's 66 b9/ba/b8 prologue; renamer's SW-1376 crash and the follow-on
   `66 05` crash confirmed the family). Real contracts honored: r16 writes
   keep upper bits, imm16 reads advance eip by 2.

## Plugin ledger deltas (3x cold runs each, plugin_run.sh)
- npp64:        FRONTIER_SW-1298-x64 crash=1 -> **PASS_INPUT_STATE_CHANGE rc=0**
- winnc:        FRONTIER_SW-1374 crash=1     -> **EXIT rc=86, crash=0** (CRT runs)
- renamer:      FRONTIER_SW-1376 rc=86 crash -> **EXIT rc=86, crash=0**
- notepad2mod:  ADVANCED_FRONTIER crash=1    -> **EXIT rc=0, crash=0**
- winrar/idman: unchanged (no regression)
- **tcpview64 NEW** (SWEEP-072 B-group U23, official Sysinternals zip,
  sha256 4fc5ceba...): reached the real EULA dialog (STATIC 72-wchar text +
  id=1/2 Agree/Disagree buttons + RICHEDIT license + TOOLBAR children) then a
  TOOLBAR poll-loop frontier (SendMessageW(0x30006) spin) -> TIMEOUT (honest;
  registered as SW-1386-tail for the next train).
- everything: TIMEOUT unchanged (own poll-loop frontier, pre-existing).
- hxd: BLOCKED_DOWNLOAD stays — mh-nexus.de TCP refused again this train
  (8th attempt; curl -4 IPv4 variant included). Same-category substitutes were
  probed dead in SWEEP-081; no unofficial mirrors used (protocol).

## Gates
- G1 PIN 12/12 twice (pre-change 92e7df46 @20261026_171541, post-change
  0eaff275 @20261026_181029).
- Corpus 3x after ALL fixes: winmine/sol/freecell/calc/breakout/snake/spider
  rc=0, ZERO crashes, frame counts stable (spider timing variance only) —
  ZERO regressions.
- Final plugin re-verification x3 on the final engine: table above.

## Next wagons
- npp64 full GUI interaction (menu/tab/edit probes through the now-registered
  window objects; pump-driven main-window frame).
- tcpview64 SW-1386-tail: the TOOLBAR poll loop (who spins it, which message).
- winnc rc=86: the installer stub exits by choice; the file-manager payload
  needs the unpacked-exe route.
- everything TIMEOUT decode (pre-existing poll loop).
