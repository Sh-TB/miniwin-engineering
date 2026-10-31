# SWEEP-073 fix audit — accept / modify / reject, with evidence

The task instructed: validate, do not blindly accept. Two fixes were proposed;
six root-cause changes actually landed. Verdicts:

## Fix 1 — `_onexit` via the guest atexit registry: ACCEPTED WITH MODIFICATION
- Report claim: old code passed a guest fn pointer to host `atexit()`; route
  it through the existing guest registry.
- Source check (pre-fix line 7961): `mw__onexit` did exactly
  `return atexit(func)` — the host glibc registry. The same defect class was
  already fixed for `atexit` (ISS1-WAVE4 registry + `w3_run_guest_atexit`).
- Contract check: msvcrt `_onexit` is `_onexit_t _onexit(_onexit_t)` — it
  returns the PREVIOUS registered handler (or NULL), a full-width function
  pointer, not an int. The proposed patch kept an `int` return; that part is
  REJECTED, the return type is corrected.
- Additional general defect found while auditing the registry: x86 guests'
  exit handlers were invoked as native host functions. SW-1316b adds
  `g_cpu32_active` dispatch (cpu32_call for PE32 VAs, direct ms_abi call for
  PE32+). The same width dispatch was applied to `mw__initterm` /
  `mw__initterm_e`.
- Regression: 7zr rc=139 -> rc=0 x3 (identical, deterministic); the trace now
  shows `_onexit(func=0x45dc80)` registered and the handler running from the
  guest teardown path.
- HONEST CAVEAT: the report's causal story for 7zr was wrong. 7zr's rc=139
  signature (RIP=0x40159800000000, steps=74) is mw__initterm reading a PE32
  32-bit initializer table with an 8-byte stride — two neighbor dwords
  {0x00000000, 0x00401598} merged into one bogus 64-bit "function pointer"
  and called as a host function. The _onexit defect is real and fixed, but
  the 7zr crash required SW-1316b (initterm width) + SW-1319 (0F BA /
  shld+shrd) + __p___initenv + SW-1320 (named GetProcAddress thunks).
  Final 7zr state: loads, runs 41 real initializers, registers _onexit,
  prints its own banner and usage text, exits rc=0 3/3. Its `a` command
  still falls back to usage (arg-parse frontier, SW-1321, open).

## Fix 2 — accelerator storage: ACCEPTED, THEN CORRECTED AGAIN (SW-1317b)
- Report claim: 8-byte array vs 512 six-byte records; fix = 512 six-byte
  records.
- Source check: confirmed the 8-byte slot and the up-to-3072-byte memcpy.
- Layout audit (Wine dlls/user32/resource.c PE_ACCEL; ReactOS
  win32ss/user/user32/windows/accel.c): PE RT_ACCELERATOR records are EIGHT
  bytes {WORD fVirt; WORD key; WORD cmd; WORD pad}; count = size/8. The
  report's "six-byte Win32 representation" is the GUEST-SIDE ACCEL struct
  used by CreateAcceleratorTableW/CopyAcceleratorTableW, not the resource
  format. Corpus proof: calc.exe's table is 760 bytes = 95x8 exactly and is
  NOT divisible by 6 (a 6-byte reader over-frames every entry after the
  first — silent wrong-hotkey behavior).
- Landed: normalized store (mw_accel_ent_t {fvirt,key,cmd}, 512 entries per
  slot), 8-byte resource parse, 6-byte Create/Copy ABI, DestroyAcceleratorTable
  now frees the slot (old ANSI-name export leaked all 8 slots), full
  capacity for CreateAcceleratorTableW (old code copied 256 but stored n).
- Regression: calc n=95, winmine n=2, spider n=12 parsed correctly; parity
  frames EXACT (calc 1, winmine 82, spider 593-631 band); no corruption.
- Fixture note: the task's "deterministic accelerator fixture" requires a
  cross-compiler; no mingw toolchain exists in this sandbox. The REAL corpus
  tables above (95/12/2 entries, static-scan verified against file bytes)
  serve as the live regression; the fixture is recorded as
  BLOCKED_TEST_HARNESS with the source-level contract documented.

