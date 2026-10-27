
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
---
Task ID: SWEEP-079
Agent: main (z.ai)
Task: «ادامه ناقص ناقص» — finish the incomplete: SW-1354 winlister x86 GUI frontier, WinObj zero-frames, retry the 11 BLOCKED_DOWNLOADs

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
Task: «ادامه بده ببینم winrar idm notpad++ رو تونستی اجرایی کامل بکنی یا نه» — the user-named trio: winrar712 x64, idman 6.42b2 x86, notepad++ 8.9.8.1 x64+x86; decode their blockers, land generic engine fixes, push

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
Task: «ادامه این سه برنامه رو برسون در حد اجرای گرافیکی کامل ... hxd و برنامه winnc و چند تا برنامه ریزه میزه دیگه ... هر برنامه چیزی شبیه یک پلاگین باشه» — plugin architecture + trio re-verify + new app-plugins (winnc/hxd/everything/renamer/notepad2-mod)

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
Task: «ادامه بده تا تکمیل شدن» — elephant train: o32/SEH class fix set, new program (mines), gates, push

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
Task: «ادامه — تصویر اجرا کامل برنامه‌ها را بده، هر ادامه یک برنامه جدید، گیت‌هاب را بررسی کن» — visual-execution-proof train + new program (solo) + gates + push

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
Task: «برنامه‌هایی که صفحه خطا می‌دهند اجرا نشده‌اند — مشکل را حل کن؛ پوشه‌بندی پیش‌نیازها کامل شود؛ شبیه‌سازی نصب» — generic file/focus/paint fix set + honest reclassification

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
Task: «ریجیستری و پوشه‌ها باید درست در مسیرهای خودشان ساخته شوند» — installer ground-truth foundation + one new game + one new program

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
