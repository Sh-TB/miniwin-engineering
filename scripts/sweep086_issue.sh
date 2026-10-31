#!/bin/bash
# SWEEP-086 Issue #1 progress comment
cd "$(dirname "$0")/.." || exit 1
TOKEN=$(git remote get-url origin | sed -n 's|.*://\([^:]*\):\(.*\)@github.*|\2|p')
BODY=$(cat <<'EOF'
## SWEEP-086 — installer ground-truth train: registry tree + install log + sandbox; **the official WinRAR installer installed 28 files / 9.56 MB**

**User mandate:** (installers — registry files and folders must be created correctly in their own paths) (for installers, registry files and folders must be created correctly in their own paths) + one new game + one new program. Commit b4223ab. Engine **changed** — G1 **12/12** (`evidence/master/g1/20261027_053105`, binary `b24475ff`); corpus parity **21/21 ×3** (7 guests × 3, rc=0 crash=0 fatal=0).

### Engine subsystems (all generic; zero app special-casing)
- **SW-1402 — on-disk registry tree mirror** (`MINWIN_REG_TREE=<dir>`): at exit the live hive is exported as a **real filesystem tree** — key paths become directories (`HKLM\SOFTWARE\X` → `<tree>/HKLM/SOFTWARE/X`), values become `@<name>` files rendered by type (REG_SZ text / `dword:0x…` / hex), plus a `_REG_MANIFEST.tsv` ledger (regpath, value, type, size, hostfile). Registry files are now literally created in their own paths. **SW-1402b**: installer-grade hive caps (256 keys / 24 values / 256-char paths / 1 KiB data; snapshot format load-compatible).
- **SW-1403 — guest install-log** (`MINWIN_INSTALL_LOG=<file>`): every guest FS **mutation** (CreateDirectory, create-mode CreateFile, CopyFileW/2, MoveFileEx, DeleteFile, RemoveDirectory) appends `op<TAB>guest-path<TAB>host-path<TAB>result`. Read-only opens never log. `mw_ExitProcess` flushes snapshot+regtree explicitly (`_exit` never ran loader atexit handlers).
- **SW-1404 — runner sandbox mode** (manifest `sandbox=yes`): guest CWD = `evidence/<sweep>/<name>/sandbox`; the SW-1234 drive-root mapping lands every `C:\...` path **inside** the sandbox. Non-sandbox plugins byte-identical.
- **SW-1405 — booted-system truth**: the Windows dir skeleton (SWEEP-058) is now seeded by `CreateDirectoryW` too; `GetTempPathW` gains the `MINWIN_WINTEMP=1` world mode (`C:\Windows\Temp\`); default stays POSIX TMPDIR (corpus untouched).
- **SW-1407 — module self-path round-trip**: `win32_module_path` is now drive-root-relative when the exe lives under it; NSIS/Inno **re-open their own image** (`CreateFile(GetModuleFileName())`) to read the embedded payload — the old `C:home\z\…` form could never re-normalize. Runner copies sandboxed installers into `<sandbox>/Installers/`.
- **SW-1408 — PE32 arity `MessageBoxIndirectW/A = 4`**: the arity-0 under-pop left 16 bytes on the guest stack; NSIS's first message box returned into the thunk region on a drifted RSP (SIGSEGV).

### Results
| Guest | Verdict | Ground truth |
|---|---|---|
| **winrar** (official 7.12 SFX installer) | **INSTALL_OK** rc=0 ×3 | **28 files / 9,564,664 B installed into `sandbox/WinRAR/`**: WinRAR.exe, Rar.exe, UnRAR.exe, Uninstall.exe, RarExt.dll, 7zxa.dll, 6 SFX modules, docs — 93 install-log ops, **every write result=1**. The *installed* WinRAR.exe now exists on the MiniWin C: drive (next train target). |
| **idman** (official 6.42 b2 installer) | EXIT rc=0 ×3 | `C:\Windows\Temp\IDM_Setup_Temp` **created** (the historical "Could not create temp dir" blocker is gone); child-exec frontier SW-1401 unchanged. |
| **nppsetup** (NEW — official 8.9.8.1 NSIS installer, sha256 26f3bcce…) | FRONTIER_SW-1409 | Deep run: **69.7M steps, 1093 API calls** — self-reopen OK, temp stage `ns*.tmp` + `System.dll` staged, payload `ReadFile` loop (16 KiB chunks, 8 MiB LZMA window) → SIGSEGV inside LZMA output write (si_addr 0x73df300f). Queued SW-1409. |
| **lightup** (NEW game — Simon Tatham Light Up) | **PASS_INPUT_STATE_CHANGE first contact** | rc=0 ×3, 6 frames / 3 unique states; official zip sha a5b00f63 re-verified. |
| npp64 / mines (re-verify) | PASS_LAUNCH_RENDER / PASS_INPUT_STATE_CHANGE | zero regressions. |

### Honest-classification note
`EXIT rc=0 frames=0` rows are **not** render claims: winrar's row is INSTALL_OK because the install-log + filesystem tree **prove** the files; idman/nppsetup rows are pre-wizard frontiers with the stage of arrival named.

### Next wagons
SW-1409 NSIS LZMA extraction desync (SetFilePointer/ReadFile position or chunk-size decode at dist=190492) · SW-1401 idman CreateProcessW child exec · SW-1400 winrar SFX interactive modal · **run the INSTALLED `sandbox/WinRAR/WinRAR.exe`** · +1 game +1 program per train.
EOF
)
python3 - "$TOKEN" "$BODY" <<'PY'
import json, sys, urllib.request
token, body = sys.argv[1], sys.argv[2]
req = urllib.request.Request(
    "https://api.github.com/repos/Sh-TB/miniwin-engineering/issues/1/comments",
    data=json.dumps({"body": body}).encode(),
    headers={"Authorization": f"token {token}", "Accept": "application/vnd.github+json",
             "Content-Type": "application/json", "User-Agent": "miniwin-train"})
r = urllib.request.urlopen(req)
print("comment id:", json.load(r)["id"])
PY
