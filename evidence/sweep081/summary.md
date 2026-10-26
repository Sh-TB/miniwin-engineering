# SWEEP-081 — the plugin framework + the trio re-verified + 4 new app-plugins

## Baseline
- origin/main 829d1560 (SWEEP-080) == local; clean rebuild byte-exact
  123d7750 BEFORE any change; G1 pre-change PIN 12/12 (20261026_161027).
- Final engine sha256 92e7df46... (G1 post-change PIN 12/12, 20261026_165111);
  loader 4,465,312 B (+35,064).

## The plugin architecture (user-directed: "هر برنامه چیزی شبیه یک پلاگین")
- `plugins/<name>/plugin.tsv` — one manifest per application: official
  source_url + source_sha256, artifact path, unpack mode, exe, input probes
  (MW_GUI_KEYS/MINWIN_MOUSE), known root-cause numbers, note.
- `scripts/plugin_run.sh <name|--all>` — generic runner: artifact hash
  verify -> unpack -> N cold runs with the scripted input channel -> frame
  stats (initial + post-input diff) -> verdict -> evidence PNG conversion ->
  one status row.
- `plugins/status.tsv` — the status ledger (verdict vocabulary: honest
  states only — PASS_INPUT_STATE_CHANGE / PASS_LAUNCH_RENDER /
  FRONTIER_SW-n / ADVANCED_FRONTIER / PARITY_FRONTIER / BLOCKED_DOWNLOAD).
- `plugins/README.md` — the contract.
- Standing rule honored: the three user-named base apps (winrar, idman,
  npp64) run on every train; each train additionally runs ONE new unknown
  (this train: notepad2-mod; it joined as a permanent plugin).

## Downloads (official URLs + SHA-256 in dl_sha256.tsv)
- WinNc 11.6.0.0 x86 installer — https://www.winnc.com/setup.exe, bef0fcaf...
- Everything 1.4.1.1028 x64 portable — voidtools.com, 4f4a9b92...
- ReNamer 7.5 portable — den4b.com (official 2-step key flow), b11342a5...
- notepad2-mod 4.2.25.998 x64 — github release, c893ce4a...
- HxD: **BLOCKED_DOWNLOAD** — mh-nexus.de TCP-443 refused from the sandbox
  (DNS ok; 6 attempts across SWEEP-079 + SWEEP-081 incl. http/UA/IPv4
  variants). Same-category substitutes probed and dead: Frhed (SF paths
  404/unreachable), XVI32 (host dead), Okteta (no Windows zip published),
  ImHex (66 MB — fails the "tiny" criterion). No unofficial mirrors used;
  the hxd plugin stays in the ledger and retries next train.

## Engine fixes landed
1. **SW-1372 — the shlwapi/user32/kernel32 string+profile batch** (23 APIs,
   notepad2-mod x64 23-unresolved-import gate): StrCpyW/StrCpyNW/StrCatW/
   StrNCatW/StrCatBuffW/A, PathIsPrefixW, PathCommonPrefixW,
   PathRenameExtensionW, PathQuoteSpacesW, PathUnquoteSpacesW,
   PathUnExpandEnvStringsW (documented env set, longest-match),
   PathRelativePathToW (component splitter + ".."-upcount), PrivateProfile
   SectionW pair (family contract: no ini store, documented default
   answers), IsCharLowerA/UpperA/UpperW/AlphaNumericA (LowerW binds the
   pre-existing accented-range impl), MessageBoxExW (routes to the real
   MessageBoxW), DrawAnimatedRects (TRUE + trace), SHCreateDirectoryExW
   (recursive mkdir over the host FS, ERROR_ALREADY_EXISTS contract),
   SHGetDataFromIDListW (honest E_FAIL). Plus x86 stdcall arity rows for
   all of them (the ESP-corruption class stays dead) and SHELL32.180
   (wine shell32.spec: 180=SHGetDataFromIDListW — the exe imports the same
   name beside the ordinal, the classic lib-duplicate).
   RESULT: notepad2-mod unresolved imports 23 -> **0**.
2. **SW-1334 observability** — the class-proc dispatch trace now prints the
   numeric message ("WM_?[0x20]") — every future guest-wndproc decode gets
   the message id for free.

## The user-named trio (re-verified through the plugin runner, NO regression)
- **winrar 7.12 x64**: PARITY — DialogBoxParamW(STARTDLG) resolves, the
  fixed SW-1360 walker parses **13/13 children** with real texts (license
  id=111 "122 wchars", buttons id=1/2), then the SW-1361 SFX
  self-resolver frontier (SRW+VirtualProtect .didat rebinds) exits clean
  rc=0 before first paint. Unchanged from SWEEP-080; SW-1361 remains the
  next deep decode (synthetic export directories for virtual DLLs).
