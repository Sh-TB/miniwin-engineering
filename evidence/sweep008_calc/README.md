# SWEEP-015 — SW-801f acceptance: MiniWinCalc (simple GUI calculator)

User mandate: (test at least one simple graphical calculator or another program) — a real Win32 GUI calculator as PE x64 (zig cc, machine
0x8664), run under the loader's event pump with a scripted key sequence.

## What this proves
`tests/games/miniwin_calc.c` is a REAL Win32 GDI calculator: window
lifecycle (RegisterClassA -> CreateWindowExA -> ShowWindow -> UpdateWindow),
keyboard input via WM_KEYDOWN (digits, VK_OEM_PLUS/MULTIPLY/DIVIDE,
VK_RETURN, VK_ESCAPE), display + keypad drawn with real GDI (FillRect /
CreateSolidBrush / TextOutA / SetBkMode / SetTextColor), integer arithmetic
(deterministic: no rand()/time()), clean PostQuitMessage exit.
The loader's GDI-lite surface rasterizes every WM_PAINT frame to BMP.

## Acceptance (3 runs, MW_GUI_KEYS=49,187,50,13,27 = "1","+","2",Enter,ESC)
- exit 0 x3
- 4 frames per run (one paint per key + initial)
- ALL frames byte-identical SHA-256 across all 3 runs:
  1b92b95f873029f0 (per-run combined hash of the 4 frames)
- Final frame: display shows expression "1+2=" and result "3"
  (miniwin_calc_final_frame.png)
- keypad "1" button highlighted (last-pressed feedback loop is live)

## This required SW-801f (pump gate-bypass)
The old pump QUIT ("no event source: no timer/paint/keys left") at the
first idle gap: keys queued but pacing gate closed + no timer/paint.
Timer-driven guests (snake) never hit it; paint-driven guests (this calc)
died after their first key. Real GetMessage BLOCKS in that state.

## Build
python3 -m ziglang cc -target x86_64-windows-gnu -O1 \
    -o artifacts/sweep008/calc.exe tests/games/miniwin_calc.c \
    -luser32 -lgdi32 -lkernel32
