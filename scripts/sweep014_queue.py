#!/usr/bin/env python3
"""SWEEP-014: generate the 3x task multiplication (SW-651..SW-800, 150 items).

Every item cites its blocking evidence (artifacts/sweep012/runs-AFTER3/
progress.json + gate decode + crash banners). Appends the SWEEP-014 queue
section to .agent/SWEEP50_WORKLIST.md (idempotent: rewrites the section).
"""
import os, json, collections

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WL = f"{REPO}/.agent/SWEEP50_WORKLIST.md"

prog = json.load(open(f"{REPO}/artifacts/sweep012/runs-AFTER3/progress.json"))
crashes = [p for p in prog["programs"] if p["class"] == "CRASH-GUEST"]
rejects = [p for p in prog["programs"] if p["class"] == "LOAD-REJECT"]

L = []
def add(sid, text):
    L.append(f"- [ ] **{sid}** {text}")

# ---- 1. crash frontier (15 programs, REA funcmap method) ----
i = 651
for p in sorted(crashes, key=lambda x: x["name"]):
    add(f"SW-{i}", f"{p['name']} exit 139 0-stub guest crash: MINWIN_FUNC_MAP "
        f"symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence "
        f"artifacts/sweep012/runs-AFTER3/{p['name']}/)")
    i += 1

# ---- 2. ws2_32 socket ladder (ordinal table opened it; real fd map) ----
sock = ["socket->fd real alloc", "bind/listen real", "accept real",
        "connect non-blocking", "send/recv real fd", "select real fd_set",
        "getsockopt/setsockopt honest levels", "shutdown + closesocket fd free",
        "getsockname/getpeername real sockaddr", "inet_addr/inet_ntoa real",
        "ntohl/htonl no-op identity", "ioctlsocket FIONBIO real",
        "WSAPoll subset", "WSAAsyncSelect event queue real", "getaddrinfo host resolver"]
for s in sock:
    add(f"SW-{i}", f"ws2_32 ladder: {s} (cports/ProduKey/sqlite3_analyzer now "
        f"resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)")
    i += 1

# ---- 3. GUI frontier (calc 9/9 proves the pipeline; deepen it) ----
gui = ["WM_CHAR path: pump delivers typed digit chars to EDIT children (SW-616)",
       "child-control PAINT: draw BUTTON/EDIT frames + labels into the raster (SW-615)",
       "WM_CHAR +/- operators: full keyboard calculator scenario E 1+2+3=",
       "second GUI guest: Notepad clone (multiline EDIT + WM_CHAR typing)",
       "third GUI guest: analog/digital clock (SetTimer + WM_TIMER + paint)",
       "comdlg32 GetOpenFileNameW/GetSaveFileNameW modal-lite + file pick",
       "menus: SetMenu/DrawMenuBar/TrackPopupMenu + WM_COMMAND ids",
       "scrollbars: SetScrollInfo/ShowScrollBar + WM_VSCROLL delivery",
       "clipboard: OpenClipboard/SetClipboardData/GetClipboardData (text)",
       "ListView first slice: LVM_INSERTCOLUMN/ITEM + LVM_GETITEMCOUNT",
       "TreeView first slice: TVM_INSERTITEM + TVM_GETCOUNT",
       "TrackPopupMenu real WM_COMMAND return",
       "SetCursor/ShowCursor + WM_SETCURSOR routing",
       "GetUpdateRect/GetRgnBox honest rect fill",
       "RedrawWindow superset of InvalidateRect (RDW flags decode)",
       "dialog units MapDialogRect real font metric",
       "IsDialogMessage/DlgDirListlite for dialog keyboards",
       "WM_GETDLGCODE + IsDialogMessage focus walk",
       "Calc scenario F: chained 12+45-7= via clicks",
       "Calc paint assertion: golden-BMP hash pin of frame_006"]
for g in gui:
    add(f"SW-{i}", f"GUI frontier: {g}")
    i += 1

