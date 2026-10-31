#!/bin/bash
# SWEEP-087 Issue #1 progress comment
cd "$(dirname "$0")/.." || exit 1
TOKEN=$(git remote get-url origin | sed -n 's|.*://\([^:]*\):\(.*\)@github.*|\2|p')
BODY=$(cat <<'EOF'
## SWEEP-087 — installed-program train: fake-COM ABI fix + CharLower low-word contract; the INSTALLED WinRAR.exe boots, inits COM, builds its window

**Protocol:** run the *installed* programs (not just portables) + one new game + one new program. Commit 4955f54. Engine **changed** — G1 **12/12** (`evidence/master/g1/20261027_055118`, binary `62dea8e0`); corpus parity **21/21 ×3**.

### Engine fixes (all generic; zero app special-casing)
- **SW-1410 — Microsoft x64 ABI on every fake-COM vtable member (16 fns)**: `SHGetMalloc` IMalloc (9), `SHGetDesktopFolder` IShellFolder (7), `CoGetMalloc` OLE allocator (8) were **plain SysV functions** exposed to the guest. MS x64 passes args in RCX/RDX/R8/R9; SysV bodies read RDI/RSI — `IMalloc::Free(p)` executed `free(garbage)` and crashed inside libc reading the chunk header. Installed WinRAR 7.12's first COM Release/Free round-trip unblocked.
- **SW-1411 — `CharLowerW`/`CharLowerA`/`CharUpperA` low-word contract**: real WinAPI treats HIWORD==0 as a single CHARACTER, not a pointer. Installed WinRAR passes `CharLowerW(1)` during init — dereferencing it was SIGSEGV @0x1. (CharUpperW already honored the contract.)
- **SW-1407b — runner `sandbox_tree=<dir>`**: installed-program plugins stage their whole install tree into the sandbox and run the exe **from its own install dir** with every sibling present (a real C: layout).

### Results
| Guest | Verdict | Notes |
|---|---|---|
| **winrarinstd** (NEW program — WinRAR 7.12 **installed by the official installer** in SWEEP-086; 28-file tree) | FRONTIER_SW-1412 crash=1 ×3 | Progress ladder: (1) IMalloc ABI crash → fixed; (2) `CharLowerW(1)` crash → fixed; (3) now: **SHGetMalloc OK + IMalloc round-trips OK + main window + child `SendMessageW` loop + `SHGetFileInfoW` probes** → heap-corruption crash inside libc (malloc metadata poisoned). Queued SW-1412 (heap-guard decode). |
| **pattern** (NEW game — Simon Tatham Pattern/Nonogram) | **PASS_INPUT_STATE_CHANGE first contact** | rc=0 ×3, 6 frames / 3 unique states. |

### Next wagons
SW-1412 installed-WinRAR heap-corruption decode · SW-1409 NSIS LZMA extraction desync · SW-1401 idman CreateProcessW child exec · SW-1400 winrar SFX interactive modal · +1 game +1 program per train.
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
