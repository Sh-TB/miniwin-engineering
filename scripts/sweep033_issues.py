#!/usr/bin/env python3
"""Post SWEEP-033 evidence to GitHub issues; close fully-executed ones.
Token comes from git remote URL, never printed."""
import json
import subprocess
import sys

REPO = "Sh-TB/miniwin-engineering"
TOKEN = subprocess.run(
    ["git", "-C", "/home/z/my-project/workspace/miniwin-engineering",
     "remote", "get-url", "origin"],
    capture_output=True, text=True).stdout.strip()
TOKEN = TOKEN.split("https://Sh-TB:")[1].split("@")[0]

import urllib.request


def gh(method, path, body=None):
    url = f"https://api.github.com/repos/{REPO}/{path}"
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": f"token {TOKEN}",
        "Accept": "application/vnd.github+json",
        "Content-Type": "application/json"})
    with urllib.request.urlopen(req) as r:
        txt = r.read().decode()
    return json.loads(txt) if txt else None


EPIC = """## SWEEP-033 landed (15c770f): level-0 Wine relay campaign over **22 real programs** — BASE API derived

Method = the level-0 tracker approach: every program passed through Wine `WINEDEBUG=+relay` (wine-10.0 WineHQ noble build, restored after the Debian repack builds proved to abort at prefix init in this sandbox), **every call captured**, then diffed against the MiniWin loader's import-resolution sets.

**Corpus (sha256-pinned):** WinRAR.exe / Rar.exe / UnRAR.exe 7.12 (fresh from rarlab), putty, puttygen, pageant, procexp64, procmon64, bginfo64, rammap64, autoruns64, zoomit64, diskmon64, coreinfo64, ProduKey, cports, SearchMyFiles, WinMergeU, 7za, jq, 7z2604-x64 SFX, notepad++ 8.6.9.

**Measured level-0 totals:** 429k–973k relay calls per program — a *usage print alone* (7za) is 744,102 calls / 576 distinct APIs. GUI apps reached real message-pump calls headless (putty: RegisterClassW×26, GetMessageW×20, DispatchMessageW×18).

**Key finding for this EPIC:** MiniWin resolved **100% of static imports for ALL 22 apps** (unresolved = 0 across the board). The frontier is no longer import resolution — it is behavioral fidelity of the resolved APIs (putty/rammap64 SIGSEGV 139, notepad++ rc=9 MFC EH frontier, pageant pump-hang).

**BASE API (dynamic ∩ static surface): 349 APIs / 12 DLL families** — KERNEL32 135, USER32 83, ADVAPI32 46, MSVCRT 18, GDI32 14, NTDLL 14, SHELL32 8, COMCTL32 5, GDIPLUS 1, OLEAUT32 4, SETUPAPI 4, SHLWAPI 4...
Top core (apps exercising it): CloseHandle 22/22, GetProcAddress 22/22, GetStdHandle 21/22, WideCharToMultiByte 21/22, MultiByteToWideChar 20/22 (46,691 calls), HeapFree 17/22 (259,392 calls), CreateThread 17/22, GetCommandLineW 18/22.

Artifacts: `artifacts/sweep033/API_BASELINE.md` (+campaign.json, matrix-app-x-dll.tsv, 44 per-app JSONs). Honest gates: GUI-INIT apps = headless init-phase traces; infra-vs-app caller attribution = future work (relay samples kept per app)."""

