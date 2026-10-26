
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
