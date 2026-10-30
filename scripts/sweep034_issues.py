#!/usr/bin/env python3
"""SWEEP-034: register the class-model + WinSock error-vector issues.
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
    r = gh("POST", "contents/artifacts/sweep034/evidence/" + name, {
        "message": "SWEEP-034 evidence: " + name,
        "content": b64, "branch": "main"})
    return r["content"]["html_url"] if r and "content" in r else "(upload pending)"

body1 = """## SW-1511/1512/1513/1513b — class IDENTITY + per-window DISPATCH model (putty + RAMMap64 SIGSEGV frontier closed)

**The user's level-0 method (pass real programs through Wine `WINEDEBUG=+relay`, diff against the engine) found the deepest structural gap so far.**

### Root causes (each measured on the real binary)

1. **All classes shared one atom.** Every `RegisterClass*` returned `MW_GUI_ATOM` 0xC001 — Wine gives putty's three classes 0xc012/0xc013/0xc01b (relay, putty 0.85). Class identity was impossible.
2. **ANSI class names never matched.** `mw_class_proc_lookup` probed every cls param as a WIDE string: `\"PuTTYTimerWindow\"` probed as `0x7550,0x5454,...` — never matching its own registration. The main-window class-proc snapshot silently failed (`g_main_class_proc=NULL` for every A-path create).
3. **Dispatch used the LAST-registered channel.** With no snapshot, putty's timer window (`PuTTYTimerWindow`, created FIRST, 100x100 hidden) received engine-generated WM_SIZE through the global `g_wndproc_fwd` = putty's **main** WndProc (registered later as W-class `PuTTY`). That proc starts with `mov $0xffffffeb,%edx; call GetWindowLongPtrW(GWLP_USERDATA)` → 0 → r13=NULL → `mov rcx,[r13+0xbc8]` → **SIGSEGV at putty RVA 0x18aa** (SW-1325).
4. **RAMMap64 (SW-1326, RVA 0x4cce): dialog-only app.** It registers its frame class (proc 0x140004a30) but hadn't created the frame yet; the engine's PHANTOM main window (0x10001) received WM_SIZE through that unrelated class proc → its handler `GetPropW(0x10001,PROP)` → 0 → `mov rcx,[rax+8]` → **SIGSEGV**.

### Engine changes (src/loader/loader.c)

- **SW-1511** encoding-correct class lookup (`_a` variants for the A paths)
- **SW-1512** distinct atoms 0xC001+ stored per class row; `MAKEINTATOM` resolves by atom (`mw_class_proc_by_atom`)
- **SW-1513** per-window proc: minted top-levels bind their own class proc; `mw_window_proc()` = real DispatchMessage routing (subclass override → own class proc → legacy); pump/SendMessage/create-seq/destroy/size-sync all route through it
- **SW-1513b** the phantom main window has NO fallback proc — a window the guest never created receives no messages

### Measured (same binaries, before → after)

| guest | before | after |
|---|---|---|
| putty 0.85 | rc=139 SIGSEGV 0x18aa | **rc=0** — full init + terminal create + its own `Host does not exist` error box (see evidence image) |
| RAMMap64 /accepteula | rc=139 SIGSEGV 0x4cce | **rc=0** — frame created, WM_SIZE via its own class proc, 2 real GDI frames 744x574 |
| G1 | 12/12 | **12/12** (evidence/master/g1/20261030_082206) |
| corpus gate | 15 rows rc=0 in 9 crash 0 | identical (no regressions) |
| procexp64/procmon64/bginfo64/zoomit64/diskmon64/winmergeu/puttygen/notepadpp rc set | unchanged | unchanged |

Full analysis: `artifacts/sweep034/LOADER_STRUCTURE_DIFF.md`.
"""

body2 = """## SW-1514 — WinSock `getaddrinfo` error-vector contract (host EAI_* leaked onto the WinSock surface)

**Found via the level-0 differential on putty 0.85** (real binary, Wine relay as oracle).

### Contract

On Windows (`ws2tcpip.h`) the `EAI_*` codes ARE the WSA codes:
`EAI_NONAME==WSAHOST_NOT_FOUND(11001)`, `EAI_AGAIN==WSATRY_AGAIN(11002)`, `EAI_NODATA==WSANO_DATA(11004)`, `EAI_FAIL==WSANO_RECOVERY(11003)`, `EAI_FAMILY==WSAEAFNOSUPPORT(10047)`, `EAI_BADFLAGS==WSAEINVAL(10022)`, `EAI_SERVICE==WSATYPE_NOT_FOUND(10109)`, `EAI_SOCKTYPE==WSAESOCKTNOSUPPORT(10044)`.
Wine converts identically (`dlls/ws2_32/unixlib.c addrinfo_err_from_unix`).

