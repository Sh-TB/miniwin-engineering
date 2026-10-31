# SWEEP-076 — B-group rescue: 6 engine-generic root causes, 5 guests fixed

Branch main @ 0b7cb7d (SWEEP-075). Engine sha256 33661afa... (4,357,280 bytes).
G1 PIN 12/12 (evidence/master/g1/20261026_054035). All runs x3 cold, per-target
fresh hive, determinism verified (rc + steps byte-stable).

## Verdict matrix (the 8 FAIL-state B-group targets from sweep074)

| id  | guest               | before        | after         | car     |
|-----|---------------------|---------------|---------------|---------|
| R3  | LastActivityView x86| FAIL_CRASH 139| **PASS rc=0** | SW-1332 |
| U03 | Notepad3 x86        | FAIL_CRASH 139| **PASS rc=0** | SW-1331/1332 |
| U04 | Notepad3 x64        | FAIL_CRASH 139| **PASS rc=0** | SW-1333..1337 |
| U07 | AutoHotkeyU32 x86   | FAIL_CRASH 139| **PASS rc=0** | SW-1331 |
| U16 | ZoomIt x86          | silent 139    | **PASS rc=0** | SW-1341/1341b |
| U25 | TcpView x64         | FAIL_CRASH 139| RUNNING (no crash, event loop, rc=124=alive at 60s) | SW-1338 |
| U18 | RegScanner x86      | FAIL_CRASH 139| FAIL_CRASH 139 (deterministic steps=1283963; walk fix landed, runtime frontier named) | SW-1342/SW-1343 |
| U27 | WinLister x64       | FAIL_CRASH 139| FAIL_CRASH 139 (fully decoded; needs low guest stack plane) | SW-1340 queued |

Corpus parity: winmine 82 / sol 69 / freecell 10 / calc 1 / breakout 10 / snake
13 x3 deterministic, spider 592-628 (pre-existing timer band), zero crashes,
zero PE32-FATAL — NO REGRESSIONS.

## Root causes (all engine-generic, no per-app hacks)

1. **SW-1331 — PE32 data-directory snapshot (UAF).** load_pe32 read dd[] from
   the FILE BUFFER after `free(raw)`. Guests whose file size crosses the glibc
   mmap/trim threshold (AutoHotkeyU32.exe ~0.9MB) got garbage directory
   entries: dd[13] pointed at a wild ddesc=0x43a76552 (ASCII heap reuse) ->
   crash in the delay-import walk. Fix: snapshot all 32 directory dwords
   while raw is valid; zero entries beyond NumberOfRvaAndSizes (a short
   table previously exposed section-header bytes as phantom directories).
2. **SW-1332 — GetModuleHandleA/W(NULL) must answer the ACTIVE world's base.**
   The x86 interpreter maps the guest at g_image_base32; the shared API
   returned the x64-only g_image_base (NULL in PE32 mode). LastActivityView
   died 26 steps in on the classic inline PE parse
   `call GetModuleHandleA(NULL); cmp word[eax],'MZ'; mov ecx,[eax+0x3c]`.
   Fix: mw_main_image_base() helper; GetModuleHandleA/W/ExA NULL paths return
   the real base — the x86 image carries its true PE headers so inline
   parsers work unmodified (same root family as SW-1322, x86 side).
3. **SW-1333 — comctl32!#381 = LoadIconWithScaleDown.** Notepad3 x64 imports
   it BY ORDINAL. Decoded from call sites: 5-arg (hinst, MAKEINTRESOURCE(id),
   cx, cy, HICON* out) + HRESULT checked by `test eax; jns` (S_OK=0). The
   class sits with the 410-413 subclass family Notepad3 imports alongside.
   Stub-0 = S_OK with *out untouched -> NULL icon object -> vcall crash.
4. **SW-1334 — per-window class-proc routing.** Engine children created from
   a GUEST-registered class never received messages (SendMessageW answered
   0). Fix: capture the class proc at creation (x64 only; x86 procs are raw
   EIPs and must go through the cpu32 shim) into the child record; SendMessage
   routes non-engine messages to it — real Windows "every window has a
   wndproc" contract.
5. **SW-1335/1335b — creation message sequence + DefWindowProc answers.**
   Guest-class children now receive WM_NCCREATE(0x81) -> WM_NCCALCSIZE(0x83)
   -> WM_CREATE(0x01) at creation (Scintilla constructs its C++ object in
   WM_CREATE; without the sequence the first SCI message hit an uninitialized
   object). WM_CREATE=-1 cancels creation. DefWindowProc/DefWindowProcA and
   mw_engine_defproc now answer WM_NCCREATE with TRUE (documented default) —
   Scintilla forwarded it, saw FALSE, and creation cancelled.