SW1500 = """Closing with executed evidence — **SW-1500 (SWEEP-107 lineage, commit 4366a47)**:

- OBSERVED: CreateProcessW previously lied with ERROR_FILE_NOT_FOUND for every child spawn.
- IMPLEMENTED: MZ-probe (honest 193 BAD_EXE_FORMAT for non-PE), fork+execv(/proc/self/exe) nested PE load in isolated host process, MwProcess registry (16 slots), WaitForSingleObject on real host pid, GetExitCodeProcess STILL_ACTIVE(259)→waitpid reap, signal deaths→NTSTATUS mapping (SIGSEGV>0xC0000005, SIGILL>0xC000001D, SIGABRT>0x40000015, SIGFPE>0xC0000090), TerminateProcess with requested code, PROCESS_INFORMATION filled.
- TESTED: synthesized mw_proc_child.exe + hand-encoded x64 parent battery — CREATE=1 WAIT=0 **EXIT=42** (child really ran, exit code observable): `artifacts/sw1500/evidence/parent_battery.txt`. G1 12/12 PASS.
- VERIFIED: WinRAR 7.12 smoke a/t/x exit 0, extract SHA-256 byte-exact vs originals (no regression)."""

SW1501 = """Closing with executed evidence — **SW-1501 (commit 4366a47)**:

- OBSERVED: SetFileAttributesW was a no-op; GetFileAttributesW lied.
- IMPLEMENTED (Wine ntdll/unix/file.c model): 256-entry overlay keyed by normalized host path + chmod(2) READONLY write-back; GetFileAttributesW overlay-authoritative with host write-bit fallback; FILE vs PATH NOT_FOUND distinction via parent-dir stat; overlay removed on DeleteFileW.
- TESTED: parent battery SETATTR=1 ATTRS=33 ROBIT=1, and CREATEFILE=-1 honest ACCESS_DENIED against the read-only file from run 1. G1 12/12.
- F1-c / F1-d closed."""

SW1502 = """Closing with executed evidence — **SW-1502 + SW-1502b (commit 4366a47)**:

- OBSERVED: default value unreadable (R5-b); RegGetValueW flags ignored (R5-c/R5-d); opened-subkey handles always ERROR_FILE_NOT_FOUND.
- IMPLEMENTED: NULL name → L"" at query; RRF_RT_* type mask → ERROR_UNSUPPORTED_TYPE(5729); REG_EXPAND_SZ auto-expand via getenv, unknown %VAR% kept literal, expands-to-REG_SZ; RRF_NOEXPAND/RRF_ZEROONFAILURE honored; two-pass size probe; opened-key handles resolve through stored key path.
- TESTED: battery HK=0x90015 SETDEF=0 QDEF=0 TYPE=1 D0=100 D1=101; GETEXP=0 LEN2=26 %MWTESTPATH% expanded; UNSUP=5729 type-mask rejection. G1 12/12 + WinRAR smoke SHA-exact."""

FILE3 = """**GATE D re-verified again in SWEEP-033 (15c770f) + level-0 traces added.**

- Rar.exe / UnRAR.exe ran under the level-0 Wine relay: rar-console 614 distinct / 750,648 calls; unrar 612 / 747,831 — full dynamic call census of the real programs committed in `artifacts/sweep033/`.
- Prior evidence stands: S1–S7 8/8 checkpoints VERIFIED on BOTH engines (banner / create RAR v5 / list / t=All OK / extract / SHA-256 fixture equality / determinism), 0 confirmed workflow divergences (`evidence/wine-tracker/compare-*.json`), 560,574 api_call events from the 83MB bounded relay.
- Issue #4 hard-cap regression (MW_REG_MAXVAL=25 loop) fixed by SW-1504 grow-by-doubling store; console a/t/x SHA-256 re-verified byte-exact on the same commit (08f1c5a).

Acceptance criteria of [FILE-3] are met on both engines → closing."""

