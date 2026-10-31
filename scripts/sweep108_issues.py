#!/usr/bin/env python3
"""SWEEP-108: register the round evidence on the per-program acceptance issues
(#23 WinRAR / #24 Notepad++ / #25 IDM) + the generic gap issues.
Token comes from the git remote URL, never printed."""
import json, subprocess, urllib.request, base64

REPO = "Sh-TB/miniwin-engineering"
TOKEN = subprocess.run(
    ["git", "-C", "/home/z/my-project/workspace/miniwin-engineering",
     "remote", "get-url", "origin"],
    capture_output=True, text=True).stdout.strip()
TOKEN = TOKEN.split("https://Sh-TB:")[1].split("@")[0]

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

def upload(name, path):
    with open(path, "rb") as f:
        b64 = base64.b64encode(f.read()).decode()
    r = gh("POST", "contents/artifacts/sweep108/evidence/" + name, {
        "message": "SWEEP-108 evidence: " + name,
        "content": b64, "branch": "main"})
    return r["content"]["html_url"] if r and "content" in r else "(upload pending)"

common = """**SWEEP-108 (2026-10-30, binary 2bb1586a, G1 12/12 + corpus 15 rows rc=0 in 9 crash 0 — zero regressions).**

Engine changes this round (all generic, wine-referenced): SW-1515 drive-path
contract restored + `\\\\?\\` verbatim re-map (wine ntdll/unix/file.c); SW-1516
GetCurrentDirectoryW/A unified on the `C:\\` drive plane (wine
kernelbase/path.c — the host path leak is gone); SW-1517 u16len probe 4-byte
wchar_t bug; SW-1518 FreeLibraryAndExitThread = FreeLibrary+ExitThread (wine
kernelbase/thread.c) — the old ExitProcess mapping killed every guest whose
worker DLL finished + skeleton seeding on the find path; SW-1520 GUI content
plane (statusbar/toolbar/combo content + metrics per wine commctrl.h, menu
band as non-client); SW-1521 ReBarWindow32 → TOOLBAR identity; SW-1522
hierarchy-aware registry miss codes (parent missing → ERROR_PATH_NOT_FOUND(3),
leaf → 2; wine advapi32/registry.c)."""

bodies = {
 23: common + """

### WinRAR 7.12 (this program's row of the round)

| surface | before | after |
|---|---|---|
| official SFX installer chain | rc=6 "Errors encountered", 0 files (CreateFileW self-reopen ENOENT — SW-1509 regression) | **INSTALL_OK rc=0 — 28 files / 9.2 MB into `Program Files/WinRAR/`, WinRAR.exe sha 7415b18099 byte-verified** |
| installed GUI | SPIN_AT_TIMEOUT frames=0 (SW-1506, overtaken) → self-exit rc=0 in ~1 s (temp-cleanup thread's FreeLibraryAndExitThread = engine ExitProcess) | **ALIVE_PUMP rc=124 ×5 deterministic** — full control tree (rebar toolbar + combo + toolbar + listview 4 columns + statusbar), pump alive at timeout |

### The graphical image (the user's ask)

The frame now renders the REAL WinRAR chrome: the menu band
(File Commands Tools Favorites Options Help — texts from the RT_MENU resource),
sunken listview pane with the client plane honoring the non-client band.
Evidence: artifacts/sweep108/evidence/winrarinstd_frame_001.png (1024x768).

### Remaining frontier (honest): SW-1523
Toolbar buttons still absent — the button-add flow reads
`General\\Toolbar\\Layout` (miss 3), `Toolbar` (miss 2) and scans
`Toolbar\\Buttons\\*` 160× on the config thread; the main thread's
TB_ADDBUTTONSW never fires. Next round: MW32_STEP_TRACE decode of the
toolbar-init region with the SW-1503 backtrace tool.""",
 24: common + """

### Notepad++ 8.9.8.1 portable x64 (this program's row of the round)

**PASS_INPUT_STATE_CHANGE rc=0, frames=35 uniq=16** — re-verified on the
rebuilt path/cwd/message planes with ZERO regressions. The class model +
per-window dispatch (SW-1511..1513) and the content plane changes left the
render untouched. Evidence: plugins/status.tsv (sweep108 rows), run log +
api_trace under evidence/sweep108/.""",
 25: common + """

### IDM 6.42 build 2 installer (this program's row of the round)

PASS_LAUNCH_RENDER rc=0, 1 real dialog frame rendered ("IDM Setup — Could not
create temp dir in unpacker … Error ID: 2" — the Setup message box with title
bar, icon and OK button drawn by the GDI-lite raster). The 32-bit payload's
child-exec frontier (SW-1401) and the cpu32 run path (steps=77269 exit 0,
SWEEP-034) are unchanged. The remaining blocker stays WOW64/PE32 (issue #22
tracks the loader face); the temp-dir contract improved via SW-1518b
(skeleton seeding now covers enumeration too).""",
}

for num, body in bodies.items():
    r = gh("POST", f"issues/{num}/comments", {"body": body})
    print(f"issue #{num}: comment {r.get('id')}")

# frame evidence upload
try:
    url = upload("winrarinstd_frame_001.png",
                 "evidence/sweep108/winrarinstd/frame_001.png")
    print("frame uploaded:", url)
except Exception as e:
    print("upload failed:", e)

# register the two new generic issues (toolbar frontier + this round's contract set)
n = gh("POST", "issues", {
  "title": "[GFX-3] SW-1523: WinRAR GUI toolbar buttons never added (TB_ADDBUTTONSW absent)",
  "body": """Program: WinRAR 7.12 installed GUI (winrarinstd). Round: SWEEP-108 (binary 2bb1586a).

OBSERVED (api_trace, evidence/sweep108/winrarinstd/):
- Toolbars mint: ReBarWindow32 → TOOLBAR identity (SW-1521), TOOLBAR id=12/13,
  both receive TB_BUTTONSTRUCTSIZE(0x41E).
- The button-add flow: RegOpen('HKCU\\Software\\WinRAR\\General\\Toolbar\\Layout')
  miss(3), 'General\\Toolbar' miss(2), then a 160-iteration scan of
  'General\\Toolbar\\Buttons' on the config thread; the MAIN thread never sends
  TB_ADDBUTTONSW(0x444)/TB_ADDSTRING(0x41C/0x44D) — the toolbar renders empty.
- The menu band, listview (4 columns captured via LVM_INSERTCOLUMN), statusbar
  and combo render for real (SW-1520 content plane).

IMPLEMENTED this round (context): TB_ADDSTRING A/W pool, TB_ADDBUTTONS A/W +
INSERTBUTTON capture, TB_GETBUTTON readback, TB_BUTTONCOUNT,
TB_SETBUTTONSIZE/GETBUTTONSIZE (wine commctrl.h:1200-1247 constants),
TB_GETITEMRECT cumulative layout, TB_SETBITMAPSIZE/ADDBITMAP.

PENDING decode: why the guest's toolbar-init aborts after the registry misses
(heaps freed right after the Layout/Toolbar misses). Instrument: MW32_STEP_TRACE
around the toolbar-init call region + the SW-1503 guest backtrace tool.""",
  "labels": []})
print("new issue:", n.get("number"), n.get("html_url"))
