
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
