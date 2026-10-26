
---
Task ID: SWEEP-076
Agent: main (z.ai)
Task: «ادامه ادامه قطار قطار حداقل چند ده برنامه رو کامل بکن» — B-group rescue train: 8 FAIL-state utilities from the 48-target campaign

Work Log:
- Reconciled: local 242af84 -> origin 0b7cb7d (SWEEP-075, puzzles 20/20).
  Sandbox reset had wiped artifacts/sweep074/dl — re-downloaded the 8 failing
  targets from official URLs, SHA-256 byte-verified 8/8 MATCH vs targets.tsv.
- SW-1331: load_pe32 read dd[] AFTER free(raw) — UAF; AutoHotkeyU32 (~0.9MB,
  glibc mmap/trim class) got garbage dd[13] -> wild delay-walk ddesc
  0x43a76552. Fix: snapshot 32 dwords (zero beyond NumberOfRvaAndSizes).
- SW-1332: GetModuleHandleA/W/Ex(NULL) returned x64-only g_image_base (NULL
  in PE32) — LastActivityView died at `cmp word[eax],'MZ'`. Fix:
  mw_main_image_base() answers the active world (g_image_base32 x86 side).
- SW-1333: comctl32!#381 decoded = LoadIconWithScaleDown (5-arg + HRESULT jns
  check) from Notepad3 x64 call sites; implemented + ordinal map entry.
- SW-1334: per-child class-proc capture + SendMessage routing (x64 only).
- SW-1335/1335b: WM_NCCREATE->WM_NCCALCSIZE->WM_CREATE sequence for guest-
  class children; DefWindowProc answers WM_NCCREATE=TRUE (Scintilla init).
- SW-1336: shlwapi string family x20 real (StrStr/StrChr/StrCmp/StrDup/
  SHStrDup/StrToIntEx/StrSpn/StrCSpn/StrTrim/StrRetToBuf/UrlEscape/
  UrlCreateFromPath/StrFormatByteSizeEx).
- SW-1337: SHGetPropertyStoreForWindow honest E_FAIL (stub-0 = S_OK-with-
  NULL-out crashed Notepad3 x64 at 0x173a3f).
- SW-1338: SetWindowLongPtrW(GWLP_WNDPROC) returns current proc (class proc
  / main channel / engine defproc) — TcpView subclass gate.
- SW-1341/1341b: ZoomIt x86 137MB MAP_FIXED over the host loader text
  (silent 139, handler dead on zeroed pages — core-dump decoded); collide
  check + relocate to 0x40000000 + HIGHLOW type-3 fixups (the loop checked
  type 1 = HIGH: ZERO fixups were ever applied to x86 images; 37,180 now).
- SW-1342: import-descriptor stop on EITHER zero field (Wine/Windows rule);
  RegScanner's import dir overlaps its IAT and produced phantom descriptors.
- Gates: G1 PIN 12/12 (evidence/master/g1/20261026_054035, sha 33661afa);
  battery x3 deterministic; corpus parity green (winmine 82 / sol 69 /
  freecell 10 / calc 1 / breakout 10 / snake 13 x3, spider 592-628 band),
  ZERO regressions.

Stage Summary:
- 5 of 8 B-group FAIL guests now PASS rc=0 (R3, U03, U04, U07, U16); TcpView
  runs alive (no crash, event loop); RegScanner + WinLister decoded with
  named frontiers (SW-1343 Toolhelp32 module-name layout, SW-1340 low guest
  stack plane). Engine 4,357,280 bytes, 10 engine-generic root causes, no
  per-app hacks, no third-party binaries committed.
- Cumulative campaign: 20 puzzles + Notepad3 x86/x64 + AHK x86 + ZoomIt x86 +
  LastActivityView + TcpView(running) of the 48-target list.

---
Task ID: SWEEP-077
Agent: main (z.ai)
Task: «ادامه ادامه ۲۰ برنامه» — the 20-puzzle input campaign: initial frame + valid input + post-input frame for all 20 sgtatham puzzles, plus B-group probes

Work Log:
- Baseline: ff to origin ad1524b (SWEEP-076); engine 33661afa rebuilt
  byte-exact; G1 PIN 12/12 (evidence/master/g1/20261026_073259).
- Sandbox reset had wiped downloads again: re-fetched all 20 puzzle exes
  from chiark, SHA-256 20/20 MATCH vs evidence/sweep074/targets.tsv.
- Source-verified the input contract upstream (git.tartarus.org
  simon/puzzles, MIT): windows.c WM_KEYDOWN decode needs lParam bit 24
  (KF_EXTENDED) for arrows else they become MOD_NUM_KEYPAD digits;
  midend.c midend_really_process_key handles 'n' (new game, fresh 15-digit
  seed, one_key_shortcuts=true default) and 'q' (QUIT — battery avoids it).
- SW-1344 ENGINE FIX: mw_key_lparam(vk, down) — real keyboard lParam
  layout (scan=1 | bit24 extended | bit30 prev | bit31 transition) at all
  three delivery sites; arrows now decode as cursor keys.
- Pilot proved the fix (blackbox frames aedba8b0b5 -> a9d3aee1ea on cursor
  move) and exposed a second root cause: cube painted 46 byte-identical
  frames.
- SW-1345 ENGINE FIX: real GDI Polygon/Polyline (scanline even-odd fill +
  closed pen outline through the SWEEP-006 DC surface machinery); cube's
  tiles and solid are all Polygon() fills and the stub painted nothing.
