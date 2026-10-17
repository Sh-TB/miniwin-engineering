# SWEEP-005 — SW-501 micro-game 100% under MiniWin (image evidence)

## What this proves
`tests/games/miniwin_snake.c` is a REAL Win32 GDI game compiled to PE x64
(zig cc, machine 0x8664). MiniWin loads it, runs its window lifecycle
(RegisterClassA -> CreateWindowExA -> ShowWindow -> UpdateWindow), pumps
synthetic-but-real events into its message loop (WM_TIMER game ticks,
scripted WM_KEYDOWN turns, WM_PAINT), the guest's own WndProc draws every
frame with GDI (FillRect / Ellipse / Rectangle / TextOutA via
CreateSolidBrush + SelectObject), the loader's SWEEP-005 raster surface
renders each frame to BMP, and the game exits 0 through
ESC -> PostQuitMessage -> WM_QUIT(wParam=0) -> `return (int)msg.wParam`.

## Acceptance (scripts/sweep005_snake_accept.sh)
- 3 runs, identical key script (MW_GUI_KEYS=40,39,38,39,40,39,38,39,40,39,27):
  exit code 0 x3, 35 frames x3.
- ALL 35 frames byte-identical SHA-256 across all 3 runs
  (determinism_report.txt has every hash).
- Final frame HUD: `MiniWin Snake | Score: 10 | Len: 9` — food eaten
  (LCG seed 12983 places it on the scripted path), snake grew 8->9,
  new food respawned at (1,14). Verified in pixels.

## Files
- miniwin_snake_game.gif        — the 35-frame run as an animation
- miniwin_snake_final_frame.png — end state (score 10, len 9)
- miniwin_snake_first_frame.png — first WM_PAINT frame
- determinism_report.txt        — per-frame SHA-256 x3
- snake_api_trace.txt           — full loader API/GUI-event trace, run 1
- exit_code.txt                 — run 1 exit code (0)

## Loader changes this wave (all behind MW_GUI_EVENTS=1; default v1 untouched)
GDI32 pseudo-module (24 entries) + event pump v2 + WNDPROC registry +
framebuffer; BUG-067 (__p__acmdln NULL deref), BUG-067b (SysV va_list in
ms_abi varargs), BUG-067c (key/paint priority). G1 re-pinned 12/12.
