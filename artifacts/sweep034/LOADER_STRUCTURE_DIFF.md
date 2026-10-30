# SWEEP-034 — LOADING STRUCTURE: Wine vs MiniWin (level-0 extraction, measured)

Date: 2026-10-30. Method: the user's level-0 methodology — read the Wine source
as the oracle (`wine-9.0` tree at `workspace/wine-ref/wine-9.0`, mirrored against
wine-10.0 behavior), diff it against `src/loader/loader.c` (47,882 lines), and
prove every claimed gap with a real binary run from this session.

Wine's PE load path (dlls/ntdll/loader.c + unix/virtual.c + unix/env.c):

| # | Wine phase | Wine source anchor | MiniWin (loader.c) | Verdict (measured this session) |
|---|---|---|---|---|
| 1 | **Process/env init** — TEB+PEB built by the unix side before any PE code runs | `unix/env.c` (signal handlers, TEB, PEB, last-error at TEB+0x68) | `setup_teb_peb()` (line 526) before `load_pe()`; last error lives at TEB+0x68 (`mw_GetLastError`, SW-1225) | MATCH |
| 2 | **Image mapping** — `virtual_map_image` maps sections at the preferred base or a relocated base | `unix/virtual.c` | `load_pe()` (line 42462): map sections → host `mmap`, image base 0x140000000 (or host-relocated) | MATCH (console/GUI all run: 22/22 corpus binaries load, SWEEP-033) |
| 3 | **Base relocations** — `LdrProcessRelocationBlock` (HIGHLOW/DIR64) | loader.c:3515 (Wine) | reloc loop in `load_pe` (HIGHLOW x64 + x86 loop, SW-1297) | MATCH |
| 4 | **Import resolution** — `fixup_imports` → `import_dll` walks descriptors, resolves by NAME through the loader's module list; missing DLL = `LdrLoadDll` recursion | Wine loader.c:1093 `import_dll`, 1426 `fixup_imports` | `g_import_table` name match (3,094 ENTRYs, `mw_resolve_imports`), recursion via `load_pe_dll` (line 1391) | MATCH — 100% static-import resolution for all 22 corpus apps (SWEEP-033) |
| 5 | **Dependency-order attach** — `process_attach` walks the DDAG bottom-up so a dependency's `DLL_PROCESS_ATTACH` runs before its importer | Wine loader.c:1704 `process_attach` + `walk_node_dependencies` | disk-DLL path runs `DllMain(ATTACH)` per `load_pe_dll`; "DllMain still runs once, at the end, as Windows does" (line 1444) | PARTIAL — single-level attach order matches for the corpus; deep DDAG (diamond deps) untested |
| 6 | **TLS** — `alloc_tls_slot` per module + `LdrInitializeThunk` runs TLS callbacks with reason DLL_PROCESS_ATTACH | Wine loader.c:1281, unixlib `LdrInitializeThunk` | TLS callbacks: EXE slot 0 + per-DLL slots, reason=1 (line 1792-1795) | MATCH (sal3-class guests verified SWEEP-049) |
| 7 | **Entry invocation** — `pBaseThreadInitThunk` → main EP with a real return address | unix/loader.c init block (SET_INIT_BLOCK) | EP called as host code with sentinel RA; SIGSEGV backtracer walks guest frames (SW-1503) | MATCH |
| 8 | **WINDOW-MANAGER STRUCTURE** — each window dispatches through **its own class's** `lpfnWndProc`; classes are per-atom distinct identities; `DispatchMessage` routes by the target hwnd | user32 models this over win32k; the observable contract putty 0.85 + RAMMap64 exercise: RegisterClass → distinct atoms, CreateWindow binds the class proc, GWLP_USERDATA per-window | **WAS THE GAP.** Pre-SWEEP-034: every RegisterClass* returned the SAME atom 0xC001; class lookup probed ANSI names as wide chars (never matched); dispatch fell back to the LAST-registered global channel | **FIXED (SW-1511/1512/1513/1513b) — measured:** putty 0.85 SIGSEGV 0x18aa → rc=0; RAMMap64 SIGSEGV 0x4cce → rc=0 + real 744x574 frame painted; G1 12/12; no corpus regressions |
| 9 | **WinSock error vector** — `getaddrinfo` failures surface as WSA codes (EAI_NONAME==WSAHOST_NOT_FOUND per ws2tcpip.h); `addrinfo_err_from_unix` converts | Wine `dlls/ws2_32/unixlib.c:446-475`, `include/ws2tcpip.h:83-91` | **WAS THE GAP.** Host-libc EAI_NONAME(-2) passed verbatim to guests | **FIXED (SW-1514) — measured:** putty now prints its own `Host does not exist` (win_strerror(11001) formats through our FormatMessageA) instead of `Error -2: (unable to format...)` |
| 10 | **Process teardown** — `LdrShutdownProcess` runs DLL_PROCESS_DETACH in reverse order | Wine loader.c | atexit/ExitProcess flush paths; DETACH model best-effort | PARTIAL — honest for the corpus; reverse-order walk not modeled |

