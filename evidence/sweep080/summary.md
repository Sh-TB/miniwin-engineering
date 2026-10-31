# SWEEP-080 — winrar / idman / notepad++ : the user-named trio attack

Base: origin/main f08dc8b (SWEEP-079), byte-exact rebuild 5a8d5a72 BEFORE any
change (env-reset protocol), G1 pre-change pin 12/12 (20261026_143555).
Final engine sha256 123d7750... (G1 post-change 20261026_154619).

## Targets (download preflight: dl_sha256.tsv)
- WinRAR 7.12 x64 — artifacts/sweep062/dl/winrar712.exe, 80ab6824... (stock)
- IDM 6.42b2 x86 — https://mirror2.internetdownloadmanager.com/idman642build2.exe, 276aa7e4...
- Notepad++ 8.9.8.1 x64 — official portable zip, beddf554... (231 files extracted)

## Headline results

| Guest | Before | After |
|---|---|---|
| winrar712 x64 | silent exit rc=0, wizard dialog misparsed (garbage child ids) | **13/13 STARTDLG controls parsed and registered** (STATIC/COMBOBOX/BUTTON/RICHEDIT/msctls_progress32), license/STARTDLG flow reached; frontier = SFX custom resolver (SW-1361) |
| idman x86 | silent exit at 1,936 steps (InterlockedIncrement unresolved) | **76K+ steps, full unpacker run**: registry seeding (real RegSetValueExW), temp dir create/check, /s probe, CreateProcess attempt, **clean honest error dialog** "Could not create/open error log file" — frontier = child-process support (SW-1370) |
| notepad++ x64 | SWEEP-066x64: 2 MessageBoxes then crash | **parity+**: same 2 real MessageBoxW frames (Configurator / stylers.xml) + RegisterClassW ATOM 0xc001 + **dialog children now registered** (SW-1360 walker fix); crash moved into the guest class proc (SW-1298-x64 continues) |
| notepad++ x86 | 2450-step crash (pre-existing, relocated) | unchanged (SW-1371: SW-1341-relocated CRT-init crash — pre-existing, A/B same step count) |

## Generic engine fixes (no per-app hacks)

1. **SW-1360 — DLGTEMPLATE walker triple off-by-one** (mw_dialog_create_children):
   header skip p+=10 -> p+=9 (18B/9 words); item id it[9] -> it[8]; item class
   it+10 -> it+9. Proven by byte-level simulation on the real WinRAR STARTDLG
   template: the buggy walk reproduces the traced garbage ids EXACTLY, the
   fixed walk reproduces the real template ids 200/109/110/65535/101/102/103/
   104/105/106/111/1/2. Templates with menu=0/class=0/empty title accidentally
   compensated — why Sysinternals EULA dialogs looked fine while WinRAR's
   installer wizard was total garbage.
2. **SW-1360b — class table**: msctls_progress32 added as sysclass 0x8E
   (w/a tables + name array + ordinal ranges 0x85->0x8E); the class-lookup
   loop bound was the 15-char probe width AND the table length — table entries
   beyond index 14 were silently unreachable (sizeof(t) now).
3. **SW-1360c — dialog APIs traced**: SendDlgItemMessageW/SetDlgItemTextW/
   GetDlgItemTextW were invisible in traces (whole dialog phase was a void).
4. **SW-1360d — MW32_DUMP_VA for x64**: the dump facility never fired for
   PE32+ guests; also fixed the 32-bit VA truncation (0x140040900 ->
   0x40040900 host SIGSEGV inside the dump loop itself). x64dump/x64postdump
   (entry + ExitProcess) now emit.
5. **SW-1360e — file-position APIs traced**: SetFilePointer/SetFilePointerEx/
   GetFileSize (winrar's SFX archive scan was invisible; the scan is a
   stride-8176 sliding window over its own file looking for the Rar! marker).
