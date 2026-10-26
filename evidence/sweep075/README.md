# SWEEP-075 — the sgtatham puzzle family: 27 FAIL_CRASH -> 20 clean runs

Continuation instruction: "ادامه لیست بزرگ رو حل بکن" (continue solving the
big list). The big list = the honest remaining frontier recorded in
evidence/sweep074/summary.md, item 1: the 20-puzzle GDI/paint/teardown
family (27 FAIL_CRASH rows in sweep074/results.tsv).

## Method

Every fix was reproduced FIRST, decoded at the byte level, then fixed
generically in the engine (no per-game hacks). Six independent root causes
were found and removed (SW-1325..1330). The binary was rebuilt
deterministically after each step; final sha256
ecd893c8901c04d72ff2390994ddc29134d2a35d560ad219b7a455e47db55ab6,
4,315,168 bytes.

## Root causes and fixes (all engine-generic)

1. **SW-1325 — sprintf delivered nothing (map_crt_stream refused our own
   fmemopen streams).** mw__vsnprintf / mw___stdio_common_vsprintf create an
   internal fmemopen FILE*, but mw_vfprintf's map_crt_stream runs the
   BUG-065 validity gate (registry of FILE*s ever handed to a guest); an
   unregistered internal stream was refused and the formatted bytes went to
   the stderr fallback — the guest's buffer kept its stale content.
   Chain of evidence: obfuscate_bitmap's sprintf(numberbuf,"%d",counter)
   stayed empty -> strlen() read stack garbage -> SHA_Bytes consumed junk ->
   the obfuscate masks were wrong AND non-deterministic (same seed
   820600e03393 produced masks 8ef773770a1d and d7de61486ed0; real SHA-1 is
   91a817c2df05...) -> game descs decoded to garbage w/h -> blackbox
   new_game wrote BALL_CORRECT at grid[145*217+115] -> SIGSEGV RVA 0x18a6.
   Fix: register every internally created stream (mw_fileptr_register).

2. **SW-1326 — WS_CHILD without a parent got the main window's HWND.**
   Real Windows fails such a CreateWindowEx with ERROR_TLW_WITH_WSCHILD
   (1406). The engine's fall-through returned the single MW_GUI_HWND, so
   the puzzles' statusbar create (parent=NULL, WS_CHILD) collided with the
   main window; its DestroyWindow then tore down the MAIN window (full
   early teardown, free storm, PostQuitMessage) and the guest later
   dereferenced the freed frontend (tcache-mangled pointer 0xa278...) —
   SIGSEGV RVA 0x124e0. Fix: return NULL + SetLastError(1406) on both the
   A and W paths; guests already handle a NULL statusbar.

3. **SW-1327 — GetWindowRect answered children with the MAIN rect.**
   get_statusbar_height() read the main window's height (427) as the
   statusbar height; adjust_statusbar then resized the main window to
   11x427 and new_bitmap allocated 11x11 — the board painted into a
   32x32 corner. Fix: children answer with their own rect record (same
   contract family as SW-1288).

4. **SW-1328 — every memory-probe with dst=NULL failed (EFAULT).**
   mw_probe_copy used process_vm_readv with the LOCAL iovec base = NULL;
   process_vm_readv rejects a NULL local buffer, so the readability probe
   returned 0 for EVERY pointer. Consequence: mw_rc_ok() was always false
   and every rect API gated on it (SetRect/OffsetRect/IntersectRect/
   UnionRect/InflateRect/EqualRect) returned 0 WITHOUT writing — the
   WM_PAINT IntersectRect left rcDest uninitialized (measured:
   dst-before values flowed straight into the BitBlt args), producing
   `BitBlt 1958712133x-95 @942518224,32765` (0 px). Fix: NULL-dst probes
   read into scratch memory; large spans probe head+tail.

5. **SW-1329 — GDI32!ExtTextOutW was bound to a 5-arg body.** The puzzles'
   win_draw_text calls ExtTextOutW (8 args); the entry pointed at
   mw_TextOutW, so the callee read s from the lprc slot (NULL) and len from
   the string-pointer slot — every glyph silently dropped (probed: 15 calls
   with s=(nil) len=0). Fix: real 8-arg mw_ExtTextOutW honoring
   ETO_OPAQUE/ETO_CLIPPED; GDI32 + USER32 entries rebound.

6. **SW-1330 — the mouse DOWN/UP pair was stranded by the pacing gate.**
   With MINWIN_MOUSE_EVERY>2 the DOWN landed on a %==0 slot but the UP
   waited for the next multiple; no other event source existed, the pump
   declared "no event source" and the guest quit with a half-click
   (fifteen: WM_LBUTTONDOWN delivered, WM_LBUTTONUP never). Fix: a pending
   UP bypasses the pacing gate.

## Results

- All 20 sgtatham puzzles (P01..P20) now run to completion: rc=0, zero
  crashes, 3 cold runs each, deterministic (input_battery.tsv).
- Boards render with real game state: fifteen's shuffled 4x4 with digits,
  solo's full 9x9 grid, blackbox's board frame, mines' minefield, etc.
- Scripted-input proof (MWGui mouse channel): fifteen — click slides the
  clicked segment (frames fifteen_before_click.bmp /
  fifteen_after_click.bmp differ, 16 paint cycles); mines — click reveals
  cells (state change, frames 6-12). The other 18 games need per-game
  interaction models (edge buttons, dialogs, keyboard-first UIs, or a
  network peer for netgame) and are documented honestly as
  PASS_LAUNCH_RENDER, not PASS_BEHAVIOR.
- G1 PIN 12/12 x3 (evidence/master/g1/20261026_032328, _032406, _032424-
  named dirs _032438), regression 12/12, no crash dumps.
- Corpus parity x3, fully deterministic: winmine 82 EXACT, freecell 10
  EXACT, calc 1 EXACT, breakout 48 EXACT, snake 30 EXACT, sol 69 frames
  (baseline 38) — final deal content verified complete (7 tableau columns,
  foundations, deck back; more repaint cycles after the SW-1326/1327
  statusbar-flow change; zero visual regressions).

## Known remaining deltas (honest)

- The paint surface stays 1024 px tall for several puzzles (window-height
  arithmetic path still oversized: SW-1330-era trace shows
  SetWindowPos(248x4391)); the board itself paints correctly in the top
  band; frames carry black margin below the board.
- Window metrics: SystemParametersInfo(SPI_GETWORKAREA) still returns
  FALSE (documented fallback path); child statusbars render 1 px tall.
- netgame (P11) needs a peer; its UI runs but board state cannot change
  headlessly.

## Reproduction

- Puzzles: MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=60 ./minwin_loader
  artifacts/sweep074/dl/<puzzle>.exe (re-fetch via
  scripts/sweep075_fetch_puzzles.sh; every exe sha256-verified against
  evidence/sweep074/targets.tsv).
- Input proof: MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=60 MINWIN_MOUSE="48,115"
  MINWIN_MOUSE_EVERY=4 ./minwin_loader artifacts/sweep074/dl/fifteen.exe
- Battery: scripts/sweep075_input_battery.sh (20 targets x 3 runs).
