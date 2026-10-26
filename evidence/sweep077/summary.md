# SWEEP-077 — the 20-puzzle INPUT campaign: 20/20 PASS_INPUT_STATE_CHANGE

Continuation of «ادامه ادامه ۲۰ برنامه» (continue — 20 programs). SWEEP-075
made all 20 sgtatham puzzles launch and render; only 2 of them (fifteen,
mines) had a demonstrated INPUT state change. The evidence bar for the
campaign is initial frame + valid input + post-input frame that differs.
This sweep closes the gap: **all 20 puzzles now pass the full bar**, plus
behavior probes on the B-group utilities. Two engine root causes fixed;
zero regressions; G1 green.

Baseline: origin/main ad1524b (SWEEP-076), engine sha 33661afa (4,357,280
bytes) rebuilt byte-exact. Final engine after this sweep: sha 060b2b03...
G1 PIN 12/12 (evidence/master/g1/20261026_074330). Downloads re-fetched and
SHA-256 verified against evidence/sweep074/targets.tsv (20/20 puzzles MATCH;
U08/U17/U25/U28 re-downloads MATCH).

## Root causes and fixes (engine-generic, no per-game code)

1. **SW-1344 — scripted keyboard lParam lacked the extended-key flag
   (KF_EXTENDED, bit 24).** Real keyboards set bit 24 for the
   arrow/PgUp/PgDn/Home/End/Ins/Del cluster. Guests decode arrows through
   that bit — sgtatham windows.c translate_key: `if (!(lParam &
   0x01000000)) key = MOD_NUM_KEYPAD|'4'; else key = CURSOR_LEFT;` Our pump
   delivered lParam=1 for every key, so EVERY arrow arrived as a NUMPAD
   digit: midend stripped the modifier (digits <= '9') and cursor movement
   never happened headlessly. This is why sweep075 could only show click
   state changes on fifteen/mines. Fix: mw_key_lparam(vk, down) builds the
   real lParam layout (scan=1 | bit24 extended | bit30 prev | bit31
   transition) at all three delivery sites (WM_KEYDOWN paced, WM_KEYDOWN
   gate-bypass, WM_KEYUP). Source cross-check: sgtatham puzzles windows.c
   WM_KEYDOWN + midend.c midend_really_process_key ('n' new-game,
   one_key_shortcuts=true default; 'q' quits — battery avoids it).
2. **SW-1345 — GDI Polygon/Polyline were silent TRUE-returning stubs.**
   cube draws its entire board (every tile AND the rolling solid) through
   Polygon(); the stub painted nothing so all 46 paints were byte-identical
   (uniq=1) — input state changes were invisible even when the game logic
   ran. Fix: real scanline polygon fill (even-odd ALTERNATE, the GDI
   default; no corpus guest sets WINDING) with the current brush +
   closed pen outline (last->first edge, GDI contract), routed through the
   SWEEP-006 DC surface machinery (mw_dc_surface + mwb_fill/mwb_set/
   mwb_line); Polyline draws the vertex chain unclosed.

## The battery (scripts/sweep077_input_battery.sh)

Per puzzle, 3 cold starts with the SAME scripted input through the real
guest pipeline (pump -> TranslateMessage -> guest WndProc -> midend ->
execute_move -> redraw -> surface capture):
- keys: LEFT,UP,RIGHT,DOWN x2 (extended-bit cursor) + ENTER/SPACE
  (CURSOR_SELECT/2) + 'n' (midend new game, fresh seed) — MW_GUI_KEYS,
  KEY_EVERY=7, KEYUP=1
- clicks: 12 board coordinates — MINWIN_MOUSE, MOUSE_EVERY=6
- budget 220 events, frame capture every paint (MINWIN_FRAME_EVERY=1)

Verdict = rc==0 x3, no [CRASH], and >=1 post-input frame whose MD5 differs
from the initial frame. Results: 20/20 PASS_INPUT_STATE_CHANGE (was 2/20).
Frame counts x3 stable; per-puzzle before/after BMP pairs in frames/
(frames/MANIFEST.tsv).

## B-group probes (bonus)

| id | guest | result |
|----|-------|--------|
| U08 | AutoHotkey v2 x64 | **PASS_BEHAVIOR** — a real .ahk script
(`x := 6*7; FileAppend(y,"*"); ExitApp 0`) executes inside MiniWin and
prints `MiniWin-AHK:42` to stdout, rc=0, 3/3 byte-identical (a complete
scripting-language runtime working headlessly). |
| U28 | Sigcheck x64 CLI | usage text (UTF-16) byte-identical x3, sha
aa27fc289bf1b796, rc=255 stable — PASS_EXPECTED_CLI_ERROR (honest: its own
usage convention, not a crash; stderr clean). |
| U17 | BgInfo x64 | rc=0, 2 frames rendered, no crash — PASS_LAUNCH_RENDER. |
| U25 | TcpView x64 | rc=124 x3 (alive at 70s, event loop, no crash) —
SW-1338 holds; frames still 0 (list-view custom-draw capture remains a
named frontier, unchanged). |

## Regression gates

- G1 PIN 12/12 (evidence/master/g1/20261026_074330, sha 060b2b03).
- Corpus parity x3, EXACT: winmine 82/82/82, sol 69/69/69, freecell
  10/10/10, calc 1/1/1, breakout 10/10/10, snake 13/13/13 (snake/breakout
  rebuilt after the sandbox reset wiped the llvm-mingw toolchain —
  re-downloaded 20240917 release, sources tests/games/), spider 513-638
  (pre-existing wall-clock timer band). ZERO crashes, ZERO PE32-FATAL,
  ZERO regressions from SW-1344/SW-1345.

## Honest state vocabulary

PASS_INPUT_STATE_CHANGE = launch + render + scripted input produced a
pixel-level different post-input frame through the real guest pipeline.
PASS_BEHAVIOR = program computed real observable output. UNCHANGED_RUNNING
= alive, no crash, frame capture frontier unchanged. No per-app hacks; both
fixes are generic engine corrections (keyboard model, GDI raster).

## Wagon 2 (same sweep): input evidence for the corpus games

snake (tests/games, rebuilt with llvm-mingw 20240917 after the sandbox
reset): steer-key battery -> 25 frames, 25/25 unique — every keystroke
changed the board. breakout: paddle/launch/brick battery -> 760 frames,
380 unique. Both PASS_INPUT_STATE_CHANGE (before/after PNGs in frames/).
The 48-target campaign plus the canonical corpus now has input-state
evidence on 22 programs total (20 puzzles + snake + breakout).

Network note: a second B-group wagon (Notepad3/RegScanner/WinLister
typing probes) was attempted but every upstream host returned
connection failures (http=000) during this window; recorded honestly
as NOT DOWNLOADED this round. U03/U04/U07/R3 already hold PASS rc=0 x3
from SWEEP-076 on byte-verified binaries.