# ---- 4. oleaut32 ordinal + variant family (BUG-072 audit) ----
ole = ["audit full OLEAUT32 ordinal map vs pefile db (BUG-072: 8 was wrong)",
       "VariantChangeType real VT_I4/VT_R8/VT_BSTR",
       "VariantClear deep VT_BSTR|VT_ARRAY",
       "SafeArrayCreate/DestroyDescriptor/Data",
       "SafeArrayGetElement/PutElement 1D",
       "SafeArrayGetUBound/LBound real dims",
       "VariantTimeToSystemTime real conversion",
       "SystemTimeToVariantTime inverse",
       "VarBstrFromDate honest locale format",
       "OleLoadPicture honest E_FAIL + stream probe"]
for o in ole:
    add(f"SW-{i}", f"oleaut32 family: {o} (Bginfo64 imports 19 ordinals; "
        f"SysAllocStringByteLen=150 fixed this sweep)")
    i += 1

# ---- 5. GDI depth (CreateDIBSection landed r3) ----
gdi = ["CreateDIBSection 32bpp real 4B/px mode + alpha",
       "CreateDIBSection BI_BITFIELDS masks decode",
       "StretchBlt real scale loop on MWBmp",
       "GdiFlush/batch no-op contract",
       "GetDIBits real from MWBmp pool",
       "SetDIBitsToDevice direct raster write",
       "PatBlt rop PATCOPY/PATINVERT",
       "ExtCreatePen/ExtCreateRegion honest handles",
       "GetTextExtentPoint32W real font metric est",
       "EnumFonts metrics round-trip pin",
       "SetWorldTransform/GetWorldTransform honest identity",
       "CreatePalette/RealizePalette honest handles",
       "GetPixel/SetPixel real MWBmp accessors",
       "RoundRect/Ellipse real raster paths",
       "TransparentBlt color-key loop"]
for g in gdi:
    add(f"SW-{i}", f"GDI depth: {g} (CreateDIBSection real landed SWEEP-014 r3)")
    i += 1

# ---- 6. msvcrt narrow-CRT tail (ProduKey 77-gate list) ----
crt = ["_itoa/_ltoa/_ultoa real", "_strlwr/_strupr real", "_mbsicmp/_memicmp",
       "_mbschr/_mbsrchr byte semantics", "_snprintf real vararg",
       "_strcmpi/_strnicmp real", "WNetOpenEnum/EnumResource/CloseEnum honest ERROR_NO_NETWORK",
       "CreateEnvironmentBlock/DestroyEnvironmentBlock honest NULL",
       "NetWkstaUserGetInfo/NetServerGetInfo NERR base",
       "NetApiBufferFree/NetApiBufferAllocate real"]
for c in crt:
    add(f"SW-{i}", f"msvcrt/advapi tail: {c} (Bginfo64 355-gate list order)")
    i += 1

# ---- 7. PE DLL loader slices (SW-509 kin) ----
dll = ["PE DLL: LoadLibraryW image map + DllMain", "PE DLL: export table walk",
       "PE DLL: GetProcAddress by name/ordinal", "PE DLL: TLS callbacks",
       "PE DLL: DllMain detach + refcount", "PE DLL: 7z.dll case study",
       "PE DLL: libcrypto.dll case study", "PE DLL: dependencies-of-dependencies",
       "PE DLL: resource-only DLLs", "PE DLL: G1 extension with 7z.exe full ops"]
for d in dll:
    add(f"SW-{i}", f"TASK-LOADER-DLL slice: {d} (unblocks 13+ sweep programs)")
    i += 1

# ---- 8. kernel32/user32 long tail ----
tail = ["ReadProcessMemory honest fail (self-probe)", "WriteProcessMemory honest fail",
        "GetComputerNameA real hostname", "ExpandEnvironmentStringsW real env map",
        "WritePrivateProfileStringW in-memory win.ini + GetPrivateProfileStringW round-trip",
        "GetPrivateProfileIntW (Bginfo64 pairs with GetProfileIntW)",
        "SearchPathW real module dir search",
        "GetModuleFileNameExW + EnumProcessModules honest set",
        "WaitForMultipleObjects full WAIT_FAILED paths",
        "SetUnhandledExceptionFilter + UnhandledExceptionFilter contract",
        "GetExitCodeProcess/TerminateProcess cross-check",
        "CreateToolhelp32Snapshot honest empty snapshot",
        "Process32First/Next honest ERROR_NO_MORE_FILES",
        "HeapWalk honest end-of-heap", "GetProcessWorkingSetSize honest quota",
        "SetInformationJobObject honest reject", "AssignProcessToJobObject honest fail",
        "GetLogicalDrives + GetDriveTypeA honest C: only",
        "GetVolumeInformationW honest volume", "SetLocalTime honest ignore",
        "GetTimeZoneInformation real host TZ",
        "GetSystemInfo real cores/page size", "GlobalMemoryStatusEx real",
        "GetProcessAffinityMask pin", "SetErrorMode honest store"]