- Battery v2 (scripts/sweep077_input_battery.sh): 20 puzzles x3 cold runs,
  keys (arrows x2 + Enter + Space + 'n') + 12 board clicks, budget 220,
  every frame captured; verdict = rc==0 x3 + no crash + MD5-different
  post-input frame. RESULT: 20/20 PASS_INPUT_STATE_CHANGE (was 2/20 in
  sweep075); cube uniq 1 -> 21.
- Frames evidence: evidence/sweep077/frames/<puzzle>/{before_input.bmp,
  after_input.bmp} + MANIFEST.tsv (20/20 DIFFERS).
- B-group probes: U08 AutoHotkey v2 x64 runs a real script
  (6*7 -> stdout "MiniWin-AHK:42", rc=0, byte-identical x3) =
  PASS_BEHAVIOR; U28 Sigcheck UTF-16 usage byte-identical x3 rc=255
  stable; U17 BgInfo rc=0 2 frames; U25 TcpView rc=124 alive x3
  (SW-1338 holds, frame frontier unchanged).
- Re-downloaded llvm-mingw 20240917 (sandbox wipe), rebuilt snake/breakout
  from tests/games; corpus parity EXACT x3: winmine 82, sol 69, freecell
  10, calc 1, breakout 10, snake 13, spider 513-638 (wall-clock band);
  G1 re-pinned 12/12 after both fixes (20261026_074330, sha 060b2b03);
  ZERO regressions.

Stage Summary:
- 20/20 puzzles at the full evidence bar (launch + render + input state
  change), 4 B-group probes recorded; 2 engine-generic root causes removed
  (keyboard extended-bit model, GDI polygon raster); engine 4,363,528
  bytes; no per-app hacks; no third-party binaries committed; no force-push.

---
Task ID: SWEEP-078
Agent: main (z.ai)
Task: «ادامه ادامه» — B-group tool wagon: 9 official downloads, input battery, generic engine root causes

Work Log:
- Reconciled: local == origin 87fe73d (SWEEP-077). Engine rebuilt byte-exact,
  G1 green before changes.
- Re-downloaded 9 official B-group assets after sandbox reset (nirsoft x6 +
  Sysinternals ZoomIt/WinObj + winlister x86); SHA-256 logged, RegScanner x86
  and ZoomIt byte-identical to SWEEP-076's verified copies. Honest
  BLOCKED_DOWNLOAD rows: SciTE/ResourceHacker/KeePass/HxD/ShellMenuView/
  Notepad3 (hosts down, 404s, HTML-not-zip) — no mirror substitutes.
- Battery: 9 tools x 3 cold runs with keys+mouse through the guest pipeline.
- ROOT CAUSES (all generic, loader.c):
  SW-1349 shift-group mapping /0=ROL /1=ROR /6=SAL was aliased to SHL/SHR/ROL
  across C0/C1/D0/D1/D2/D3 at 8/16/32-bit (UPX filter-0x49 unfilter proved it:
  rol eax,16 -> 0x00010000 instead of 0x000103a4; added rol16/ror16/rol8/ror8,
  fixed ROR CF=MSB);
  SW-1350 REP SCAS compared AL vs [ESI] not [EDI] (UPX strlen scanned header
  zeros, stopped 12 late, import walk ran off the image);
  SW-1351 8-bit reg reads for indexes 4..7 pulled ESP/EBP/ESI/EDI low bytes
  instead of AH/CH/DH/BH (86 c4 read ESP&0xFF=0xe0);
  SW-1352 GetProcAddress on DATA exports (_acmdln class) returned a code thunk
  — UPX stubs store it raw into the rebuilt IAT, guests deref it as data;
  SW-1353 ~38 missing stdcall arities (GetMessage/PeekMessage family,
  AttachThreadInput, DeferWindowPos, service trio, SHGetSpecialFolderPathA/W,
  Process32First/Next...) — each under-pop left args on the stack and the CRT's
  ret popped data as EIP (0x100); plus x86-mangled msvcrt operator new/delete
  (??2@YAPAXI@Z) were UNRESOLVED -> NULL -> CRT died;
  SW-1348 PE32 crash diagnostics: page-clamped guest byte dumps (6 eip sites),
  guarded pointer context, MW32_DUMP_VA dump facility (pre/post/crash).
- Proof chain for RegScanner x86: rc=139 (UPX unpack corrupt via SW-1349) ->
  rc=0 standalone (SW-1350/1351) -> GUI-mode IAT data-export crash (SW-1352)
  -> arity/operator-new crash (SW-1353) -> rc=0 x3 under the input battery.
- Verification: corpus 7/7 deterministic parity EXACT x3 (winmine 82, sol 69,
  freecell 10, calc 1, breakout 10, snake 13), ZERO crashes, G1 PIN 12/12
  (master/g1/20261026_111644), engine 335e08ce6f0c038f.
- evidence/sweep078: summary.md, results.tsv, dl_sha256.tsv, engine_sha256.txt,
  frames/ (5 PNGs). Commit pushed (no force).

Stage Summary:
- 6 new tools at the clean-run bar (cports, FileTypesMan, RegScanner x64+x86,
  shexview, WhatInStartup), winlister fixed standalone (GUI-mode frontier
  SW-1354 recorded honestly), WinObj/ZoomIt honest NO_HEADLESS_UI rows.
- Four interpreter ISA correctness bugs (rotate mapping, SCAS operand, 8-bit
  register reads, ROR CF) + two ABI bugs (data exports, arity table) fixed in
  one wagon — all found by disciplined crash archaeology on real UPX-packed
  NirSoft tools, all verified regression-free on the canonical corpus.
