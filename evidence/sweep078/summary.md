# SWEEP-078 — B-group tool campaign + 6 generic engine root causes

## Baseline
- Started from origin/main 87fe73d (SWEEP-077), clean rebuild, G1 PIN green
  before any change. Final engine sha256 prefix 335e08ce6f0c038f
  (full sha in engine_sha256.txt); final G1 evidence master/g1/20261026_111644.

## Downloads (official sources, URL + SHA-256 logged)
- NirSoft zips (all 200 OK, exes MZ-verified inside):
  cports-x64, filetypesman-x64, regscanner-x64, regscanner-x86 (sha
  b07497d6... IDENTICAL to SWEEP-076's verified copy), shexview-x64,
  whatinstartup-x64, winlister-x86.
- Sysinternals direct: ZoomIt.exe (e5c55414... byte-identical to SWEEP-076 U16),
  Winobj.exe (c8313c8e..., new target).
- Honest failures this window: scintilla.org http=000 (SciTE), angusj.com 409
  (Resource Hacker), keepass.info returned an HTML redirect page not a zip,
  mh-nexus.de (HxD) http=000, nirsoft shellmenuview 404 under both names,
  GitHub Notepad3 release assets 404 (tag retired upstream). These are
  recorded, not replaced with mirrors.

## Battery (scripts/sweep078_battery.sh — 3 cold runs, keys+mouse through the
## real guest pipeline, frame-diff verdict)
- W01 cports x64          rc=0 x3, 1 frame, PASS_LAUNCH_RENDER
- W02 FileTypesMan x64    rc=0 x3, 2 frames, PASS_LAUNCH_RENDER
- W03 RegScanner x64      rc=0 x3, 1 frame, PASS_LAUNCH_RENDER
- W04 shexview x64        rc=0 x3, 1 frame, PASS_LAUNCH_RENDER
- W05 WhatInStartup x64   rc=0 x3, 1 frame, PASS_LAUNCH_RENDER
- W07 RegScanner x86 (UPX) rc=0 x3 — was rc=139 before this sweep's fixes
- W06 Winobj              rc=0 x3, 0 frames — console/pump exits before a
  frame lands; honest FAIL_NO_FRAMES (tool itself launches clean)
- W09 ZoomIt (137MB PE32 — self-extractor shape) rc=0 x3, 0 frames; the asset
  from live.sysinternals is byte-identical to the SWEEP-076-verified one, but
  it does not reach a renderable UI headless — recorded as-is
- W08 winlister x86 (UPX) rc=0 standalone; rc=139 x3 under the battery's GUI
  event env — guest EIP=0xa00001, host RIP=0x400000000: a late-bound
  GUI-path API call reaches a slot whose host fn pointer is garbage
  (1<<32 pattern). Frontier SW-1354, next wagon.

## Root causes fixed (all generic, loader.c)
1. SW-1348: PE32 crash diagnostics — page-clamped guest byte dumps at the
   last 6 eip-hist sites + guarded ESI/EDI/EBX/EDX/EBP pointer context +
   MW32_DUMP_VA="va:len,..." dump facility (pre-run, post-run, crash).
2. SW-1349: x86 shift-group opcode mapping was shifted — /0 (ROL) executed as
   SHL, /1 (ROR) as SHR, /6 (SAL) as ROL — across C0/C1/D0/D1/D2/D3 at 8/16/32
   bit. Added alu_rol16/ror16/rol8/ror8, fixed ROR's CF (MSB not LSB). Proof:
   UPX filter-0x49 unfilter's `rol eax,16` produced 0x00010000 instead of
   0x000103a4; every E8/E9 call site in unpacked UPX binaries patched wrong.
3. SW-1350: REP SCAS (AE/AF) compared AL against [ESI] instead of [EDI] —
   UPX stubs' `repne scasb` strlen scanned the wrong region (image header
   zeros) and stopped 12 bytes late, walking the import-name blob off the end.
4. SW-1351: 8-bit register reads of indexes 4..7 (AH/CH/DH/BH) read the LOW
   byte of r[rm & 7] — i.e. ESP/EBP/ESI/EDI bytes (e.g. `86 c4` = xchg ah,ah
   pulled ESP&0xFF=0xe0 into the value). LD8M + LD_RorM8 now map 4..7 to the
   high bytes of EAX/ECX/EDX/EBX.
5. SW-1352: GetProcAddress on a DATA export (msvcrt _acmdln/_iob/__argv
   class) returned a code thunk; UPX stubs store the answer straight into the
   rebuilt IAT and guests deref it as data (RegScanner: mov eax,[__imp__acmdln];
   mov esi,[eax] read thunk-page bytes). Data exports now return the data
   address when it is 32-bit reachable.
6. SW-1353: stdcall arity table was missing ~38 names the RegScanner/winlister
   CRT+shell path calls (AttachThreadInput, DeferWindowPos family, GetMessage/
   PeekMessage/DispatchMessage/TranslateMessage, GetModuleFileNameA/W,
   SetForegroundWindow, GetWindowThreadProcessId, Process32First/Next,
   service trio, SHGetSpecialFolderPathA/W...). Each unknown under-popped 0
   and left args on the guest stack; the next ret popped data as EIP (0x100).
   Also added x86-mangled msvcrt C++ memory operators (??2@YAPAXI@Z
   operator new and siblings) which were UNRESOLVED -> NULL -> CRT died.

## Verification
- Corpus 7/7 deterministic parity EXACT (winmine 82, sol 69, freecell 10,
  calc 1, breakout 10, snake 13, spider timing-varies as always) x3 runs,
  ZERO crashes, ZERO unresolved — no regressions from the interpreter fixes.
- G1 PIN 12/12 green at every step (final evidence master/g1/20261026_111644).
- RegScanner x86 full evidence chain: rc=139 (crash) -> rc=0 standalone ->
  rc=0 x3 under the GUI input battery.

## Honest notes
- The winlister GUI-mode crash (SW-1354) and the WinObj/ZoomIt zero-frame
  runs are recorded as-is; no app-specific hacks were used anywhere.
- MW32_DUMP_VA is opt-in and off by default; the crash-report byte dumps are
  always-on diagnostics (bounded, page-clamped, never deref wild pointers).