### Measured engine defect

The engine forwarded the HOST libc's getaddrinfo return verbatim. putty's
`sk_namelookup("")` (config dialog auto-confirmed headless) → host getaddrinfo("")
→ Linux `EAI_NONAME (-2)` → putty `win_strerror(-2)` → `FormatMessageA(-2)`
→ engine cannot format, honest 317 → putty MessageBox
`Error -2: (unable to format: FormatMessage returned 317)` → `exit(0)`.
Wine relay on the same binary: no such path (WSAStartup 0, no FormatMessage(-2)).

### Fix (SW-1514)

`mw_getaddrinfo_` maps EAI_* → WSA_* (incl. EAI_SYSTEM via errno), sets `g_wsa_err`,
returns the WSA code. putty now prints **its own** message `Host does not exist`
(`namelockup_strerror`, windows/network.c:466-472) — the exact string real putty
prints for WSAHOST_NOT_FOUND — and exits cleanly rc=0.

Evidence: artifacts/sweep034/evidence/putty_after_messagebox.bmp (after).
"""

body3 = """## NEXT — headless GUI frontier after SWEEP-034 (registered from measured runs)

1. **Modal-dialog semantics headless.** Wine rc=TO on GUI-INIT apps because the
   modal loop waits forever. Our `DialogBox*` returns immediately (auto-IDOK),
   which is why putty proceeded to its connection path without input. Honest
   headless = block in the modal loop like the blocking `GetMessageW` (SW-sweep031).
   Visible impact: putty's config dialog (and every DialogBox guest) — trace
   shows `CreateDialogParamW -> 0x10003 (modeless)` then immediate fall-through.
2. **RamMap child rendering.** Frame + class-brush paint works (744x574);
   SysListView32 children carry their class props (SW-1334 model) but rows
   aren't painted yet — next step is the child-model painter for the
   RamMap main list (`GetPropW(hwnd=0x30004, name=0x140044798)` flow already runs).
3. **notepad++ rc=9** — unchanged (SW-1201 MFC /GS FASTFAIL family; needs the
   C++ EH chain decode already started in SW-1499/SW-1503 backtrace work).
4. **pageant pump-hang** — unchanged; needs named-pipe/security semantics.

Files: artifacts/sweep034/LOADER_STRUCTURE_DIFF.md (Wine-vs-MiniWin load structure,
10-phase table, all measured).
"""

i1 = gh("POST", "issues", {"title": "[SW-1511/1512/1513/1513b] class identity + per-window dispatch — putty & RAMMap64 SIGSEGV frontier closed (Wine level-0 diff)", "body": body1, "labels": []})
print("issue1:", i1.get("number"), i1.get("html_url"))
i2 = gh("POST", "issues", {"title": "[SW-1514] WinSock getaddrinfo error-vector contract (EAI_* -> WSA_*) — putty 'Error -2' root cause", "body": body2, "labels": []})
print("issue2:", i2.get("number"), i2.get("html_url"))
i3 = gh("POST", "issues", {"title": "[NEXT] headless GUI frontier: modal-dialog blocking, RamMap child rows, npp rc=9, pageant hang", "body": body3, "labels": []})
print("issue3:", i3.get("number"), i3.get("html_url"))

# evidence uploads
u1 = upload("putty_after_messagebox.bmp", "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep034/evidence/putty_after_messagebox.bmp")
print("up1:", u1)
u2 = upload("rammap_after_frame_002.bmp", "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep034/evidence/rammap_after_frame_002.bmp")
print("up2:", u2)
u3 = upload("LOADER_STRUCTURE_DIFF.md", "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep034/LOADER_STRUCTURE_DIFF.md")
print("up3:", u3)

# comment on issue #19-ish flow: post session summary to the epic thread #19? Post to the newest issue instead.
c = gh("POST", f"issues/{i1['number']}/comments", {"body": "Evidence uploaded:\n- putty after: putty_after_messagebox.bmp ('Host does not exist' — putty's own WSAHOST_NOT_FOUND message)\n- RAMMap after: rammap_after_frame_002.bmp (744x574 real frame, class-brush fill)\n- LOADER_STRUCTURE_DIFF.md (10-phase Wine-vs-MiniWin load structure with measured verdicts)\n\nG1 12/12 (evidence/master/g1/20261030_082206), corpus gate 15 rows rc=0 in 9 crash 0 — zero regressions."})
print("comment:", c.get("html_url"))