- **idman 6.42b2 x86**: PASS_LAUNCH_RENDER rc=0 — the full unpacker +
  registry seeding + honest "Could not create/open error log file"
  dialog rendered (messagebox_1.png). SW-1370 (CreateProcessW child
  chain) remains the frontier.
- **npp64 8.9.8.1 x64**: PARITY+ — 2 real MessageBoxW frames
  (Configurator / stylers.xml), then the SW-1298-x64 class-proc crash,
  NOW DECODED to instruction level:
  - crash site RVA 0x1099b3: `mov [rcx],rbx` with rcx=0x20, rbx=0
    (`lea rcx,[rbx+0x20]` first — a C++ intrusive-list/sentinel store on
    a NULL object), rdi=0x10001 (the window handle), reached through the
    SW-1334 class-proc re-entry (the [rsp]=0 sentinel return frame).
  - the crashing path is NOTepad++'s wndproc itself but a callee chain
    (0x1fd90/0x1fcb0/0x1f0d0 all called with rcx=rdi) that ended up with
    rbx=0 — an earlier API in that chain returned NULL and the guest
    proceeded. Next train: identify 0x1f0d0's `r8d=0x2a` call (a
    LoadStringW/MessageBoxW-class wrapper) and walk back to the NULL
    producer.

## New app-plugins (first results)
- **winnc 11.6 x86 (Inno Setup)**: FRONTIER **SW-1374** — Inno's runtime
  probes `GetProcAddress(ntdll, "wine_get_version")` (honest NULL +
  unresolved-stub thunk), then host SIGSEGV inside the cpu32 interpreter
  at guest 0x40722A (Inno runtime dispatch `mov ax,6; call rel32`),
  RDI=0x1ffffff, step 551. Needs a step-level decode of the Inno runtime
  dispatcher.
- **everything 1.4.1 x64**: FRONTIER **SW-1375** — guest C++ exception
  (0xE06D7A82 on the stack) then NULL-deref `mov rcx,[rsi+0x98]` rsi=0
  at RVA 0x105f9f. Likely the NTFS MFT/DeviceIoControl volume-query
  path (Everything's core) raising where real Windows answers.
- **renamer 7.5 x86 (Delphi)**: FRONTIER **SW-1376** — PE32-FATAL opcode
  0x12 at EIP=0x3fefef39 (inside the guest STACK) after the Delphi
  SysInit TlsGetValue/SetLastError/GetLastError probe loop — the
  wild-jump-into-stack class (SW-1358b family; SEH-chain suspicion).
- **notepad2-mod x64 (NEW unknown)**: ADVANCED — 23 unresolved imports
  resolved by SW-1372; now runs its message pump (RegisterClassW +
  44 SendMessageW class-proc cycles, GetLocaleInfoW,
  SystemParametersInfoW) and dies at a COM/virtual call
  `[rax+0x208]` with this=NULL after `GetWindowLongPtrW(<guest-stack
  addr>,0) -> 0`. Recorded as **SW-1373**.

## Gates
- G1 PIN 12/12 pre-change (20261026_161027, 123d7750) AND post-change
  (20261026_165111, 92e7df46).
- Corpus x3 EXACT parity, zero crashes/fatals: winmine 82/82/82,
  sol 69/69/69, freecell 10/10/10, calc 1/1/1, breakout 10/10/10,
  snake 13/13/13; spider 627-633 (wall-clock band).
- Puzzle spot-check 4/4 (blackbox/bridges/cube/dominosa): rc=0,
  6-26 frames, zero crashes.

## Honest frontiers (queued)
- SW-1298-x64: npp64 NULL-object sentinel store (decoded to the
  instruction; next: walk back to the NULL producer API).
- SW-1361: winrar SFX delay-load self-resolver (synthetic export
  directories for virtual DLLs — the standing proposal).
- SW-1370: idman CreateProcessW child-process chain (loader re-exec).
- SW-1373: notepad2-mod COM vtable call on NULL this post-pump.
- SW-1374: WinNc/Inno runtime dispatch host crash.
- SW-1375: everything MFT/DeviceIoControl-class guest exception.
- SW-1376: renamer Delphi wild stack jump (SEH-chain class).
- HxD: BLOCKED_DOWNLOAD (network egress), retry next train.
