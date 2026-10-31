# SWEEP-019 — (continue dozens of tasks at once and push to GitHub) (2026-10-19)

User directive: execute the queued SW-816..880 continuation queue (65 items,
(triple-parity continuation) x3 of the SW-817..850 list) as ONE elephant batch — dozens
of tasks simultaneously — with a measured progress table and a push to GitHub
at the end ((changes without measured progress are worthless): no change without measured
progress).

## What landed (13 families, 74 new imports resolved + 1 family fix)

Starting point: `main = 7994eaa` (SWEEP-018), clean tree, G1 pinned
`616a9ba1…`. This sweep is pure loader.c work plus its scripts/evidence:

1. **SW-816..820 menu family x5** — full menu-item storage model:
   `MwMenu g_menus[8]` x 16 items (id, state, text, submenu); CreateMenu /
   CreatePopupMenu / DestroyMenu (slot free, SW-812 pump-loop prerequisite) /
   AppendMenuA/W (MF_POPUP links submenu handles) / ModifyMenuA/W
   (MF_BYPOSITION + MF_BYCOMMAND) / GetMenuItemCount / GetSubMenu /
   GetMenuItemInfoA/W (MIIM_STATE/ID/SUBMENU/STRING, real MENUITEMINFO x64
   offsets 0/4/8/12/16/24/56/64, cbSize==72 enforced). LoadMenuW/A rebuilt on
   the same allocator (DestroyMenu now actually frees what LoadMenu took).
2. **SW-821..825 clipboard x5** — single headless clipboard: OpenClipboard /
   CloseClipboard (close-without-open = ERROR_INVALID_HANDLE) /
   EmptyClipboard / SetClipboardData / GetClipboardData (fmt-matched) +
   IsClipboardFormatAvailable.
3. **SW-826..830 common dialogs x5** — comdlg32.dll: GetOpenFileNameA/W,
   GetSaveFileNameA/W, ChooseColorA/W, FindTextA/W, ReplaceTextA/W,
   PageSetupDlgA/W — canned user-CANCEL returns (0/FALSE/NULL,
   CommDlgExtendedError()==0), the clean no-op path real programs implement.
4. **SW-831..840 ListView/TreeView/Tab x5+x5** — ROOT CAUSE fixed:
   `mw_sysclass_a/w` only knew 8 built-in classes, so a guest-created
   SysListView32 got sc=0 and fell into the main-window path — the LVM_*
   messages were NEVER seen by any child model. Now recognised (0x88..0x8B):
   SysListView32 / SysTreeView32 / SysTabControl32 / SysHeader32, registered
   as ordinary children, with lazy per-slot state and real message routing in
   BOTH SendMessageW and SendMessageA:
   - LVM_GETITEMCOUNT / INSERTITEM A+W (iItem==-1 append, text stored) /
     INSERTCOLUMN A+W / GETITEMTEXT A+W (LVITEM x64 offsets: iItem@4,
     iSubItem@8, state@12, pszText@24, cchTextMax@32) / SETITEMSTATE / HITTEST
     (typed -1: no pixel layout in the headless engine).
   - TVM_INSERTITEM A+W (MW_TREE_BASE pseudo-handles) / GETCOUNT /
     GETNEXTITEM (TVGN_ROOT only).
   - TCM_INSERTITEM A+W / GETITEMCOUNT / SETCURSEL / GETCURSEL.
   - HDM_GETITEMCOUNT / HDM_INSERTCOLUMN A+W.
5. **SW-841..845 GDI regions x5** — `MwRgn[32]` x 16 rects, real rect
   algebra: CreateRectRgn / CreateRectRgnIndirect / CombineRgn (RGN_OR
   concatenates rect lists, RGN_AND pairwise intersection, RGN_DIFF
   rect-minus-bbox slabs, RGN_XOR = DIFF both ways, RGN_COPY; returns
   NULLREGION/SIMPLEREGION/COMPLEXREGION) / SelectClipRgn (per-DC clip map) /
   RectInRegion (exact intersection scan) + GetUpdateRgn (user32;
   NULLREGION — no pending updates headless). Rect-based approximation (no
   y-sweep) is documented at the block head; the type codes are real.
6. **SW-846..850 printing x5** — the old tail LIED backwards: StartPage/
   EndPage/EndDoc returned -1 (job failure) which bginfo64 treats as a fatal
   print error. Now an honest no-op printer: StartDocA/W -> job id 1, page/
   doc lifecycle succeeds, AbortDoc added (family: StartDocA/W, StartPage,
   EndPage, EndDoc, AbortDoc).
7. **SW-851..855 shell x5** — shell32.dll: SHGetSpecialFolderPathA/W
   (scratch dir `$MINWIN_SHELL_ROOT/shellfolders/csidl_%02x`, mkdir'd on
   create=TRUE — the produkey GetProcAddress-NULL gate) /
   SHBrowseForFolderA/W (cancel) / SHGetFileInfoA/W (honest 0: no shell icon
   DB) / ShellExecuteA/W (existence-checked: file exists -> 42 (>32 success
   convention), missing -> SE_ERR_FNF=2) / ShellExecuteExA/W (writes
   hInstApp@52) / SHGetMalloc (malloc-backed IMalloc vtbl — QI/AddRef/
   Release/Alloc/Free/Realloc/GetSize/DidAlloc/HeapMinimize).