## Fix 3 — GetAsyncKeyState: ACCEPTED AND GENERALIZED (SW-1318)
- Report claim: returns zero unconditionally, breaks a Pong paddle.
- Source check: confirmed `return 0` (both GetAsyncKeyState and GetKeyState).
- Landed: canonical 256-entry key table (high bit = down) + per-VK
  "went down since last async query" latch consumed on read (Win32 low-bit
  contract), updated by the SAME pump sites that deliver WM_KEYDOWN/WM_KEYUP
  and mouse buttons (VK_LBUTTON). GetKeyboardState copies the table.
- Press/release model: the scripted channel models keys the harness HOLDS
  (default, legacy behavior preserved); MW_GUI_KEYUP=1 delivers the matched
  WM_KEYUP one pump slot after each WM_KEYDOWN.
- Regression: BgInfo64 x64 with MW_GUI_KEYS="27" MW_GUI_KEYUP=1 traces
  `#1 WM_KEYDOWN VK=0x1b` then `#2 WM_KEYUP VK=0x1b`, rc=0 clean. Corpus
  input-sensitive games unchanged: winmine 82 / sol 38 / freecell 10 /
  breakout 48 / snake 30 EXACT.
- Pong: no pong.exe exists anywhere in the corpus or the sandbox, and no
  cross-compiler is available to build one — the paddle-movement demo is
  BLOCKED_TEST_HARNESS; the state model itself is proven by the pair
  delivery above and by the games' message-driven parity.

## Fix 4 — Notepad++ pseudo-handle: REPORT CONFIRMED, ROOT CAUSE FIXED GENERALLY (SW-1322)
- Reproduced: LoadLibraryA("comctl32.dll") -> 0xdead000c; N++ reads
  [rcx+0x3c] (e_lfanew) -> SIGSEGV si_addr=0xdead0048 at RVA 0x1d04.
- Contract: Wine/ReactOS make HMODULE == image base with readable MZ/PE
  headers for EVERY module (wine ntdll/loader.c get_modref; ReactOS
  RtlImageNtHeaderEx). Fake PE headers at the OLD sentinel values are not a
  typed object — REJECTED as designed. Landed: pseudo-modules now live at
  real page-aligned image bases (MW_PSEUDO_BASE 0xDEA00000, 64KB stride),
  each with synthesized DOS+PE headers (MZ, e_lfanew=0x40, PE\0\0, PE32+
  optional header); all 9 sentinel sites migrated to
  mw_pseudo_handle/mw_is_pseudo_module. Exports still resolve through the
  name-keyed dispatch table (module-agnostic), as before.
- Regression: notepadpp64 advances past the header read, renders BOTH real
  MessageBoxes again (Configurator, Load stylers.xml failed — 2 BMPs), and
  reaches the pre-existing SW-1298-x64 frontier (RVA 0x3b94, `mov rax,[rcx+28]`
  with NULL context after CreateWindowExW). Status: PARTIAL, unchanged
  frontier, honestly not "fully supported" (no editing probe).

## Extra general fixes landed by this sweep (report did not propose them)
- SW-1319: i386 interpreter 0F BA /4..7 (bt/bts/btr/btc imm8) + 0F A4/A5/AC/AD
  (shld/shrd imm8|CL) — 7zr CRT/LZ paths.
- __p___initenv (msvcrt): real &__initenv cell (7zr _setenvp wrote EA=0).
- SW-1320: PE32 GetProcAddress late-binds now return NAMED thunks (real
  stdcall arity) instead of raw host pointers (7zr SetDefaultDllDirectories
  returned 0x20602b0; interpreter executed it as guest code) and instead of
  anonymous arity-32 thunks (stack desync of +28 after 1-arg APIs).
- SW-1323: UCRT `_get_narrow_winmain_command_line` implemented (WinMain
  lpCmdLine semantics). Its NULL gate was the single shared pre-main SIGSEGV
  of ALL 20 sgtatham puzzle guests (60/60 runs, uniform RVA 0x8919
  `mov al,[rcx]` rcx=0) and of Notepad3/AHK-x86.