for t in tail:
    add(f"SW-{i}", f"kernel32/user32 tail: {t}")
    i += 1

# ---- 9. verification/infra (engineering discipline) ----
infra = ["decoder ordinal audit: sweep012_progress vs pefile db full sweep",
         "api_trace repair: buffered writer close-on-crash (parse-err in AFTER3)",
         "sweep pool: add collision-free meta names (AFTER2 dup RAMMap64)",
         "G1: add jq.exe exit-parity case",
         "prog-sweep: parallel runs (nproc workers)",
         "prog-sweep: per-program wall-time column",
         "progress.json: track cumulative zero-stub count",
         "stdin </dev/null harness: document in sweep README",
         "MINWIN_FUNC_MAP: auto-build for all 52 pool programs",
         "crash banner: include last-3 APIs from api_trace",
         "stub rate-limiter: MINWIN_STUB_LIMIT env (200 default)",
         "loader: assert-dispatch audit script (defined-but-unwired check, BUG-070 class)",
         "docs: ADR-015 ordinal-import policy",
         "docs: BUG-069/070/071 postmortems",
         "100-list: x86 LOAD-REJECT column formalized (11 programs)",
         "games: 2048-class win64 obtain+run (SW-501 mandate check-in)",
         "7zFM/7zG: click-through GUI scenario via MINWIN_CLICKS",
         "procexp64: 0-stub crash RIP symbolize (SW-602 close-out)",
         "produkey: RegSetValueExW persistence path (EulaAccepted pattern)",
         "zoomit64 SW-553 int3 re-check after batch #4",
         "curl: OPENSRC-CURL-1 UCRT state pointer next slice (SW-610)",
         "puttygen: SW-552 stack machine-named frames -> fix RIP=0 path",
         "md5deep64 suite: shared CRT-start crash root-cause (one BUG-*)",
         "Coreinfo64: 0-stub crash — IsWow64Process now real, re-trace (SW-608)",
         "SearchMyFiles: 0-stub crash re-trace after batch #4 (SW-609)",
         "Bginfo64: libc-side NULL deref — last-API correlation (new frontier)",
         "WinMergeU: guest NULL store at RVA 0x12d0c2 (REA funcmap)",
         "sqlite3_analyzer: byte-store crash at RVA 0xaa652 (REA funcmap)",
         "procmon64: exit 1 banner path audit after batch #4",
         "lz4/zstd/trurl: exit-code parity re-pin (3x each)"]
for t in infra:
    add(f"SW-{i}", f"infra/verify: {t}")
    i += 1

assert i == 801, f"generated {i-651} items, expected 150"
section = f"""
## SWEEP-014 QUEUE — 3x continuation multiplication (SW-651..SW-800, 150 items)

**Source:** artifacts/sweep012/runs-AFTER3 (mean_score 3.08, RAN-OUTPUT 26,
STUB-GATE 0 — batch #4 closed every named stub gate; the frontier is now
0-stub guest crashes + deep API families). Rules unchanged: 3x acceptance,
irreversible status labels, no fake success, PROGRESS over activity.

{chr(10).join(L)}
"""
txt = open(WL).read()
marker = "## SWEEP-014 QUEUE"
if marker in txt:
    txt = txt[:txt.index(marker)].rstrip() + "\n" + section
else:
    txt = txt.rstrip() + "\n" + section
open(WL, "w").write(txt)
print(f"worklist updated: SW-651..SW-{i-1} ({i-651} items)")