6. **SW-1362 — Interlocked family**: InterlockedIncrement/Decrement/Exchange/
   ExchangeAdd/CompareExchange/CompareExchangePointer implemented as real
   host atomics over the guest pointer (was: NO dispatch entries -> unresolved
   gate slots returning 0; IDM's CRT init state machine died on the first one).
7. **SW-1363 — 12 missing arity entries** (ESP +32 corruption class!):
   GetEnvironmentStringsW(0), GetMessageTime(0), GetDoubleClickTime(0),
   GetClipboardOwner(0), CountClipboardFormats(0), ReleaseMutex(4),
   GetKeyboardLayout(4), IsWindowUnicode(4), GetTopWindow(4),
   GetLastActivePopup(4), ShowOwnedPopups(8), SetRectEmpty(4). A 0-arg stdcall
   call that pops 32 leaves the guest ESP permanently shifted; IDM's malloc
   frame then overlapped the caller's locals (WideCharToMultiByte srclen
   read clobbered garbage -> conversion failed -> HeapFree(wild) -> exit 4).
8. **SW-1364/1364b — arena32 heap**: HeapFree no-op for bump-arena pointers
   (the documented contract; was HEAP-TRAP _exit(4) on the first legit free);
   HeapReAlloc = alloc-new + clamp-at-live-edge copy (arena tracks no sizes).
9. **SW-1365 — wsprintf family**: wsprintfA/W + wvsprintfA/W are CDECL
   varargs (arity 0; was 32 -> ESP +32 after every format call -> IDM jumped
   into the thunk page). Plus a REAL ms_abi varargs gate path (the 16-arg
   SysV cast-call put buf/fmt in the wrong registers for __builtin_ms_va_list
   callees; now GCC materializes the true Windows x64 vararg layout).
10. **SW-1366 — %S/%ls/%hS**: wide-source conversions in the ANSI formatter
    (the wide formatter already had %S; the ANSI one printed mojibake).
11. **SW-1369 — GetFileAttributesW path normalization**: guests concatenate
    Windows-style paths ("/tmp/" + L'\\' + name); the literal backslash made
    stat() miss the directory that WAS created (IDM "Error ID: 2"). Same
    rule as CreateFileW/BUG-054b.

## Honest frontiers (queued)
- **SW-1361**: winrar SFX ships its own delay-load resolution machinery
  (SRW lock + VirtualProtect self-rebind of .didat slots; custom resolver
  `lookup(table, key); jmp rax`). Only ~20 APIs resolve through our
  GetProcAddress; the dialog phase resolves/fails outside our dispatch.
  Needs SFX-table decode (Ghidra-class) or synthetic export directories for
  virtual DLLs so the guest's self-resolution finds typed functions.
- **SW-1370**: idman unpacker needs CreateProcessW child-process support
  (loader re-exec) for the setup chain; its log-file probe creates host-root
  files ("/s" — OPEN_ALWAYS honored, sandbox pollution noted).
- **SW-1371**: notepad++ x86 crash at 2450 steps under the SW-1341 relocation
  (pre-existing, byte-identical step count vs sweep067 evidence).
- **SW-1298-x64** (continues): npp64 guest class-proc writes [rcx+0x20] with
  rcx=0x20 from inside its own WNDPROC (RVA 0x1099b3) — deeper than
  SWEEP-066x64 (which stopped at the same rc=139 class after RegisterClass).

## Gates
- G1 PIN 12/12 pre-change (20261026_143555, 5a8d5a72) AND post-change
  (20261026_154619, 123d7750).
- Corpus x3 EXACT parity: winmine 82/82/82, sol 69/69/69, freecell 10/10/10,
  calc 1/1/1, breakout 10/10/10, snake 13/13/13; spider 499-623 (wall-clock
  band, origin-parity class).
- Puzzle spot-check 4/4 (blackbox/bridges/cube/dominosa, sha-verified vs
  sweep074 targets.tsv): rc=0, 2 frames each, zero crashes.
- evidence/sweep080: dl_sha256.tsv, corpus_parity.tsv, raw/, idm_messagebox*.png,
  npp64/messagebox_1.png, npp64/messagebox_2.png.
