#!/usr/bin/env python3
"""SWEEP-112: post the SW-1563 round evidence on the acceptance issues
(#25 IDM / #22 headless GUI frontier). Token comes from the git remote
URL, never printed (same pattern as sweep108_issues.py)."""
import json, subprocess, urllib.request

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

IDM_BODY = """**SW-1563 CLOSED — the extracted GUI payload frontier (SWEEP-112, commit 44d312f)**

`IDM5.tmp` (the 6,490,992-byte PE32 GUI payload extracted to
`Windows/Temp/IDM_Setup_Temp/`) went **rc=9 -> rc=0**.

Root cause (measured, engine-generic, 32-bit plane):
- 94 stdcall APIs resolved with `arity unknown -> under-pop 0`. For stdcall the
  CALLEE pops its arguments; under-pop 0 leaves them on the guest stack, so
  after three such calls the CRT epilogue popped a data pointer as a return
  address / misaligned the frame until the guest's own /GS cookie check raised
  0xC0000409 -> `SetUnhandledExceptionFilter(NULL)` -> UEF -> `TerminateProcess`
  (the SW-1561 trace-tail shape, same class as SW-1560).
- 28 A-variant imports (CopyFileA, CreateDirectoryA, MoveFileA, RemoveDirectoryA,
  GetVolumeInformationA, OpenMutexA, WriteConsoleA, EnumResourceLanguagesA,
  FindResourceExA, ConvertDefaultLocale, GlobalFindAtomA, GlobalGetAtomNameA,
  RegQueryValueA, CloseWindow, CopyAcceleratorTableA, CreateDialogIndirectParamA,
  DrawStateA, GetPropA, RemovePropA, SetPropA, SetClassLongA, GrayStringA,
  CopyMetaFileA, GetFileTitleA, PathFindExtensionA, PathIsUNCA,
  PathStripToRootA, GetClassInfoA) were stub thunks — only the W names were
  registered. Fixed per Wine's A = widen-then-W (dlls/kernel32/path.c);
  GetFileTitleA / PathFindExtensionA / PathIsUNCA / PathStripToRootA are real
  implementations. Engine atom table stores host 4-byte `wchar_t` (wcscmp), so
  GlobalFindAtomA widens byte-per-unit — documented in the code.

Fix set: 98 arities in `g_api32_arity` (SDK x86 stack bytes) + the 28 entries.

Measured:
- IDM5.tmp: `cpu32_run rc=0 steps=416607`, **0 unresolved / 0 unknown arities**
  (was 28 / 94). Trace: `evidence/sweep112/idm5_payload_trace.json`.
- Full chain: parent (289M interpreted steps incl. extraction + child spawn)
  -> IDM1.tmp second stage END-TO-END -> IDM5.tmp GUI payload clean exit.
  Battery rc=0 x3, crash=0 (`evidence/p13_idm/`).
- Corpus-wide cure (same drift class): putty 139->0, rammap64 139->0,
  procexp64 139->0, winmergeu 133->0, zoomit64 1->0 — every program now
  exit 0 x3 except pageant (ALIVE_PUMP by design); zero unresolved on all
  45 rows (`evidence/sweep112/corpus_sweep_p13.tsv`).
- G1 PIN 12/12 (evidence/master/g1/20261031_094535).

Remaining IDM rungs: none blocking the installer chain; the next named
frontier is the payload's first-run GUI dialogue (needs the modal pump
plane, tracked on #22)."""

GUI_BODY = """**Corpus-wide stdcall arity-drift cure (SWEEP-112, commit 44d312f)**

The SW-1563 fix (98 missing x86 stdcall arities + 28 A-variant imports in the
32-bit dispatch) turned out to be the root cause behind FIVE corpus GUI
frontiers, not just IDM's:

| program | before | after (x3 deterministic) |
|---|---|---|
| putty | SIGSEGV 139 | exit 0 |
| rammap64 | SIGSEGV 139 | exit 0 |
| procexp64 | SIGSEGV 139 | exit 0 |
| winmergeu | /GS frontier 133 | exit 0 |
| zoomit64 | exit 1 | exit 0 |

Zero unresolved imports on all 45 rows (`evidence/sweep112/corpus_sweep_p13.tsv`).
pageant remains 124 = ALIVE_PUMP by design (input battery pumping).

The remaining headless-GUI frontier list from this issue is now:
1. the modal message pump for dialog-driven flows (WinRAR nag dialog runs its
   whole life inside WM_INITDIALOG; guests that pump internally never return);
2. TB_ADDBUTTONSW chain-field decode (SW-1562, issue #26);
3. Notepad++ guest C++ EH resume CONTEXT (issue #24, SW-1201 class)."""

for num, body in ((25, IDM_BODY), (22, GUI_BODY)):
    gh("POST", f"issues/{num}/comments", {"body": body})
    print(f"posted comment on #{num}")
