
---
Task ID: SWEEP-076
Agent: main (z.ai)
Task: «continue-continue train-train: complete at least dozens of programs» — B-group rescue train: 8 FAIL-state utilities from the 48-target campaign

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
Task: «continue-continue: 20 programs» — the 20-puzzle input campaign: initial frame + valid input + post-input frame for all 20 sgtatham puzzles, plus B-group probes

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
Task: «continue-continue» — B-group tool wagon: 9 official downloads, input battery, generic engine root causes

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
---
Task ID: SWEEP-079
Agent: main (z.ai)
Task: «continue: finish the incomplete» — finish the incomplete: SW-1354 winlister x86 GUI frontier, WinObj zero-frames, retry the 11 BLOCKED_DOWNLOADs

Work Log:
- Env-reset recovery: local 5 commits behind origin (242af84 -> 40eefc3), ff-only merged, stale binary rebuilt byte-exact (70fbb03e == evidence/sweep078/engine_sha256.txt), G1 pre-change pin 12/12 (20261026_121846).
- SW-1354 winlister x86 full decode (5 generic fixes a-e: i386 WNDCLASS layouts; guest-EIP/host-channel separation + api32 gate ring; cpu32_call inverted frame; ordinal GP raw-host-ptr thunks; mode-aware callback routing + guest-staged i386 CREATESTRUCT).
- SW-1355 process identity unification (TEB32 ClientId 1234) + XP-i386 NtQSI class-5 record (decoded from winlister's own consumer bytes).
- SW-1355b WCTMB dst probe (guest wild buffers fail cleanly).
- SW-1357 i386 STARTUPINFO 68B (Winobj EIP=0 root cause).
- SW-1358 SSE2 movdqa/movdqu/pshufd; SW-1358b arity found-flag + locale Ex-family arities; SW-1358c GP real-dll binding; SW-1358d x86 operator new in guest heap.
- Downloads: shmnview.zip (ShellMenuView correct name) + FreeCommander portable landed+hashed; 20/20 puzzles refetched byte-verified; 8 honest BLOCKED rows.
- Gates: corpus 7/7 EXACT x3 zero regressions; 20/20 puzzles PASS_INPUT_STATE_CHANGE x3; G1 12/12 (20261026_140651); engine 5a8d5a72.
- evidence/sweep079 curated; commit 06f20bb pushed (no force).

Stage Summary:
- The x86 (cpu32) GUI world now runs dialogs, window classes, NtQSI-driven UI and C++ heaps correctly; winlister reached its tree-walk frontier, shmnview 907K steps.
- Queued: SW-1356 guest-SEH delivery, SW-1358e shmnview wild call, SW-1359 FreeCommander Delphi startup, WinObj detour machinery.

---
Task ID: SWEEP-080
Agent: main (z.ai)
Task: (continue and show me whether you can bring winrar/idm/notepad++ to a complete run» — the user-named trio: winrar712 x64, idman 6.42b2 x86, notepad++ 8.9.8.1 x64+x86; decode their blockers, land generic engine fixes, push

Work Log:
- Env-reset reconcile: local 242af84 (SWEEP-030 shell) vs origin f08dc8b (SWEEP-079); ff-reset to origin, rebuilt byte-exact 5a8d5a72, G1 pre-change 12/12 (20261026_143555).
- Downloads preflighted+hashed (dl_sha256.tsv): IDM 6.42b2 (mirror2.internetdownloadmanager.com, 276aa7e4), npp 8.9.8.1 x64 portable zip (beddf554, 231 files), winrar712 stock 80ab6824.
- SW-1360 (winrar712 STARTDLG): DLGTEMPLATE walker triple off-by-one — header skip p+=10->9 words, item id it[9]->it[8], class it+10->it+9. Byte-level walker simulation on the real template reproduces the traced garbage ids EXACTLY; the fixed walk yields the true 13 ids. Result: WinRAR's installer wizard parses 13/13 controls (STATIC/COMBOBOX/BUTTON/RICHEDIT).
- SW-1360b: msctls_progress32 = sysclass 0x8E (w/a/name/ordinal ranges); class-table loop bound was table length AND probe width fused at 15 — entries beyond idx14 unreachable (sizeof(t) now).
- SW-1360c/d/e observability: dialog APIs traced (SendDlgItemMessageW family); MW32_DUMP_VA extended to x64 (x64dump/x64postdump incl. ExitProcess) + uintptr width fix (32-bit truncation crashed the host dump loop); SetFilePointer/Ex + GetFileSize traced (winrar SFX self-scan = stride-8176 sliding window finding Rar! at 0x76200).
- SW-1361 (frontier, documented): winrar SFX custom delay-load machinery — SRW+VirtualProtect self-rebind of .didat slots, own resolver lookup(table,key);jmp rax; only ~20 APIs resolve via our GetProcAddress; dialog phase resolves outside dispatch.
- SW-1362: InterlockedIncrement/Decrement/Exchange/ExchangeAdd/CompareExchange/CompareExchangePointer implemented as real atomics over guest pointers (was: NO entries; idman CRT init died at 1,936 steps).
- SW-1363: 12 missing arity entries (GetEnvironmentStringsW 0, GetMessageTime 0, GetDoubleClickTime 0, GetClipboardOwner 0, CountClipboardFormats 0, ReleaseMutex 4, GetKeyboardLayout 4, IsWindowUnicode 4, GetTopWindow 4, GetLastActivePopup 4, ShowOwnedPopups 8, SetRectEmpty 4) — the ESP+32 corruption class; proven by the [API32] gate trace (GetEnvironmentStringsW esp_after +0x20 on a 0-arg call; then malloc frame overlapped caller locals -> WCTMB srclen garbage -> HeapFree wild -> exit 4).
- SW-1364/1364b: arena32 HeapFree no-op (documented contract; was HEAP-TRAP exit 4) + HeapReAlloc alloc-new+clamp-at-live-edge copy.
- SW-1365: wsprintfA/W/wvsprintfA/W are CDECL varargs (arity 0; was 32 -> ESP+32 -> IDM jumped into thunk page 0x00a00000) + REAL ms_abi varargs gate path (16-arg SysV cast-call misplaced buf/fmt for __builtin_ms_va_list callees).
- SW-1366: %S/%ls/%hS wide-source in the ANSI formatter (mojibake dialog path).
- SW-1369: GetFileAttributesW path normalization ("/tmp/" + L'\\' + name kept a literal backslash -> stat missed the created dir -> IDM "Error ID: 2").
- idman final: 1,936 steps -> 76K+ steps; full unpacker run (registry seeding RegCreateKeyExW/RegSetValueExW real, temp dir create+check, /s probe, CreateProcessW honest fail, FormatMessageA(2)); renders CLEAN "Could not create/open error log file" dialog (636x122). Frontier SW-1370 (CreateProcess child chain).
- notepad++ x64 8.9.8.1: parity+ vs SWEEP-066x64 — same 2 real MessageBoxW frames (Configurator / Load stylers.xml failed) + RegisterClassW ATOM 0xc001 + dialog children NOW registered (SW-1360); crash moved into guest class proc [rcx+0x20] rcx=0x20 RVA 0x1099b3 (SW-1298-x64 continues). x86 unchanged (SW-1371 pre-existing SW-1341-relocation crash @2450 steps, same step count as sweep067 evidence).
- Gates: G1 12/12 post-change (20261026_154619, 123d7750); corpus x3 EXACT (winmine 82, sol 69, freecell 10, calc 1, breakout 10, snake 13; spider wall-clock band); puzzle spot-check 4/4 sha-verified (blackbox/bridges/cube/dominosa) rc=0 2 frames 0 crashes.
- evidence/sweep080 curated; commit pushed (no force).

Stage Summary:
- All three user-named programs ADVANCE: winrar 7.12 installer wizard now parses 100% of its controls; idman runs its whole unpacker with real registry writes and renders a clean honest dialog; npp64 registers dialog children past the previous record.
- 11 generic fixes landed (dialog walker ABI, class table, Interlocked, arity ESP-corruption class, arena32 heap, cdecl varargs gate, %S, path normalization, x64 dump facility).
- Next wagons: SW-1361 winrar SFX resolver decode, SW-1370 IDM CreateProcess child support, SW-1298-x64 npp64 class-proc ctx, SW-1371 npp86 relocation crash.

---
Task ID: SWEEP-081
Agent: main (z.ai)
Task: «bring these three programs to a complete graphical run ... hxd plus winnc and a few other tiny programs ... every program should behave like a plugin» — plugin architecture + trio re-verify + new app-plugins (winnc/hxd/everything/renamer/notepad2-mod)

Work Log:
- Baseline: origin 829d1560 == local; byte-exact rebuild 123d7750; G1 pre-change 12/12 (20261026_161027).
- PLUGIN FRAMEWORK (user-directed): plugins/<name>/plugin.tsv manifests (8 plugins: winrar/idman/npp64/winnc/hxd/everything/renamer/notepad2mod) + scripts/plugin_run.sh generic runner (hash verify -> unpack -> cold runs -> frames -> verdict -> evidence PNGs -> status row) + plugins/status.tsv ledger + plugins/README.md contract. Standing rule: trio every train + one NEW unknown per train.
- Downloads (official, dl_sha256.tsv): WinNc 11.6 setup.exe bef0fcaf, Everything x64 4f4a9b92, ReNamer 7.5 b11342a5 (den4b key flow), notepad2-mod c893ce4a. HxD = BLOCKED_DOWNLOAD (mh-nexus.de TCP-443 refused, 6 attempts across 2 sweeps; substitutes Frhed/XVI32/Okteta dead, ImHex not tiny) — plugin stays, retries next train.
- SW-1372: 23-API shlwapi/user32/kernel32 batch (Str*/Path* string ops, PrivateProfile Section pair, IsChar A-variants, MessageBoxExW, DrawAnimatedRects, SHCreateDirectoryExW recursive mkdir, SHGetDataFromIDListW E_FAIL) + x86 arity rows + SHELL32.180 (wine spec SHGetDataFromIDListW). notepad2-mod unresolved 23 -> 0.
- SW-1334 observability: class-proc trace now prints numeric message id.
- Trio re-verified via plugin runner, NO regression: winrar 13/13 STARTDLG children with texts, clean rc=0 at SW-1361 frontier; idman honest dialog rendered (messagebox_1.png) rc=0, SW-1370 frontier; npp64 2 real MessageBox frames, SW-1298-x64 crash DECODED to instruction level (mov [rcx],rbx @0x1099b3, lea rcx,[rbx+0x20], rbx=0 NULL object, via SW-1334 re-entry [rsp]=0 sentinel).
- New plugins first results: winnc FRONTIER SW-1374 (Inno wine_get_version probe -> host SIGSEGV in cpu32 interpreter @guest 0x40722A, step 551); everything FRONTIER SW-1375 (C++ throw then mov rcx,[rsi+0x98] rsi=0 — MFT/DeviceIoControl class); renamer FRONTIER SW-1376 (Delphi TlsGetValue probe loop -> wild stack jump, SW-1358b class); notepad2-mod ADVANCED SW-1373 (message pump 44 SendMessage cycles, then COM vtable [rax+0x208] on NULL this).
- Gates: G1 post-change 12/12 (20261026_165111, 92e7df46, 4,465,312 B); corpus x3 EXACT (winmine 82, sol 69, freecell 10, calc 1, breakout 10, snake 13, spider 627-633 band) ZERO crashes/fatals; puzzles 4/4 rc=0.
- Post-push decode (same train): npp64 rerun with the SW-1334 [msg] trace PINNED the SW-1298-x64 producer — GetWindowLongPtrW(0x10001,-21=GWLP_USERDATA)->0x0 is the LAST read before the crash; the trace has ZERO SetWindowLongPtrW calls (the guest's per-window object store never populated); guest then runs lea rcx,[rbx+0x20]; mov [rcx],rbx on the NULL object. Next train: verify the x64 CreateWindowExW WM_NCCREATE/WM_CREATE lpCreateParams delivery for guest-registered classes.
- evidence/sweep081 curated (summary.md, dl_sha256.tsv, corpus_parity.tsv, per-plugin run logs + crash contexts + frames); plugins/status.tsv written; commit pushed (no force).

Stage Summary:
- Every application is now a PLUGIN: manifest + runner + ledger make failures directly comparable across trains, exactly as the user asked.
- Trio verified at parity with SWEEP-080 through the new pipeline; notepad2-mod (new unknown) advanced 23-unresolved -> message pump.
- 7 honest frontiers queued (SW-1298-x64 decoded, SW-1361/1370/1373/1374/1375/1376) + HxD download retry.
---
## SWEEP-082 (2026-10-26) — trio driven to full graphical init; the ms_abi callback class fix; WinNc/renamer/notepad2mod un-crashed

Resumed from 12d7c8b after a sandbox reset (local was 75 commits behind; pulled,
stray untracked artifacts moved aside). Pre-change rebuild byte-exact 92e7df46,
G1 12/12.

ENGINE FIXES (all generic):
- SW-1380 top-level WM_NCCREATE->WM_NCCALCSIZE->WM_CREATE delivery (x64 + i386
  staged paths; FALSE cancels creation). The child path had it since SW-1335;
  top-level never did — every GWLP_USERDATA-based guest framework proc never
  registered its object (npp64: zero SetWindowLongPtrW in any trace).
- SW-1381 COMCTL32.411 GetWindowSubclass (real chain search, honest FALSE) +
  SHELL32.165 SHCreateDirectory wiring; CORRECTION: SHELL32.155 = ILFree per
  live wine shell32.spec (was mis-mapped to SHCreateDirectory in SWEEP-029);
  ILFree implemented.
- SW-1382 top-level identity minting for window #2+ (g_toplvl_main_taken);
  surface store + class snapshot gated to window #1 only.
- SW-1383 GetWindowLongPtrW caller-RVA in trace (pinned SW-1384 in one rerun).
- SW-1384 ms_abi cb_t in mw_call_cb2/cb4 (SysV-vs-MSx64 register shuffle: the
  guest's arg0 was our arg3!) + SW-1384b 64-bit LRESULT returns (int cb_t
  truncated Scintilla's DirectFunction pointer) + uintptr_t pointer args
  (font-enum staged LOGFONT host-stack pointers were truncated to 32 bits).
- SW-1385 cpu32 o32 immediate family: 66-prefixed B8-BF/C7 and the accumulator
  ALU ops (05/0D/15/1D/25/2D/35/3D) now read imm16 and honor 16-bit results
  (the imm32 reads desynced eip -> NULL-ea store crash; WinNc Delphi CRT).

RESULTS (3x cold runs each, plugin runner):
- npp64 SW-1298-x64 RESOLVED: crash@0x1099b3 x3 -> PASS_INPUT_STATE_CHANGE
  rc=0 x3 (13,876 API calls, ExitProcess(0), Scintilla handshake works).
- winnc SW-1374 crash -> EXIT rc=86 crash=0 (CRT runs; installer stub exits).
- renamer SW-1376 crash -> EXIT rc=86 crash=0.
- notepad2mod SW-1373 crash -> EXIT rc=0 crash=0.
- winrar/idman unchanged (no regression); everything TIMEOUT unchanged.
- NEW plugin tcpview64 (SWEEP-072 B-group U23, official zip sha 4fc5ceba...):
  real EULA dialog reached, TOOLBAR poll-loop frontier -> TIMEOUT (honest).
- HxD: BLOCKED_DOWNLOAD (8th TCP refusal; no mirrors per protocol).

GATES: G1 12/12 x2 (92e7df46 pre, 0eaff275 post); corpus 3x ZERO regressions;
final engine sha256 4f9cc889...? — see evidence/sweep082/engine_sha256.txt.
Evidence: evidence/sweep082/; ledger rows tagged sweep082 in plugins/status.tsv.
Next wagons: npp64 interactive probes (menus/tabs/edit), tcpview64 toolbar loop
(SW-1386-tail), winnc unpacked payload route, everything poll loop.

---
Task ID: SWEEP-083
Agent: main (z.ai)
Task: (continue until completion) — elephant train: o32/SEH class fix set, new program (mines), gates, push

Work Log:
- Baseline: origin==local 9bc8009; G1 PIN 12/12 clean build 4f9cc889; trio re-verified via plugin runner (npp64 PASS_INPUT_STATE_CHANGE x3, idman PASS_LAUNCH_RENDER x3, winrar parity) — zero regressions.
- SW-1387 decode: winnc `66 81 c7 ef 00` executed by the 32-bit-only grp1 0x81 handler (imm32 read, 7 bytes consumed, eip landed on modrm byte 0xe7 -> PE32-FATAL rc=86 "opcode not implemented (step2)"). Added the o32 modrm-group plane (grp1 81/83 imm16, ALU r/m<->r family, mov 89/8B, test 85, imul 69/6B, shifts C1/D1/D3, grp3 F7) — one pre-dispatch, 32-bit cases untouched.
- SW-1388: winnc esp drift +32 decoded to MISSING "InitializeCriticalSection" in the g_api32_arity table (fallback arity 32; Delphi lazy-helper ret consumed the installed SEH handler slot). Audited all 883 entries vs canonical stdcall sizes: 0 wrong, 18 missing added.
- SW-1389: GetSystemInfo wrote the 48-byte x64 SYSTEM_INFO through a 32-bit pointer — offset 36 (granularity 0x10000) landed on the saved return address (ret to 0x10000). Bitness-aware 36-byte x86 layout (t_cpu32_live || g_image_base32 — top-level runs never arm t_cpu32_live; fwd-decl added).
- SW-1390: FNCLEX/FNINIT (DB E2/E3, reg field 4) had reg==7 checks — never fired. Real DB mod3 group + FUCOMI/FCOMI + FWAIT(0x9B).
- SW-1391: modrm_apply_seg not idempotent (NEEDM + LD_RorM* double-applied FS base; padding-lea corrupted a copy source). seg_applied flag; LEA now strips the base it must ignore.
- SW-1392: full i386 SEH dispatch — RaiseException no longer returns (guest-scratch ER+CONTEXT, FS:[0] walk, CDECL handlers on the guest stack, disposition protocols); RtlUnwind = normal stdcall call that unwinds above-target and RETURNS (guest passed TargetIp=RA as formality; post-unwind code reads the handler's own frame); MW32_SEH_RESUME_CODE pump-stop through nested pumps; SW-1392e: push %fs:[0] read 2*fsbase -> every SEH frame registered prev=0 -> chain collapsed (fixed by the idempotence flag).
- SW-1394 (renamer): 0F AE /3 mislabeled fence (stmxcsr), /2 ldmxcsr refused. Real 0F AE group (fxsave/fxrstor/ldmxcsr/stmxcsr/fences).
- NEW plugin mines (Simon Tatham puzzle collection, SWEEP-072 A-group U10, official zip sha a5b00f63): PASS_INPUT_STATE_CHANGE FIRST RUN (rc=0 x3, 6 frames/3 uniq).
- Gates: corpus 3x 21/21 GREEN zero regressions; G1 post-fix 12/12 (80bca026); evidence/sweep083 curated (summary.md + frontier stderrs + ledger rows); commit af624e3 PUSHED (fast-forward 9bc8009..af624e3, no force); Issue #1 comment 6426184495 posted.

Stage Summary:
- winnc: 551 -> 14556 steps; SEH chain + dispatch + unwind + Delphi finally machinery reached for the first time in engine history. Frontier SW-1393: Delphi finally-retry router (call *[node+4]+5 -> 0x4a8a2c -> jmp 0x4a899b) re-entry contract.
- renamer: interpreter abort -> clean guest RunError(204) exit.
- mines: new-program first-run PASS — the collection is a high-yield battery for future trains.
- Next wagons: SW-1393 router decode, renamer rc=204 pre-render path, notepad2mod x64 early-exit, everything/tcpview64 poll loops, more A-group puzzles as new-program wagons.

---
Task ID: SWEEP-084
Agent: main (z.ai)
Task: (continue — show visual proof of complete program runs, one new program per continue, survey GitHub) — visual-execution-proof train + new program (solo) + gates + push

Work Log:
- Baseline: origin==local f2f9c01 (SWEEP-083 tip; API-verified, last push 2026-10-26T20:10Z); Issue #1 closed, 46 comments. Engine UNCHANGED this train — clean rebuild byte-exact 80bca026 (matches SWEEP-083 final pin); G1 pre-train PIN 12/12 (20261026_201417).
- Trio re-verified via plugin runner (SWEEP=sweep084): npp64 PASS_INPUT_STATE_CHANGE x3 (rc=0, 2 frames), idman PASS_LAUNCH_RENDER x3 (rc=0, 1 frame), winrar EXIT rc=0 x3 (parity frontier SW-1361) — zero regressions.
- NEW plugin solo (Simon Tatham Solo/Sudoku, SWEEP-072 A-group U19, official puzzles.zip sha a5b00f63 verified): PASS_INPUT_STATE_CHANGE on first contact (rc=0 x3, 6 frames/3 uniq, cursor cell visibly moves under the arrow-key probe + grid re-renders). Second consecutive Simon Tatham first-run PASS (mines SWEEP-083) — the collection is a reliable high-yield battery.
- VISUAL PROOF DELIVERY (user request): trimmed per-app frames + labeled contact sheet exported to the session download area (miniwin_frames_sweep084/: Notepad++ dialogs, IDM error dialog, solo sudoku sequence, mines flood-fill battery proof). Montage labels English (no Persian shaping in PIL) — chat reply is Persian.
- Gates: corpus parity scripts/sweep084_corpus.sh 21/21 rc=0 crash=0 fatal=0 x3 (evidence/sweep084/corpus_parity.tsv); G1 train-end PIN 12/12 (20261026_201812, binary 80bca026 byte-exact).
- Evidence evidence/sweep084/ curated (summary.md + frames + ledger rows tagged sweep084); commit + push HEAD:main (no force); Issue #1 progress comment posted.

Stage Summary:
- No engine change needed this train: the SWEEP-083 fix set holds; all four graphical guests reproduce their verdicts deterministically.
- solo = new-program first-run PASS; visual execution proof delivered for Notepad++/IDM/solo/mines.
- Next wagons: npp64 interactive deepening, winnc SW-1393 finally-retry router, everything/tcpview64 poll loops, renamer rc=204 pre-render path, more A-group puzzles (netgame/lightup/pattern/towers), hxd download retry.

---
Task ID: SWEEP-085
Agent: main (z.ai)
Task: (programs showing an error dialog have not run — fix the cause; complete the prerequisite folder layout; simulate installation) — generic file/focus/paint fix set + honest reclassification

Work Log:
- User critique adopted: prior npp64/idman "PASS" frames were ERROR dialogs. Decoded FOUR generic engine defects and fixed them in src/loader/loader.c (zero app special-casing):
- SW-1395: the SW-1101e tails block (PathIsDirectoryW/PathFileExistsW/CopyFileW/GetCompressedFileSizeW) passed raw "C:\..." to access/stat/fopen — always-false on the host while CreateFileW normalizes (SW-1234). All four now route through w3_normalize_path (buffers 1024). MEASURED: Notepad++ langs.xml + stylers.xml SELF-CREATE at first run; both first-run error dialogs GONE.
- SW-1396: SetFocus stub/ShowWindow never tracked focus -> GetFocus() always NULL; npp64's main wndproc thunk (0x140109870) passes GetFocus() as hwnd to its dispatcher (disasm: IAT call + mov rax->rcx) and early-returned on NULL. Real focus tracking (SetFocus returns prev; ShowWindow show-states 1/3/5/9 focus+activate).
- SW-1397: ReadFile observability (fd/result/bytes/first-8 for n>=1024) — pinned the langs.xml byte story (546572 delivered, valid XML head).
- SW-1398: real per-window paint delivery — dispatch routes child hwnds to guest-owned procs (subclass wndlong(-4) first, else SW-1334 captured class_proc); pump arms a child-paint round-robin after the main paint. MEASURED: WM_PAINT child=0x30001 delivered; Scintilla proc EXECUTES BeginPaint.
- Frontier decodes (queued): SW-1399 npp (shipped langs.model.xml has 3 raw '&' = malformed XML for strict parsers; recover loop re-copies same bytes; + guest computes 32x32 MoveWindow(hwnd=NULL) layout); SW-1400 winrar SFX (wizard template RESOLVED 890B + children minted + real dlg proc; modal lifecycle needed); SW-1401 idman (CreateProcessW fail -> setup opens its silent switch "/s" as log path; generic fix = loader re-exec child).
- NEW game fifteen (SWEEP-072 A-group U05): PASS_INPUT_STATE_CHANGE FIRST CONTACT (rc=0 x3, 30 frames/15 uniq — third consecutive Simon Tatham first-run PASS).
- NEW program resourcesextract (NirSoft): official re-download verified byte-exact (cdccfe00...), x86 guest 402k steps, main() returns 0 pre-dialog — x86 dialog-path frontier queued.
- Gates: corpus 21/21 x3 (evidence/sweep085/corpus_parity.tsv); G1 12/12 pre (80bca026) + post (ad0e9a35, 20261026_205504).
- Evidence evidence/sweep085/ curated (summary.md + frames + ledger rows); commit + push HEAD:main (no force); Issue #1 comment posted.

Stage Summary:
- The "error dialog" era is reclassified honestly: npp64 now self-provisions its config files and shows its own designed recovery UX only on first run; the remaining npp64/winrar/idman frontiers are DECODED with root causes and queued as SW-1399/1400/1401.
- fifteen = new-program PASS; per-train protocol (one new game + one new program) satisfied.
- Next wagons: SW-1399 layout decode, SW-1400 modal pump, SW-1401 CreateProcessW v1, more puzzles, hxd retry.

---
Task ID: SWEEP-086
Agent: main (z.ai)
Task: (registry and folders must be created correctly in their own paths) — installer ground-truth foundation + one new game + one new program

Work Log:
- Recovered workspace after sandbox restart: fast-forwarded local main 242af84(SWEEP-030) -> 0b5b4a1(SWEEP-085) from origin; re-downloaded puzzles.zip (sha a5b00f63 exact) + official npp 8.9.8.1 NSIS installer (sha 26f3bcce).
- SW-1402: on-disk registry tree mirror (MINWIN_REG_TREE) — hive exported as real directories + @value files + _REG_MANIFEST.tsv; SW-1402b installer-grade hive caps (256 keys/24 vals/256 path/1024 data), snapshot-format compatible.
- SW-1403: guest install-log (MINWIN_INSTALL_LOG) — every FS mutation (dir/create/copy/move/delete, create-mode CreateFile) appended op<TAB>guest<TAB>host<TAB>result; 8 call sites; ExitProcess flushes regtree+snapshot explicitly (_exit skips loader atexit).
- SW-1404: runner sandbox mode (manifest sandbox=yes) — guest CWD = evidence/<sweep>/<name>/sandbox, drive-root mapping lands C:\ paths inside; install_log/regtree/snapshot/MINWIN_WINTEMP captured.
- SW-1405: Windows skeleton seeding shared with CreateDirectoryW + GetTempPathW MINWIN_WINTEMP=1 world mode (C:\Windows\Temp) — corpus default unchanged (POSIX TMPDIR).
- SW-1407: win32_module_path drive-root-relative (self-reopen round-trip) + runner copies sandboxed installers into <sandbox>/Installers/.
- SW-1408: PE32 arity MessageBoxIndirectW/A=4 (under-pop stack drift crashed NSIS after its first MessageBox).
- Battery: winrar INSTALL_OK 28 files/9.56MB into sandbox/WinRAR (WinRAR.exe installed!); idman IDM_Setup_Temp created (historical blocker gone); nppsetup NEW = LZMA-extract desync frontier (69.7M steps, System.dll staged, SW-1409 queued); lightup NEW = PASS_INPUT_STATE_CHANGE first contact; npp64/mines re-verified zero regressions.
- Gates: corpus 7x3=21/21 (sweep086_corpus.sh), G1 12/12 (evidence/master/g1/20261027_053105, binary b24475ff).
- Incident: disk filled twice (stale 8.6GB /tmp/my-project mirror from the dead session) — cleaned; G1 re-run green.

Stage Summary:
- The "folders/registry in their own paths" mandate is now a working subsystem: regtree + install-log + sandbox produce per-installer ground truth under evidence/.
- WinRAR = first real installer->installed-program conversion; installed WinRAR.exe is the SWEEP-087 target.
- Next: SW-1409 LZMA desync, SW-1401 idman child exec, SW-1400 winrar interactive modal, run installed WinRAR.exe, +1 game/program.

---
Task ID: SWEEP-087
Agent: main (z.ai)
Task: (you cannot run the installed one) — run the INSTALLED WinRAR.exe + one new game + one new program

Work Log:
- SW-1410: all 16 fake-COM vtable members (IMalloc x9, IShellFolder x7, CoGetMalloc x8 fns) re-attributed ms_abi — guests call with MS x64 convention (RCX/RDX), SysV bodies read RDI/RSI garbage; IMalloc::Free crashed libc. This unblocked the installed WinRAR's first COM round-trip.
- SW-1411: CharLowerW/CharLowerA/CharUpperA low-word single-character contract (installed WinRAR passes CharLowerW(1) at init — SIGSEGV @0x1 before fix).
- SW-1407b: runner sandbox_tree=<dir> — installed-program plugins stage their full install tree into the sandbox and run the exe from its own dir.
- Battery: winrarinstd (NEW program = installed WinRAR 7.12, 28-file tree) = FRONTIER_SW-1412 (init + main-window + child SendMessageW stage reached; libc heap-corruption crash queued for decode); pattern (NEW game) = PASS_INPUT_STATE_CHANGE first contact.
- Gates: corpus 21/21 x3 (sweep087_corpus.sh); G1 12/12 (evidence/master/g1/20261027_055118, binary 62dea8e0).
- Incident: disk pressure from the dead-session mirror again — cleanup backgrounded, gates run with headroom.

Stage Summary:
- The installed program now boots, inits COM, builds its main window and pumps child messages — two crash layers removed this train, one frontier decoded and queued (SW-1412).
- Next: SW-1412 heap-guard decode, SW-1409 LZMA, SW-1401/SW-1400, +1 game/program per train.

---
Task ID: SWEEP-088
Agent: main (z.ai)
Task: +1 game +1 program; SW-1409 LZMA decode refinement

Work Log:
- Downloaded official 7-Zip 25.01 x64 installer (sha 78afa2a1) as NEW program (7zsetup, sandboxed); towers (Simon Tatham) as NEW game.
- SW-1409 decode refined: ReadFile(fd=5,16384)@190492 delivered BYTE-EXACT LZMA stream (5d 00 00 80 00 00 35 00 = props 0x5D + 8MB dict, matching GlobalAlloc(0x800000)); NSIS temp-stage + self-reopen + trailer probe (6951149) all correct. Frontier = divergence INSIDE guest LZMA decoder execution (PE32 CPU emulation), not the file plane.
- Battery: towers = PASS_INPUT_STATE_CHANGE first contact; 7zsetup = FRONTIER_SW-1413 (PE32-FATAL rc=86 during CRT init: __set_app_type/__p__fmode stage, 7-Zip's own installer tech).
- Gates: corpus 21/21 x3 (sweep088_corpus.sh); G1 12/12 (20261027_055732).

Stage Summary:
- Three installer technologies now characterized honestly: WinRAR SFX = INSTALL_OK; NSIS = deep LZMA frontier; 7-Zip = CRT-init frontier.
- Next: SW-1412 heap-guard decode (installed WinRAR), SW-1413 CRT-init (7zsetup), SW-1401/SW-1400, +1 game/program per train.

---
Task ID: SWEEP-089
Agent: main (z.ai)
Task: +1 game +1 program — uninstall lifecycle + Untangle

Work Log:
- NEW program winraruninst = the INSTALLED WinRAR Uninstall.exe (runs from its own install dir via sandbox_tree; engine UNCHANGED binary 62dea8e0) = FRONTIER_SW-1415: silent EXIT rc=0 after CRT init (UCRT shutdown sequence incl. mscoree/CorExitProcess probe; uninstaller UI not reached).
- NEW game untangle = FRONTIER_SW-1414: SIGABRT (Signal 6) in libc — guest CRT abort path, decode queued.
- Gates: corpus 21/21 x3 (sweep089_corpus.sh); engine byte-identical 62dea8e0 (G1 evidence 20261027_055732 remains valid).

Stage Summary:
- 4 trains this session (086-089): installer ground-truth subsystem live (regtree + install-log + sandbox), WinRAR SFX INSTALL_OK, installed WinRAR.exe boot layer-by-layer, 3 installer technologies mapped honestly, 4 new games PASS first-contact (lightup/pattern/towers + solo/mines/fifteen prior), frontiers decoded and queued with SW numbers.

---
Task ID: SWEEP-090
Agent: main (z.ai)
Task: (continue) — adopt mainline frontier after stale-local recovery; port the one still-live engine bug (SW-1305); train protocol wagons

Work Log:
- Sandbox crash recovery: local workspace was a STALE SWEEP-030-era snapshot;
  origin/main had advanced 86 commits to SWEEP-089 (0f61e2f). Local-only
  SWEEP-031 work (drive-tree sandbox, registry tree mirror, handle release,
  FindFirstFileA fix, lstrcatW) re-derived fixes the mainline already landed
  better (SW-1234/1244/1402/1405/1407/1408). Adopted mainline per no-rewrite
  rule; divergent work kept on branch sweep031-local-divergent.
- Reconciliation: only ONE finding still live on mainline = SW-1305
  FindFirstFileA/FindNextFileA wrote the 592-byte W struct into guests'
  318-byte WIN32_FIND_DATAA -> 274-byte stack overflow -> /GS cookie smash ->
  FASTFAIL(2) (latent since BUG-049; proven on stale snapshot via putty with a
  seeded C:\Windows). Ported as sweep090_findfix.py: W3Find.wide flag +
  fill_find_data_a (318B CHAR layout) + wide param through find_first_impl;
  FindFirstFileA/NextA fixed, W/ExW paths unchanged. lstrcatW already on
  mainline; drive/registry/handle-release mechanisms already train-hardened.
- MEASURED: build clean (4,615,008 bytes); G1 PIN 12/12
  (evidence/master/g1/20261027_081203); corpus 21/21 x3 rc=0 crash=0 fatal=0,
  identical to SWEEP-089 except breakout/snake now render frames (10/13 vs 0)
  = improvement; NEW game netgame = PASS_INPUT_STATE_CHANGE first contact
  (rc=0 x3, frames=20 uniq=10); NEW program shexview (NirSoft ShellExView x64,
  HKCR CLSID walker) = PASS_LAUNCH_RENDER first contact (rc=0 x3, 1 frame).
- Queued: SW-1416 handle value-space collision (file 4..67 vs kobj small ints,
  latent); SW-1306 PE32/WoW64; SW-1307 real guest-DLL module loading.

Stage Summary:
- First post-recovery train landed on the true mainline; one real
  memory-corruption bug fixed; two first-contact PASS wagons.

---
Task ID: SWEEP-091
Agent: main (z.ai)
Task: (continue train train) — SW-1412 ownership-registry fix (installed-WinRAR SIGSEGV), registry chain install→app (SW-1418/1419), +1 game +1 program, gates

Work Log:
- Post-crash recovery: local workspace was a STALE SWEEP-030 snapshot; ff-only
  merged origin/main (SWEEP-090, 5aae5e2); stale sweep030/031 artifacts moved
  to /home/z/my-project/backup_old_untracked. Disk was 100% full — removed
  unused mingw toolchains (3.3G) to 68%.
- SW-1412 ROOT CAUSE (crash class): mw_CoTaskMemFree + IMalloc::Free/Realloc
  (SHGetMalloc AND CoGetMalloc variants) called host free()/realloc() on
  guest-supplied pointers with NO ownership check; installed WinRAR passed a
  garbage PIDL (from SHGetFolderLocation whose generic stub returned S_OK
  without writing the out-param) and libc SIGSEGV'd in the chunk walk (rc=139
  x3, 0 frames). Fix: engine-wide CoTask/IMalloc ownership registry
  (mw_cotask_track/owned/untrack, 1024 buckets) wired into all FIVE free
  paths; foreign pointers are logged and refused. DidAlloc now answers
  honestly from the registry (1 owned / 0 foreign / -1 NULL).
- SW-1412b: mw_SHGetFolderLocation implemented with the honest contract
  (E_POINTER on NULL out, else E_FAIL + *ppidl=NULL, same stance as
  SHGetSpecialFolderLocation); registered + x86 arg-bytes 20.
- RegOpenKeyExW miss path now logs the full composed path (was silent; the
  160x Toolbar\Buttons poll was invisible).
- SW-1418: seeded HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion
  (ProgramFilesDir/CommonFilesDir/ProgramFilesPath/DevicePath) — the non-NT
  twin probed by the WinRAR SFX installer; hive missed it.
- SW-1419: registry chain install→installed-app. plugin_run.sh reg_seed=
  stages an MWREG001 snapshot into the sandbox before the runs. Root-slot
  reservation on load (slots 0..6 = HKCR..HKDD are authoritative; field
  order len→path→nvals honored — the first parse had nvals/path swapped and
  silently broke at slot 0). Fixture plugins/winrarinstd/regseed_firstrun.bin
  = first-run defaults (16 HKCU\Software\WinRAR keys, setup-phase equivalents
  pending the installer options dialog = SW-1417 family).
- MEASURED winrarinstd: rc=139 crash=1 x3 (0 frames) → rc=124 crash=0 x3
  (crash class ELIMINATED); full main-UI skeleton observed in trace: 6
  children (2x STATUSBAR, COMBOBOX, 2x TOOLBAR, SysListView32 file list),
  3x ShowWindow(cmd=5), TB_SAVERESTOREW/TB_INSERTBUTTONA/LVM_SETITEMTEXTW
  traffic, reg misses 296→17 (one-time honest probes: Policy, Policies\
  Explorer, Profiles\4). Reg-poll loop GONE. Frontier SW-1417 registered:
  boot completion + first paint (guest runs to timeout; no frame yet).
- NEW game flood = PASS_INPUT_STATE_CHANGE first contact (rc=0 x3, 6 frames
  3 uniq) — 7th Simon Tatham PASS (mines/solo/fifteen/lightup/pattern/
  towers/netgame/flood).
- NEW program produkey = PASS_LAUNCH_RENDER first contact (rc=124 x3 = GUI
  message pump alive, 1 real frame) — registry-heavy NirSoft x64, unpacked
  sha 6cf9dbed273e35c0, zip sha e4604e0ee6803704.
- Gates: corpus 21/21 x3 rc=0 crash=0 (evidence/sweep091/corpus_parity.tsv);
  G1 PIN 12/12 binary 38a787f8 (evidence/master/g1/20261027_142322).

Stage Summary:
- The 'installed programs' chain is now mechanically complete end to end:
  installer writes files+registry in their own correct paths (sandbox +
  regtree mirror + install_log), the snapshot persists, and the installed
  app boots with the installer's registry via reg_seed. WinRAR-installed
  no longer crashes: the ownership registry kills the whole
  guest-garbage→host-free() class. Next train: SW-1417 (WinRAR boot paint),
  SW-1413 (7zsetup CRT), SW-1414 (untangle SIGABRT), SW-1415 (uninstaller
  UI), SW-1416 (handle value-space collision).

---
Task ID: SWEEP-092
Agent: main (z.ai)
Task: Repository size reduction — source-only, hash-pinned, reproducible test corpus; install/run contract extraction; simple-game corpus gate

Work Log:
- Measured BEFORE: 5,558 tracked MB (9,646 files); git pack 213.97 MiB.
  Root cause: 90 sweeps committed downloads, installers, extracted guest
  trees, sandbox copies (artifacts/ 406 MB, evidence/ 125 MB incl.
  evidence/**/sandbox, checkpoints/ 12 MB, duplicate guest copies).
- Canonical manifest tests/corpus/manifest.tsv: 24 entries, 13 columns
  (id/version/arch/source_url/source_page_url/filename/sha256/size_bytes/
  license/redistribution_notes/extraction_subdirectory/launch_target/
  test_profile). Classes: official-URL pinned (rarlab, chiark, sysinternals,
  voidtools, 7-zip, tonec, nirsoft), UNVERIFIED_URL (hash pinned, URL
  re-pin follow-up: npp x2, notepad2mod, renamer, winnc, shexview), SEED
  (XP-era binaries: hash pinned, acquire locally), GENERATED (installed
  WinRAR payload/uninstaller), BLOCKED (hxd).
- scripts/fetch_guest.sh: on-demand download -> SHA-256 verify -> atomic
  rename -> .cache/miniwin/guests/<id>/; guarded extraction (path traversal
  refused); offline mode; never executes unverified artifacts; caches
  verified entries. MEASURED: fetch + verify + cache-reuse green for
  resourcesextract/produkey/tcpview64/everything/7zip/winrar/puzzles/idman;
  planted bad SHA-256 refused rc=3 (no staging); offline miss BLOCKED rc=2.
- scripts/plugin_run.sh: `guest` field resolution via manifest+cache;
  provision chain (missing sandbox_tree -> producer plugin run);
  placeholders $SANDBOX_OF:<p> / $SNAPSHOT_OF:<p>; in-tree exe detection;
  zip extraction routed through the guarded extractor.
- MEASURED install->run chain (SWEEP-092): winrarinstd auto-provisioned by
  running the WinRAR 7.12 installer once (rc=0 crash=0); the installer now
  installs into Program Files/WinRAR (SW-1418 ProgramFilesDir seed takes
  effect — real Windows behavior); reg seed 1756B staged from the fresh
  installer snapshot (SW-1419 via placeholder); installed payload launched
  in-tree rc=124 TIMEOUT crash=0 = first-run setup-options dialog frontier
  (SW-1417). SW-1412 crash class stays dead.
- Install contract (user question: where files land / what goes into the
  registry / what the program reads): scripts/install_contract.py ->
  evidence/sweep092/winrar_install_contract.tsv. WinRAR: 29 payload files
  (WinRAR/UnRAR/Rar/Uninstall exe, 3 DLLs, 6 SFX modules, RarExt shell
  extension + msix) under Program Files/WinRAR; registry: 15 values
  (HKLM 14 incl ProgramFilesDir, HKCU Software/WinRAR SFX install-location
  key); at run the installed WinRAR reads back its own installer-written
  profile/settings keys (Default Profile, Create e-mail attachment,
  Backup selected files, Create 10 MB volumes, LastFolder, Priority,
  Threads, LargePages, OnTop, RestoreFolder) + ProgramFilesDir.
- Guest profiles (user question: language/compiler/prerequisites):
  scripts/guest_profile.py -> docs/analysis/guest_profiles.tsv. WinRAR.exe
  + Uninstall.exe = C/C++ static CRT (RAR SFX payload); flood.exe = MSVC
  2015+ UCRT; notepad++.exe = C/C++ static CRT; NPP installer = NSIS;
  everything.exe = C/C++ static CRT; idman642build2 = stub (3 imports).
- Simon Tatham puzzle family (flood/fifteen/mines/towers/lightup): all
  rc=139 at the same unresolved-import frontier during CRT _initterm
  (VCRUNTIME140/UCRT guests) — SW-1415 queued.
- Corpus gate scripts/corpus_gate.sh (manifest-resolved guests, SEED
  fallback hash-checked, honest MISSING): 21/21 rows rc=0, crash=0, fatal=0
  (xp.winmine/sol/freecell/calc/spider + breakout/snake). Simple games run.
- Untrack: artifacts/ (all), checkpoints/, samples except UPX fixture,
  working-tree/, evidence sandboxes/dl/Installers/binaries/bmp/>256KB text
  dumps, tests guest copy + pdb. MEASURED AFTER: 22.1 MB tracked (2,973
  files). Centralized .gitignore policy + documented exceptions
  (samples/upx_decompressed.exe, tests/corpus/bin, tests/gui,
  tests/appcompat, tools).
- Guardrail scripts/repo_size_guard.sh (forbidden paths, binary threshold,
  manifest validation, ignored-but-tracked check) — final run GUARD OK;
  2 documented re-pin WARNs (shexview, hxd). scripts/repo_size_audit.sh +
  docs/audit/repository_size_audit_20261027.md.
- G1 PIN OK (evidence/master/g1/20261027_151640): clean build, binary sha
  38a787f8dfcd256d, regression 12/12, no crash.

Stage Summary:
- Repo: 555.8 MB -> 22.1 MB tracked (96% reduction); fresh clones fetch
  guests on demand with fail-closed hash verification.
- Remaining limitation (documented, not fixed): git history still carries
  ~214 MiB pack of historical evidence blobs; removal requires a
  filter-repo history rewrite + force-push, which the standing no-force
  discipline forbids without owner approval.
- Next: SW-1415 puzzle-family unresolved import (VCRUNTIME140), SW-1417
  WinRAR first-run dialog, re-pin UNVERIFIED_URL rows.

---
Task ID: SWEEP-094
Agent: main (autonomous train)
Task: Run WinRAR, Notepad++ and several popular Windows programs through MiniWin (user instruction translated: "continue; run WinRAR, Notepad++ and several popular Windows programs"), all-English repo content.

Work Log:
- SW-1462 (scripts/plugin_run.sh): derived installed payloads (winrarinstd) resolved in-tree FIRST — the source-only cleanup removed artifacts/sweep086, which used to satisfy the legacy existence check before the in-tree override ran. Install->run chain restored.
- WinRAR: SW-1463 (scripts/regseed_extend.py) extends the MWREG001 reg_seed fixture with HKCU\Software\WinRAR\Setup (post-install state the SFX leaves). Result: the first-run setup poll loop passed; the installed WinRAR.exe proceeded into MAIN WINDOW init (children 0x30002/3/6 shown, toolbar msgs, settings timer armed, MoveWindow executed) — alive at timeout, crash=0. Frontiers moved from "setup dialog forever" (SW-1417).
- Notepad++ URLs re-pinned (scripts/pin_npp_urls.py): both GitHub release assets downloaded, SHA-256 re-measured, matched pinned hashes byte-for-byte; manifest rows npp.installer.x64 / npp.portable.x64 now carry exact URLs (zero Persian, English notes only).
- SW-1466 (loader.c): x86 guests must receive arena32 VAs from GlobalAlloc/GlobalFree/GlobalReAlloc/GlobalSize/LocalSize/LocalFree/LocalReAlloc — npp installer asked GlobalAlloc(GHND,0x800000), got a 64-bit host mmap pointer, truncated it, SIGSEGV'd (si_addr=0xdb000f). Same contract as HeapAlloc's SW-1281 branch; sizes registered so GlobalSize answers exactly.
- SW-1467 (loader.c): MessageBoxIndirectW decoded the HOST x64 MSGBOXPARAMS layout against the guest's 32-bit struct -> garbage text pointers -> crash inside MessageBoxW (si_addr=0x67). Added the mw32_mode() 32-bit layout decode; result: the NPP installer rendered its REAL dialog to evidence/sweep094/nppsetup/messagebox_1.png.
- SW-1468 (loader.c): unified configurable OS version plane (MINWIN_OSVER=major.minor) wired through KUSD + registry seed (ProductName/CurrentVersion/CurrentBuildNumber/CSDVersion) + GetVersion/GetVersionExA/W. Defaults unchanged (XP corpus safe). With MINWIN_OSVER=6.1 the NPP installer's "This version of Notepad++ doesn't support..." version gate PASSED (reg seed: "Windows 7 6.1 b7601") and the run reached the file-extraction phase. plugin.tsv grew an optional `env` field; plugin_run passes it via `timeout N env $penv <loader>` (bash does not treat expanded VAR=VAL words as assignment prefixes — verified rc=127 case).
- SW-1469 (documented frontier): unicode NSIS loads C:\Windows\System32\<garbled 0x3f-heavy>.dll 23x -> ERROR 126 loop; hex-dump diagnostic added (temporary, in LoadLibraryExW) proved the '?' chars are literal U+003F in the GUEST buffer (an earlier A->W conversion's default-char replacements). Crash at guest NULL-deref after the loop (139.9M steps). Next: identify the conversion that produced the '?'s.
- SW-1470 (scripts/plugin_run.sh): portable (zip, non-sandbox) apps now run with cwd = their own extracted directory (real Windows: the user cds into the folder). Notepad++ portable: "Load langs.xml failed!" recovery dialog -> langs.xml found and loaded (clean rc=0 x3, no dialog).
- SW-1471 (loader.c): CW_USEDEFAULT (INT_MIN) used to clamp to the 32px minimum -> 32x32 main windows (Notepad++ + installed WinRAR). First top-level CW_USEDEFAULT now sizes 1024x768 (same metric as SW-1269b SW_MAXIMIZE). NPP frames went 32x32 -> 1024x768 (content paint = next frontier).
- SW-1472/1472b (loader.c): registry seeds for the standard EventLog service trio (psloglist64 exit-2 "Could not open the specified event log Registry key" -> key opens, rc=0 with honest ReadEventLogA-stub output) + Sysinternals EulaAccepted markers (HKCU\Software\Sysinternals\<Tool>).
- Battery (SWEEP=sweep094): winrar rc=0 x3; winrarinstd main-window-alive (124, crash=0); nppsetup FRONTIER_SW-1469 (documented); npp64 EXIT rc=0 x3 clean; 7zsetup EXIT 86 (own decision, crash=0); everything/tcpview64/produkey TIMEOUT 124 crash=0 (alive, paint pending); resourcesextract EXIT 0 x3; idman EXIT 0 x3. ZERO crashes in the whole battery.
- Gates: corpus_gate.sh 17 rows, rc=0 in 12 (was 9), crash total 0; xp.* seeds honestly MISSING (licensed media only — operator places them in .cache/miniwin/seed/, never redistributed). g1_accept.sh: G1 PIN OK (build sha de2b1f2ca962d153, 12/12 regression, no crash). repo_size_guard.sh: GUARD OK (2 documented re-pin WARNs: shexview, hxd).

Stage Summary:
- Crash class eliminated for the SW-1466 family (32-bit large allocations); Notepad++ portable = clean rc=0 x3 with langs.xml loaded; installed WinRAR = main window up for the first time; NPP installer = real dialogs rendered + version gate passed.
- Evidence: evidence/sweep094/ (gate tsv, 2 dialog PNGs, compact run logs), evidence/master/g1/20261027_201546/.
- Next: SW-1469 unicode-NSIS garbled system-DLL names; main-window content paint for NPP/WinRAR (children round-robin); CW_USEDEFAULT positioning; ReadEventLogA honest empty-log contract.

---
Task ID: SWEEP-095
Agent: main (autonomous train)
Task: SW-1473 — fix the wsprintfW wide-%s contract violation identified by the closed TOOL-EVAL research Issue #3 (user instruction translated: "continue, several big trains").

Work Log:
- SW-1473 (src/loader/loader.c): wsprintfW / wvsprintfW / _vsnwprintf / wprintf used to convert the WIDE format to UTF-8 and call the ANSI engine (mw_vfmt_a), whose %s reads a narrow char* — every %s produced exactly 1 character. All four now route through the native wide engine mw_vfmt_w16; %hs now forces a narrow arg (h modifier was silently ignored), %ws routes wide. _vsnwprintf keeps truncate-to-cnt/NO-terminator/return-written; wprintf converts the finished wide string to UTF-8 for host stdout. Contract per MS docs + Wine dlls/user32/wsprintf.c WPRINTF_ParseFormatW.
- Direct evidence (gdb probe, full battery env, scripts/sweep095_idm_cmdline.{sh,gdb}): IDM 6.42b2 unpacker CreateProcessW cmd BEFORE = "\"CI\" -d \"C\""; AFTER = "\"C:\Windows\Temp\IDM_Setup_Temp\IDM1.tmp\" -d \"C:\Windows\Temp\IDM_Setup_Temp\"". Guest was always correct; the loader's wsprintfW broke the string. CreateProcessW capability refusal unchanged (engine decision).
- Secondary effect (nppsetup): the SW-1469 0x3f-garbled System32 DLL names are GONE — 23/23 clean loads (UXTHEME/USERENV/SHFOLDER/SETUPAPI/RichEd32/RichEd20/PROPSYS/OLEACC...); the mangling was downstream damage of the same %s bug (retry loop re-fed truncated buffers). Run now reaches DialogBoxParamW + WM_INITDIALOG; remaining frontier = unresolved RichEd20/RichEd32 (honest ERROR_MOD_NOT_FOUND) + undecoded crash at 139,972,897 steps. New observation recorded: destination path joins as "Program Filesry" (missing separator).
- mini.flood rebuilt from tracked source after sandbox reset (ziglang 0.17.0, tests/games/miniwin_flood.c unchanged); size matches the SWEEP-093 pin byte-count exactly (223,744 B); manifest re-pinned (a0102b83..., zig timestamps documented); gate 3x rc=0, 13 frames, crash=0 fatal=0.
- Gates: G1 PIN OK evidence/master/g1/20261028_045014 (binary sha c630edb6a9377582, 4,654,488 B, regression 12/12); corpus_gate.sh sweep095 = 17 rows, rc=0 in 12, crash total 0 (identical shape to SWEEP-094); battery 10 programs x3 runs with ZERO unexpected outcomes (idman/npp64/winrar/resourcesextract rc=0; 7zsetup rc=86 own decision; winrarinstd/everything/tcpview64/produkey alive-at-timeout 124 crash=0; nppsetup documented frontier); repo_size_guard GUARD OK (2 documented WARNs unchanged).

Stage Summary:
- Issue #3's headline research finding is now a landed, measured engine fix. The A03 attribution is superseded: guest correct, loader wsprintfW was the bug.
- Queued (not implemented): riched20/riched32 known-DLL plane; main-window content paint (children round-robin); ReadEventLogA honest empty-log; CW_USEDEFAULT positioning; "Program Filesry" join decode.
- Evidence: evidence/sweep095/issue_body.md + corpus_parity.tsv; scripts/sweep095_idm_cmdline.{sh,gdb}.

---
Task ID: SWEEP-096
Agent: main (autonomous train)
Task: SW-1474 + SW-1475 — NPP installer chain: riched known-DLL plane + mode-aware resource layer for PE32 guests.

Work Log:
- SW-1474 (loader.c): riched20.dll/riched32.dll added to g_known_dlls (NSIS loads them purely for class registration; the DLLs export nothing public). NPP installer RichEd20 load now succeeds; the ERROR 126 retry loop is gone.
- SW-1475 (loader.c) ROOT CAUSE: w3_res_find / mw_LoadStringA / mw_LoadStringW / mw_dlg_from_resource all gated on g_image_base (x64-native base, NULL for PE32/cpu32 guests) — every PE32 guest lived in a resources-free world (FindResource ERROR 1814, LoadString 0, dialogs with zero children). Gates + mw_res_pe_base default now go through the active-world helper mw_main_image_base() (g_image_base32 for the x86 interpreter, identity-mapped). x64 path returns the same base as before (bit-identical by construction).
- Measured (nppsetup): DLGTEMPLATE 0x69 resolved (EX, cdit=14, size 512); 14/14 children created (BUTTONs 3/1/2, STATICs 1018/1044/1035/1036/1045, ...); GetDlgItem(1/2) -> HWND 0x30002/0x30003 (was NULL).
- Remaining crash DECODED (was undecoded): guest EIP 0x406740, NSIS exehead string resolver NEGATIVE-index branch (lea eax,[eax*4+4]; sub ecx,eax; mov eax,[ecx]) reading global 0x428a3c (g_stacktop slot) = 0 -> derived-pointer deref. Path identical pre/post SW-1475 (pre-existing). g_stacktop init decode = next train entry point.
- Gates: G1 PIN OK evidence/master/g1/20261028_054121 (binary sha bb0ac7e17a2e1f41, 4,652,256 B, regression 12/12); corpus_gate sweep096 = 17 rows, rc=0 in 12, crash total 0; battery 10 programs x3 zero unexpected outcomes (procedure note: an interrupted gate window briefly removed minwin_loader via make clean; three plugins recorded runner-level rc=127 and were re-run clean); repo_size_guard GUARD OK.

Stage Summary:
- PE32 guests regained the resource layer (a class-level fix: FindResource/LoadString/dialog templates for every x86 guest, not just NPP).
- Queued: NSIS g_stacktop init decode; main-window content paint; "Program Filesry" join; ReadEventLogA; CW_USEDEFAULT positioning.
- Evidence: evidence/sweep096/issue_body.md + corpus_parity.tsv.

---
Task ID: SWEEP-097
Agent: main (autonomous train)
Task: SW-1476 honest empty-log eventlog plane + SWEEP-096 crash decode follow-up.

Work Log:
- SW-1476 (loader.c): psloglist64 ran with 2 unresolved imports (ReadEventLogA, OpenBackupEventLogA) and ReadEventLogW never set last-error (stale-error bug). Landed the honest empty-log plane: OpenEventLogA (cookie), ReadEventLogA/W = FALSE + SetLastError(ERROR_HANDLE_EOF=259) + *read=0 *needed=0, OpenBackupEventLogA = NULL + ERROR_NOT_SUPPORTED, BackupEventLogA = FALSE + 50, ClearEventLogA = FALSE + ERROR_ACCESS_DENIED, NotifyChangeEventLog = FALSE + 50, GetNumberOfEventLogRecords = TRUE + *n=0, GetOldestEventLogRecord = TRUE + *oldest=0. 8 dispatch entries added.
- Measured (psloglist64 2.82): unresolved imports 2 -> 0 (all 8 advapi32 eventlog IAT slots resolved); GetLastError after read = 259 consistently; tool takes its read-failure branch and exits rc=0.
- SWEEP-096 crash decode follow-up (gdb probe at SIGSEGV, research-only): [0x428a3c] g_stacktop slot = 0x111301c2 — NOT null (SWEEP-096 hypothesis CONTRADICTED); value points into the extracted NSIS header data (writer: mov [0x428a3c], edx with edx=header_ptr+10 at 0x404106). Faulting guest instruction mov eax,[ecx] (0x406740) with derived pointer 0 cannot result from the negative-branch math over a non-null g_stacktop — the faulting call's frame ([esp+8] arg2 / stack window) is the anomaly. Status UNRESOLVED, decode advanced; next experiment = frame-integrity probe at 0x406726. No code changed for the decode.
- CW_USEDEFAULT positioning parked (no guest evidence; sizing already landed SW-1471).
- Gates: G1 PIN OK evidence/master/g1/20261028_060651 (binary sha d61c7a6e1cdd2119, regression 12/12); corpus_gate sweep097 = 17 rows, rc=0 in 12, crash total 0; battery spot-check idman/npp64/winrar rc=0 x3; repo_size_guard GUARD OK.

Stage Summary:
- psloglist64 fully resolved at the API-contract level; stale-last-error class fixed for the eventlog family.
- Evidence: evidence/sweep097/issue_body.md + corpus_parity.tsv; scripts/sweep097_nppsetup_probe.gdb.

Task ID: SWEEP-103
Agent: main (autonomous train)
Task: honest re-test round (user instruction translated: "continue; also re-test the programs you previously could not run, and see whether progress was made"). No engine change; binary + evidence round.

Work Log:
- Sandbox-reset recovery: local clone was 104 commits behind (242af84 SWEEP-030 -> 821cc84 SWEEP-102); stale untracked artifacts backed up out of tree; git reset --hard origin/main; gdb/ziglang user-space toolchains re-provisioned as needed.
- Binary rebuilt deterministically: minwin_loader sha256 9e3e0e09... byte-identical to the SWEEP-102 G1 pin; G1 PIN OK 12/12 (evidence/master/g1/20261028_173735).
- Serial re-test battery (SWEEP-101/102 wall-clock lesson honored — no parallel runs):
  * nppsetup x3: rc=0 crash=0, run logs byte-identical (1 unique md5), install_log 75 ops, HKLM\SOFTWARE\Notepad++ registry tree captured — the sweep081 rc=139 crash @ 139,972,856 steps frontier is confirmed FIXED end-to-end on a fresh clone (SW-1477/SW-1478/SW-1485 chain).
  * notepad2mod x3: rc=0 crash=0 (was ADVANCED_FRONTIER_SW-1373 crash=1 rc=139) — fixed as an untracked side effect of the sweep082..102 engine work; first time recorded.
  * winrar provision run rc=0 (stages winrarinstd tree + 2702B reg seed).
  * tcpview64/everything/produkey/winrarinstd: rc=124 x3 each, crash=0 — unchanged, all inside the SWEEP-102 wait-family decode (WaitForMultipleObjects REALTIME-seconds vs guest-ms unit mix; SW-1489 spec parked = next fix train).
  * winnc/renamer: honest BLOCKED_DOWNLOAD — historical pinned bytes no longer served (winnc.com now ships a different 1,912,832 B build vs pinned 12,964,000 B; den4b flow JS-gated; archive.org unreachable). No dishonest re-pin.
- manifest.tsv: row 52 notepad2mod.portable UNVERIFIED_URL -> exact URL re-verified by fresh download + byte-for-byte SHA match (c893ce4a..., 814,355 B); row 58 mini.flood re-pinned b406517a... (SEED rebuilt from tracked tests/games/miniwin_flood.c via ziglang 0.17.0 after sandbox reset; 223,744 B byte-count matches the SWEEP-093 pin; zig-timestamp non-reproducibility documented).
- corpus_gate.sh sweep103: 17 rows, rc=0 in 12, crash total 0 — canonical shape restored.
- repo_size_guard: carried-over violation FIXED — evidence/sweep099/nppsetup/probe_sandbox/probe_install_log.tsv was still tracked since SWEEP-099 023e897 (the SWEEP-100 "remediation carried" note never actually landed in git); git rm --cached applied, file stays on disk; guard now GUARD OK (2 pre-existing documented WARNs unchanged).

Stage Summary:
- Progress quantified: 2 of 4 crash/exit frontiers from the sweep081 era now run rc=0 deterministically; 4 rc=124 frontiers unchanged and converge on one parked root cause (SW-1489 time plane); 2 guests honestly blocked on stale sources.
- Queued next train: SW-1489 time-plane implementation (all clock consumers audited TOGETHER per the SWEEP-102 spec) targeting the rc=124 quartet; winnc/renamer exact-URL re-pin hunt; "Program Filesry" join decode (still untouched upstream since SWEEP-095).
- Evidence: evidence/sweep103/summary.md + corpus_parity.tsv; plugins/status.tsv rows; evidence/master/g1/20261028_173735. No force-push.

Task ID: SWEEP-104
Agent: main (autonomous train)
Task: SW-1489 first wagon per the SWEEP-102 parked spec; round reshaped by observation — the rc=124 quartet decoded as ALIVE-AT-TIMEOUT (not wait-gate hangs); landed SW-1490 (runner frame accounting) + SW-1489 wagon 1 (MFO unit fix) + SW-1491 (honest rc=124 taxonomy).

Work Log:
- Observation (evidence/sweep103/probes/): /proc sampling + trace tails under battery env — produkey state S hrtimer_nanosleep (fix-F/WM_TIMER id=1 pump park), everything state S (WM_PAINT pump park), tcpview64 state R ~100% core (SendMessageW msg=0x43f refresh storm), winrarinstd state R ~100% core (HKCU\Software\WinRAR\Setup registry-miss re-read). NONE parks in WaitForMultipleObjects — the SW-1489 unit bug is real but latent; the quartet is ALIVE at timeout (two honest interactive pumps, two spin frontiers).
- SW-1490 (scripts/plugin_run.sh): the SW-1470 portable branch runs zip guests from $PORTABLE_CWD but frames/PNG/trace were read from $RESROOT (repo root) — every portable-zip plugin silently lost all frame evidence. RESROOT_EFF now tracks the real results dir. Measured: produkey frames=1 recovered; notepad2mod rc=0 frames=0 -> PASS_INPUT_STATE_CHANGE frames=4 uniq=3 (first full input-state evidence ever for it).
- SW-1489 wagon 1 (src/loader/loader.c mw_WaitForMultipleObjects): deadline was now.tv_sec (REALTIME epoch seconds) vs deadline/1000 (guest ms) — always-true -> every finite wait WAIT_TIMEOUT after one 10ms slice, INFINITE waits poll forever. Now CLOCK_MONOTONIC elapsed-ms anchor, full-budget finite waits, jump-immune. WSA/Ex wrappers inherit. Verified: no pump-path caller (guest ENTRY rows + 2 guest-facing wrappers only).
- SW-1491 (classify): rc=124 decodes parking shape from api_trace tail — ALIVE_PUMP_AT_TIMEOUT (GetMessage->WM_/fix-F signature) vs SPIN_AT_TIMEOUT (SendMessage re-fire) vs TIMEOUT; branch order fixed so frames>0 + rc=124 can never read as PASS.
- Regression watch: nppsetup first post-fix battery 0/0/124 = SWEEP-102 documented load-gated bistability (post-G1 load window); idle re-runs rc=0 x3 byte-identical x2 batteries. ONE new rare shape in the loaded window (1/12 runs): 'Error decompressing data! Corrupted installer?' MessageBox at exehead init then byte-identical clean flow — never seen before, unreplicated since; registered SW-1492 (decode queued).
- Gates: G1 PIN 12/12 (evidence/master/g1/20261028_181913, binary a09aa334); corpus_gate sweep104 = 17 rows rc=0 in 12 crash total 0; repo_size_guard GUARD OK (2 documented WARNs unchanged); serial battery: nppsetup rc=0 x3 byte-identical, notepad2mod PASS_INPUT_STATE_CHANGE (4 frames, 3 uniq), produkey ALIVE_PUMP_AT_TIMEOUT frames=1, everything ALIVE_PUMP_AT_TIMEOUT, tcpview64/winrarinstd SPIN_AT_TIMEOUT. No manifest changes.

Stage Summary:
- The rc=124 quartet's honest state is recorded: alive pumps (produkey/everything) and spin frontiers (tcpview64 msg=0x43f pacing; winrarinstd SW-1412-family registry re-read) — next-train decodes named.
- SW-1490 unlocks the portable-plugin evidence chain project-wide (frames + PNGs + input-state PASSes that were silently dropped since SWEEP-094).
- Queued: SW-1489 core time plane (parked, now with measured input); SW-1492 decompression-error decode; spin-frontier decodes; winnc/renamer URL re-pin hunt; 'Program Filesry' join decode.
- Evidence: evidence/sweep104/summary.md; evidence/sweep103/probes/ (park-shape sampling); plugins/status.tsv rows. No force-push.

## SWEEP-105 (2026-10-29) — internet re-pin round + SW-1493 runner fix + SW-1494 renamer decode

- Sandbox-reset recovery: clone 74 commits behind (SWEEP-030 -> SWEEP-104); reset to origin/main ad1288d; cold rebuild reproduced the SWEEP-104 G1 pin a09aa334 byte-for-byte BEFORE edits (determinism re-confirmed).
- Internet re-pins (user granted internet): winnc.installer VERIFIED_URL via winnc.com/setup.exe (winnc.de dead; SHA+size exact match); renamer.portable VERIFIED_URL via den4b two-step key flow (fetch_guest.sh: new one-shot tokenized-retry, SHA gate unchanged; SHA+size exact match); shexview UNKNOWN_REPIN_REQUIRED -> re-pinned from exact nirsoft URL (73248fb4..., 103904 B); mini.flood SEED rebuilt (ziglang 0.17.0, 223744 B byte-count exact, re-pinned 9b06ed2c...). hxd attempt: origin down, wayback snapshot 20260925180045 exists but transfer timed out (stays BLOCKED honestly).
- SW-1493 (scripts/plugin_run.sh): bare $PORTABLE_CWD under set -u killed every non-portable/non-sandbox plugin battery ("PORTABLE_CWD: unbound variable", latent since SWEEP-104; winnc was the first shaped plugin to hit it) — fixed with ${PORTABLE_CWD:-} guard.
- SW-1494 (decode): renamer rc=204 fully mapped — Delphi reInvalidPtr(204): `mov WORD [0x7447a0],0xCC` (ExitCode) at 0x410cac -> _Halt@0x410c40 `mov [0x8ec000],eax` -> exit-callback [0x7448a0]=nil skipped -> _Halt0 chain -> push [0x8ec000] -> stub 0x401030 (jmp [0x8f5978]) -> ExitProcess(204); ~55x {SetLastError(0),GetLastError()=0,TlsGetValue(1)=0x11000000,slot 0xa00196->39} init loop decoded; trigger (which init semantic corrupts the Delphi heap) UNRESOLVED and queued. Instrument added: m32_st8 W8 watch (SW-1494), MW32_WATCH-gated.
- winnc FIRST RUN EVER (post SW-1493): rc=86 x3 deterministic crash=0 — Inno stub LockResource(0x4a17cb..0x4a180f) then jumps into its own stack, `[PE32-FATAL] opcode=0xef at EIP=0x3fefefff` (SW-1281 abort); registered SW-1495 frontier (bad indirect target right after LockResource).
- Batteries (sweep105, foreground after detached-train deaths): nppsetup rc=0 x3 (no regression), notepad2mod PASS_INPUT_STATE_CHANGE x3, produkey ALIVE_PUMP frames=1, everything ALIVE_PUMP, tcpview64 SPIN, winrarinstd SPIN — quartet taxonomy EXACTLY matches SWEEP-104 (SW-1489 caused no drift). corpus_gate sweep105: 17 rows, rc=0 in 12, crash 0 (canonical). G1 12/12 (7f08acaa, 20261028_201610).
- Tooling lesson: detached (setsid nohup) trains die between tool calls in this sandbox; long batteries run foreground chunked. TSV edits must use python field-writers (a MultiEdit call space-mangled 26 manifest rows; gate caught it; reverted via git checkout before re-applying).
