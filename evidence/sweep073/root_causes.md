# SWEEP-073 root causes (fix-validation sweep) — see also ../sweep074/root_causes.md

1. SW-1316  msvcrt `_onexit`: was `atexit(func)` with a GUEST pointer (glibc
   jumped into the guest image at host teardown). Now routed through the
   guest atexit registry with the correct `_onexit_t _onexit(_onexit_t)`
   contract (returns previous handler at full pointer width, NULL on error).
2. SW-1316b `_initterm`/`_initterm_e`/`w3_run_guest_atexit`: PE32 guests store
   32-bit PVFV tables; the 64-bit stride merged neighbor dwords into bogus
   function pointers (7zr RIP=0x40159800000000, steps=74 — reproduced 3/3
   pre-fix). Now width-dispatched: 4-byte stride + cpu32_call re-entry for
   x86, direct ms_abi for x64.
3. SW-1317/1317b accelerator store: 8-byte slot fields vs up-to-3072-byte
   memcpy (every >=2-entry table overflowed; live corpus: calc 760B/95
   entries at the CORRECT 8-byte PE record stride, spider 96B/12, winmine
   16B/2). Normalized store + bounded reads + slot-free on Destroy + real
   Create/Copy ABI strides.
4. SW-1318 keyboard state: canonical 256-entry table (0x80 = down) + one-shot
   latch (Win32 low-bit) consumed by GetAsyncKeyState; GetKeyState/GetKeyboard
   State read the same table; pump sites deliver WM_KEYDOWN/WM_KEYUP pairs
   (MW_GUI_KEYUP=1) and mouse buttons update VK_LBUTTON.
5. SW-1319 i386: 0F BA /4..7 (bt/bts/btr/btc r/m32, imm8) and 0F A4/A5/AC/AD
   (shld/shrd r/m32, r32, imm8|CL) — 7zr CRT-init and LZ-decoder paths.
6. __p___initenv: real `char***` cell (7zr _setenvp wrote through the old
   NULL gate -> m32_st32 si_addr=0, steps=118032).
7. SW-1320 PE32 GetProcAddress late-bind: named thunks with real stdcall
   arity (raw host pointers + anonymous arity-32 thunks both crashed 7zr —
   two different signatures documented).
8. SW-1322 pseudo-modules: real page-aligned image bases with synthesized
   DOS+PE headers; sentinel space 0xdead000X retired (9 sites).
9. SW-1323 UCRT `_get_narrow_winmain_command_line`: implemented (WinMain
   lpCmdLine = command line minus program token); its NULL gate was the
   shared pre-main crash of the whole 20-puzzle family.
10. SW-1324 vfprintf on unmappable guest FILE*: routed to stderr instead of
   forwarding NULL into host libc (loader-fault class).

Regression evidence: G1 PIN 12/12 after every engine change batch
(evidence/master/g1/20261025_210649 sha b7b86431...; final build sha
08ba52fc... re-pinned below in summary.md); corpus battery parity EXACT
(winmine 82 / sol 38 / freecell 10 / calc 1 / breakout 48 / snake 30 /
spider 593-631 band / notepad, wordpad, mspaint rc=0).