## The structural rule this session extracted (user's level-0 method, applied)

**A Windows-compatible engine must model IDENTITY first, DISPATCH second.**
Every putty/RAMMap crash class traced this session decomposed to: an API that
answered with a wrong or colliding *identity* (one shared class atom; a class
proc resolved through the wrong encoding; a phantom window borrowing another
class's proc), after which *dispatch* delivered guest code through the wrong
pointer/parameters. Wine never crashes there because identity is per-object:
atoms are allocated per registration (putty under Wine: 0xc012/0xc013/0xc01b),
windows hold their own class proc, and a window that was never created
receives no messages.

Engine changes implementing that rule (all in `src/loader/loader.c`):

- **SW-1511** — encoding-correct class lookup (`mw_class_proc_lookup_a` /
  `mw_class_brush_lookup_a`): CreateWindowExA resolves classes with an ANSI
  probe; the 46-byte wide probe used to read `"PuTTYTimerWindow"` as garbage
  wide-chars, so the main-window proc snapshot silently failed and every
  dispatch fell to the last-registered channel.
- **SW-1512** — distinct registration atoms 0xC001+ (class registry rows now
  carry `atom`; `MAKEINTATOM` params resolve through `mw_class_proc_by_atom`).
- **SW-1513** — per-window dispatch: minted top-levels bind their own class
  proc (`mw_toplevel_bind_proc`); `mw_window_proc()` is the real
  DispatchMessage routing (subclass override → own class proc → legacy);
  pump `DispatchMessage`, `SendMessageW/A`, `send_create*`, `send_destroy`,
  `send_size_sync` all route through it.
- **SW-1513b** — the phantom main window has NO fallback proc: a guest that
  only creates dialogs (RAMMap64) must never see engine-generated messages
  addressed to a window it did not create.
- **SW-1514** — WinSock getaddrinfo error-vector mapping (EAI_* → WSA_*),
  per Windows `ws2tcpip.h` / Wine `unixlib.c`.

## Measured deltas (before → after, same binaries)

| guest | before | after |
|---|---|---|
| putty 0.85 | rc=139 SIGSEGV at RVA 0x18aa (`mov r13+0xbc8`, r13=GetWindowLongPtr(GWLP_USERDATA)=0) | **rc=0** — full init, terminal-window create, honest connect attempt, its own `Host does not exist` MessageBox, clean exit |
| RAMMap64.exe /accepteula | rc=139 SIGSEGV at RVA 0x4cce (`GetPropW(0x10001)->0` → `mov rax+8`) | **rc=0** — dialog init, props, frame create, WM_SIZE sync through its own class proc, 2 real GDI frames (744x574) |
| G1 regression suite | 12/12 | **12/12** (evidence/master/g1/20261030_082206) |
| corpus gate | 15 rows, rc=0 in 9, crash 0 | **15 rows, rc=0 in 9, crash 0** (identical; XP-SEED rows absent in this sandbox as before) |
| procexp64/procmon64/bginfo64/zoomit64/diskmon64/winmergeu/puttygen | rc=0 | rc=0 (unchanged) |
| notepad++ 8.6.9 | rc=9 (SW-1201 MFC /GS frontier) | rc=9 (unchanged — different frontier) |
| pageant | pump-hang TO | TO (unchanged) |

## Remaining structural frontier (registered as issues)

1. **Modal dialogs block forever headless** — Wine rc=TO on GUI-INIT apps;
   our DialogBox path returns immediately (auto-IDOK) which lets guests like
   putty proceed to their connection path without user input. Honest
   headless semantics = block in the modal loop like the pump does.
2. **RamMap child-model rendering** — frame + class-brush paint works; the
   SysListView32 children (0x30004 class props already flow) need the
   SW-1334 child-model painters to render content rows.
3. **Reverse-order DLL_PROCESS_DETACH walk** (Wine LdrShutdownProcess).
