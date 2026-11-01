# L0 MISSION TRACKER — Universal Wine Level-0 Differential Engine + WinRAR E2E

Branch `wine-l0` (origin/main `8663b4f` + wagons a69f9c5 → 2424c0a).
Everything below carries measured evidence; nothing is claimed beyond it.

## Infra facts

| Item | Status |
| --- | --- |
| Wine availability | **OBSERVED**: userspace Wine 10.1 (`tools/wine-10.1-amd64`, Kron4ek wine64; 10.2 build dies under sandbox seccomp; 32-bit launcher needs i386 libs → `wine64` binary is the supported path) |
| apt/root | BLOCKED (no passwordless sudo) — userspace tarball route instead |
| GitHub issues | BLOCKED this sandbox (no gh; API rate-limited) — no duplicate issues created |
| Sandbox recycle | dropped the 5 pre-recycle audit commits + untracked WinRAR 7.23 dl/ tree; origin/main independently absorbed most of that work (SW-1501); findings re-landed as verified wagons here |

## Wagons landed

| Wagon | Content | Status |
| --- | --- | --- |
| W1 `a69f9c5` | IMalloc contract completion on the merged engine: IID_IMalloc byte fix (0xDA→0x18, BOTH providers), Alloc(0) non-NULL, GetSize unknown→(SIZE_T)-1, CoGetMalloc MEMCTX_TASK contract, single shared allocator instance (Wine identity) | IMPLEMENTED+TESTED (t13 33/33 exit 0, real import boundary) |
| W2 `e7e4b9c` | `scripts/analysis/l0_tracer.py` universal Level-0 tracer: 7 channels, disposable per-capture prefixes, byte+time caps, per-TID stack pairing with ret-address validation/repair, module-map attribution (app/guest_dll/wine), replay hashes | IMPLEMENTED+TESTED (12/12 unit; real Wine 10.1 captures) |
| W3 `a953b5c` | `l0_normalize.py` unified schema + differential engine (missing/extra/frequency/retval-class/first-divergence) | IMPLEMENTED+TESTED (UPX report: 0 retval mismatches after pointer-class fix) |
| W4 `a41ae1d` | LoadLibraryEx contract re-land (NULL→87/empty→126/datafile flags→typed 126; ExW NULL distinction) + CW_USEDEFAULT 0x8000 sentinel (Wine is_default_coord) + L0_XVFB + matrix runner | IMPLEMENTED+TESTED (t14 19/19; G1 PIN 064632/064826) |
| W5 `2424c0a` | L0 tool test suite (12/12) + replay-consistency metrics (per-TID sequence hash) | TESTED (x3: 2 identical, 1 wineserver loader-lock race — reported, not hidden) |
| W7 `e724838` | SW-1571 WinRAR toolbar end-to-end: oracle re-capture (L0_XVFB discipline root-cause), TBMSG protocol visibility -> FULL Wine parity measured, toolbar-art renderer (app's own decoded PNG art by TBBUTTON.iBitmap), WS_CLIPCHILDREN emulation, first-pump WM_SIZE, SIGTERM final frame, 0x44D wide+idempotent, fsState round-trip; t15 (33/33) + t16 (18/18) regression PEs | IMPLEMENTED+TESTED (G1 12/12 x2; battery green x3 programs) |

## Universality matrix (Phase 5)

| App | Wine capture | MiniWin run | First meaningful divergence |
| --- | --- | --- | --- |
| UPX 4.2.4 console | 22.4k lines, 11.2k pairs | exit 0 | `RtlAddVectoredExceptionHandler` (coverage/behavior nuance, flagged) |
| 7-Zip 25.01 7za x64 | 30.8k events | exit 0 (archive created) | `msvcrt.__set_app_type` |
| putty (GUI, xvfb) | captured | alive | `KERNEL32.LoadLibraryExW` — FIXED by W4 |
| ProduKey (GUI, xvfb) | captured | alive | **none** — every Wine app-attributed function also called under MiniWin |
| autoruns64 (Sysinternals GUI) | captured | alive | `KERNEL32.LoadLibraryExW` — FIXED by W4 |
| Notepad++ 8.6.9 x64 (GUI) | captured | alive | `KERNEL32.LoadLibraryExW` — FIXED by W4 |

Evidence: `artifacts/l0-matrix/<app>/` (wine captures, miniwin logs, diffs).

## WinRAR 7.12 end-to-end (Phase 4) — honest state

| Aspect | Status |
| --- | --- |
| Oracle capture | **OBSERVED**: real Wine 10.1 + Xvfb, 50MB relay, 358k events, module map; key child windows `ReBarWindow32` → `ComboBoxEx32` → `ToolbarWindow32`×2 → `msctls_statusbar32` → `SysListView32`; toolbar populated by **TB_ADDBUTTONSW (0x444) ×11**, TB_SETEXTENDEDSTYLE ×20, ImageList_Create ×5 (32/16/48/256px ILC_COLOR32), ImageList_ReplaceIcon ×20, GDI+ `GdipCreateBitmapFromStream` decode chain; address bar via `SHAutoComplete` |
| MiniWin render | **OBSERVED**: alive (exit 124, continuous frames), real title `C:\ (evaluation copy)` + real WinRAR icon, accent caption, **REAL menu** (engine menu handle 0x50001, app-fed items), WinRAR's own **dialog template** instantiated (child `COMBOBOX` id=16 = address bar, `SysListView32` id=11 = file list) — real application-generated UI from the app's resources, 131 distinct colors (themed) |
| Toolbar window | **IMPLEMENTED — app-generated (SW-1571, W7)**: the guest itself calls CreateWindowExW(ToolbarWindow32 id=12/13) (site-instrumented proof) and drives the FULL TB protocol at measured Wine parity (0x41E/0x41F/0x420 x1, 0x413 x20, 0x44D x20, 0x417 x11, 0x444 x10+1, 0x41D x12, 0x418 x5). The engine renders the app's OWN decoded PNG button art by TBBUTTON.iBitmap — evidence/w7_toolbar/frame_tbar_visible.bmp (Add/Extract/Test/View/Delete/Find/Wizard/Info/Repair, zero hardcoding). Supersedes the pre-Wagon-24 'never sends TB_ADDBUTTONSW' state and the 'engine chrome strip' note |
| 509/509 imports resolved, 0 unresolved | OBSERVED (no stub swallowing involved) |
| Registry | WinRAR's own probes behave (HKCU\Software\WinRAR miss = fresh install on both sides) |

Next frontier (registered SW-1572, not started): rebar/combo height model —
the guest's own layout computes the rebar at 2->3->4px (+1 rounds; Wine oracle
MoveWindow(rebar)=83px), so the file-list pane lands at y=24 and covers the
toolbar strip in the settled frame. The toolbar render is proven by the
mid-composite frame; SW-1572 turns it into the final state.

## G1 discipline

G1 PIN re-run after every engine wagon: `evidence/master/g1/20261101_062338`
(wine-l0 base), `_064632` + `_064826` (after W4) — 12/12 each time.