6. **SW-1336 — shlwapi string family (20 names).** StrStrW/A, StrChrA/IA/IW,
   StrRChrW, StrCmpW, StrCmpNW/NA/NIW/NIA, StrDupW/A, SHStrDupW/A,
   StrToIntExW, StrSpnW, StrCSpnA, StrTrimA, StrRetToBufW, UrlEscapeW,
   UrlCreateFromPathW, StrFormatByteSizeEx — real semantics on UTF-16
   strings; each stub-0 poisoned Notepad3's config/locale parsing.
7. **SW-1337 — SHGetPropertyStoreForWindow honest E_FAIL.** Stub-0 = S_OK
   with *ppv untouched -> the caller deref'd NULL. AppUserModelID stores are
   decorative; E_FAIL sends the guest down its documented failure path.
8. **SW-1338 — SetWindowLongPtrW(GWLP_WNDPROC) returns the CURRENT proc.**
   First subclass of an engine window returned NULL (wndlong table empty);
   real Windows returns the class proc, never NULL. TcpView subclasses its
   list-view (GetDlgItem id=1007) and treats NULL-oldproc as failure.
9. **SW-1341/1341b — PE32 relocation plane.** ZoomIt x86 is 137MB
   (ImageBase 0x400000, SizeOfImage 0x8389000): MAP_FIXED at the preferred
   base OVERWROTE the host loader's own text (0x2006000..0x20ef000) — silent
   SIGSEGV with the crash handler dead on zeroed pages (core dump: RIP at
   handler entry, bytes all zero). Fix: collide-check the image window
   against the host loader span + arena + thunk page; relocate to the first
   free slot (0x40000000 for ZoomIt) and apply HIGHLOW fixups with
   delta = load_base - image_base. **SW-1341b: the reloc loop patched type 1
   (IMAGE_REL_BASED_HIGH) instead of type 3 (HIGHLOW) — every x86 image ever
   got ZERO fixups (harmless while delta==0, fatal after relocation);
   ZoomIt's 37,180 type-3 entries now apply.**
10. **SW-1342 — import-descriptor terminator.** The walk stopped only when
    BOTH name_rva and first_thunk were zero; Wine/Windows stop when EITHER
    is. RegScanner's import directory (size 0x1e0 = 24 slots) overlaps its
    own IAT — descriptor 9 = IAT values read as a descriptor (name=0,
    ft=0x1f4b6) — the old condition walked it and produced garbage import
    names ('tion', 'gCloseKey' = name pointers +2). Fixed in all three
    walks (EXE PE32 x2, DLL-on-disk).

## Honest remaining frontiers (named, with evidence)

- **SW-1343 (U18 RegScanner x86)**: deterministic crash steps=1283963 at
  guest EIP 0x411100 — a `mov [edx],al` NULL store after a
  CreateToolhelp32Snapshot + GetProcAddress(0xdea00000, 'tion'/'gCloseKey')
  chain: our Toolhelp32 module-list model hands the guest name pointers that
  read 2 bytes off ('gCloseKey' = 'RegCloseKey'[2:]). Needs the module-name
  struct layout audited (MODULEENTRY32 offsets) — queued.
- **SW-1340 (U27 WinLister x64)**: crash inside libc memcpy writing to a
  32-bit TRUNCATED stack pointer (RDI=0x53936168, RSP=0x7ffe53936008). Real
  Windows x64 stacks live below 4GB (0x12F000...); our native-execution
  guests run on the host stack. Any guest code that truncates stack pointers
  works on Windows and dies here. Needs a low (<4GB) guest stack plane for
  x64 guests — invasive across the x64 corpus, queued as its own wagon.
- **U25 TcpView x64**: no crash; runs its init and event loop until the
  60s harness timeout (rc=124 deterministic). A live-network-monitor UI has
  no clean headless exit — recorded as RUNNING, not PASS_BEHAVIOR.

## Rules honored

- No per-application patches: every fix is a shared engine contract (module
  handles, class procs, DefWindowProc defaults, relocation plane, import
  walk terminators, ordinal completion from decoded call sites).
- No third-party binaries committed; downloads re-fetched from official
  sources, SHA-256 byte-verified against evidence/sweep074/targets.tsv
  (8/8 MATCH, manifest updated in raw/).
- G1 PIN re-run after every loader.c batch; corpus x3 parity green.