8. **SW-856..860 network x5** — mpr.dll added to g_known_dlls: WNetOpenEnum
   A/W -> ERROR_NO_NETWORK(225) with NULL handle, WNetCloseEnum -> WN_SUCCESS
   (nothing to release), WNetEnumResource A/W -> 225/0-count; ws2_32:
   getnameinfo -> real libc getnameinfo (getaddrinfo_ twin), WSAEnumProtocols
   A/W (typed WSAENOBUFS sizing handshake, 0 providers headless).
9. **SW-861..865 CRT tail x5** — msvcrt: _mbsicmp (case-folded byte compare),
   _mbschr, _memicmp, _strlwr, _snprintf (BUG-063 discipline: ms_abi varargs
   -> __builtin_ms_va_list -> classify/slot-repack -> mw_printf_emit through
   fmemopen; MSVCRT truncation contract: -1 when count exceeded).
10. **SW-866..870 COM completion** — CoCreateInstanceEx / CoGetClassObject
    (same honest REGDB_E_CLASSNOTFOUND policy as CoCreateInstance, single
    headless apartment) / CoTaskMemAlloc / CoTaskMemFree / CoCreateGuid
    (/dev/urandom, GenRandom pattern). CoSetProxyBlanket + CoUninitialize
    pairing audited: unchanged (already honest).
11. **SW-871..875 perf/debug x5** — ReadProcessMemory / WriteProcessMemory:
    the guest PE runs in THIS process, so both are range-checked memcpys
    against [g_image_base, +g_image_size) with ERROR_PARTIAL_COPY(299)
    outside; the write path mprotects pages RW for the copy duration and
    restores R+X (code pages are mapped R-X). SuspendThread/GetThreadContext
    /SetThreadContext already honest; ResumeThread entry bound to the
    existing definition (duplicate deleted).
12. **SW-876..878 registry A-tail x3** — RegDeleteValueA / RegEnumValueA /
    RegQueryInfoKeyA: A-twins mirroring the W-side honest contracts
    (2/259/6 + SetLastError), wrapped not duplicated.
13. **SW-879..880 closure** — G1 re-pin + measured sweep table (below).

## G1 re-pin

- binary sha256 `2f5535f254a2819e11dc6190ce91c74932171baabdf5517afd0d0965d8ff3774`
- UPX banner exact, no [CRASH], regression **12/12**
  (evidence/master/g1/20261019_153111). Build grew 1,656,608 -> 1,771,176
  bytes (+114 KB of new honest APIs).

## Measured sweep (mandate: every commit ships a progress table)

`artifacts/sweep019/sweep/sweep019_sweep.tsv` — 16-program corpus x3
pump-off + produkey pump-on + calc/snake/breakout x3. Results (deterministic
x3 — every run byte-identical per program):

| program | exit x3 | stubs | unresolved | vs SWEEP-018 |
|---|---|---|---|---|
| autoruns64 | 0/0/0 | 0 | 151 | parity (0) |
| bginfo64 | 0/0/0 | 2 | **310** | **-26 imports** (336 -> 310), exit parity |
| coreinfoex64 | 0/0/0 | 0 | 0 | parity (0) |
| cports | 0/0/0 | 0 | 49 | parity (0) |
| diskmon64 | 0/0/0 | 0 | 21 | parity (0) |
| produkey | 0/0/0 | 0 | 39 | parity (0) |
| rammap64 | 0/0/0 | 0 | 39 | parity (0) |
| pageant | 1/1/1 | 0 | 31 | parity (1, SW-554) |
| procmon64 | 1/1/1 | 0 | 139 | parity (1) |
| putty | 1/1/1 | 0 | 94 | parity (1) |
| zoomit64 | 1/1/1 | 0 | 0 | parity (1, TRAP133 SW-553) |
| procexp64 | 139 x3 | 0 | 277 | parity (139, SW-809 open) |
| puttygen | 139 x3 | 0 | 23 | parity (139, SW-807 open) |
| searchmyfiles | 139 x3 | 0 | 49 | parity (139, SW-814 open) |
| winmergeu | 139 x3 | 0 | 320 | parity (139, SW-815 open) |
| produkey pump-on | 1 | 0 | 0 | SW-812 remains open -> SW-881..885 |

**Zero regressions**: all 15 exit states identical to the SWEEP-018 table;
measured forward progress = bginfo64's 26 newly-resolved imports at the same
exit state, +74 new named imports available to every program (IAT resolution
count grows for any guest that imports the 13 families).

**GUI battery**: calc acceptance **9/9 PASS** (sweep014_calc_accept.sh),
snake/breakout x3 deterministic — micro-game 100% runnable unchanged. GUI
exit=1 rows are the normal deterministic headless teardown (the acceptance
script, not exit code, is the calc contract).

## Honest notes

- produkey pump-on (SW-812 menu-loop) still exits 1 — the menu family gives
  the loop its DestroyMenu/readback APIs, but the loop itself needs the
  SW-881..885 menu deepening queue (TrackPopupMenuEx/InsertMenuItem family).
- The 139 group (procexp64/puttygen/searchmyfiles/winmergeu) is unchanged —
  their signatures are recorded in SW-807/809/814/815 and carried in the new
  SW-881..920 queue.
- The 3 GB host kills long background batches; the sweep runs as four
  foreground chunks (scripts/sweep019_chunk.sh) — same semantics as the
  monolithic sweep018 script, just chunked for the box.