GFX3 = """Status update (SWEEP-032-b/c + SWEEP-033 level-0 view):

- SW-1320 landed REAL GDI+ raster: GdipCreateFromHDC binds target DC; Bresenham line/rect/ellipse-arc/pie outlines; even-odd polygon fills; bezier sampling; path adders (Line/Line2/Bezier/Arc/Ellipse/Rectangle, I+float) store real points; DrawPath/FillPath/DrawLines/DrawPolygon rasterize; DrawImage* real alpha-gated RGBA→surface blits; +30 flat-API ENTRYs incl. the 4 WinRAR names (GdipDrawRectangle/GdipAddPathArc/GdipAddPathLine/GdipBitmapSetPixel).
- Measured: WinRAR GUI `GdipDrawRectangle/AddPathArc/AddPathLine/DrawPath/DrawEllipse/DrawLineI/CreateFromHDC all IAT-resolved to the real raster impls`; main frame CreateWindowExW fires (1024×768 surface + STATUSBAR id=14 + SysListView32 id=11), no crash.
- NEW frontier (registered SW-1506, 08f1c5a): WM_PAINT not yet dispatched to guest proc post-init — byte-identical CustomExt mask-scan loop, window tree complete + pump armed, GetMessageW never reached. First x64 guest backtrace machinery (SW-1503) now exists to attack it.
- SWEEP-033 level-0 census: WinRAR GUI surface = 108 APIs exercised under wine (incl. the GDIPLUS family) — the behavioral target list for closing this issue.

Keeping open until the WinRAR GUI paints for real."""

WOW64 = """## [WOW64-1] PE32 (32-bit) guests have no load path — 14+ real programs blocked pre-load

**OBSERVED**
- All 14 NirSoft tools staged in SWEEP-062 Train 3 (driverview, usbdeview, faview, resourcesextract, iconsext, devmanview, simpleprogramdebugger, heapmemview, htmlastext, urlprotocolview, dnsquerysniffer, …) are machine 0x14c (PE32) and crash rc=139 in the PE32/x86 subsystem pre-load (`g_image_base=nil`, Signal 11 libc early-init). 14 staged reproducers under `artifacts/sweep062/`.
- IDM (Internet Download Manager) rejected at loader entry: `Not a PE32+ (magic=0x10b)`.
- SWEEP-033 base-API campaign corpus could not include any 32-bit program for the same reason.

**IMPACT**
~half of the "famous programs" universe the engine targets is PE32. Without a 32-bit load path the compatibility matrix is capped at x64-only.

**IMPLEMENTED so far**
- Honest rejection (not a lying stub): loader detects magic 0x10b and fails loudly — that is correct behavior for now, the missing part is the WOW64 subsystem itself.

**PROPOSED (Wine as reference)**
1. Wine's actual design: `dlls/ntdll/unix/virtual.c` + `loader.c` wow64 build — a 64-bit ntdll running 32-bit code through `wow64.dll`/`wow64cpu.dll` transition layers (source: wine-10.0 `dlls/wow64/`, `dlls/wow64cpu/`). A minimal port for our engine = 32-bit PE loader (already have PE64 one — share section/reloc code, add PE32 ImageBase/reloc quirks + stdcall thunks) + 32-bit→64-bit API shim table.
2. Alternative pragmatic first step: PE32 support for **console** apps only (no GUI thunking) — 7za32/NirSoft console tools would then run.
3. Track separately: mingw-w64 i686 toolchain availability in sandbox for cross-checks (exists in Debian as `gcc-mingw-w64-i686`).

**TEST:** first acceptance = any staged NirSoft 32-bit tool exits rc=0 with real usage output; second = IDM passes the magic check and reaches its first unresolved-import census."""

for num, body, close in [(14, EPIC, False), (16, SW1500, True), (9, SW1501, True),
                          (8, SW1502, True), (18, FILE3, True), (17, GFX3, False)]:
    gh("POST", f"issues/{num}/comments", {"body": body})
    if close:
        gh("PATCH", f"issues/{num}", {"state": "closed",
           "state_reason": "completed"})
    print(f"issue #{num}: commented{' + closed' if close else ''}")

r = gh("POST", "issues", {
    "title": "[WOW64-1] PE32 (32-bit) load path missing — 14 NirSoft tools + IDM blocked pre-load",
    "body": WOW64,
    "labels": ["enhancement", "pe-loader", "wine-reference", "priority-high"]})
print("created issue", r["number"])
