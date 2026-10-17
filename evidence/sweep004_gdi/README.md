# SWEEP-004 — GDI-lite dialog raster (2026-10-18)

User mandate: «ببین نمیتونی از برنامه گرافیکی تصویر هم بدی» — produce a real
image from a graphical program.

## What landed
- mw_MessageBoxW / mw_MessageBoxA render a classic Win95-style dialog
  (font8x8, D. Hepper, public domain) to 24-bit BMP under
  <results>/<exe>/screenshots/messagebox_N.bmp. TEXT/TITLE/ICON-CLASS are the
  guest's own; layout is MiniWin's (no GDI engine yet) — honest contract.
- Trigger: putty.exe --version (real PuTTY x64) hits its genuine startup
  error path -> MessageBoxA("PuTTY Fatal Error",
  "Unable to load any WinSock library") -> dialog #1 rendered.

## Evidence
- putty_messagebox_1.bmp      — the rendered dialog (604x122x24bpp)
- putty_messagebox_minwin.png — PNG copy (2x nearest upscale)
- determinism_3x_sha256.txt   — 3 runs -> identical SHA-256 (f953a2f3ec131b3a*)
- putty_version_trace.json    — full API trace of the render run
- gui_wave_progress.png       — progress chart (lifecycle + 16-program map)
- driverview/heapmemview traces — 2 NEW programs (NirSoft x64), deterministic
  CRASH-GUEST 139 at 0x14000cd1d / 0x14000c581 after the advapi32 privilege
  phase (SW-557/SW-558).
