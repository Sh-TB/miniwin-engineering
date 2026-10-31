#!/bin/bash
# SWEEP-088 Issue #1 progress comment
cd "$(dirname "$0")/.." || exit 1
TOKEN=$(git remote get-url origin | sed -n 's|.*://\([^:]*\):\(.*\)@github.*|\2|p')
BODY=$(cat <<'EOF'
## SWEEP-088 — third installer technology + LZMA decode refinement

**Protocol:** +1 game +1 program. Commit f9312b8. Engine analysis-only this train (no behavior change) — G1 **12/12** (`evidence/master/g1/20261027_055732`); corpus parity **21/21 ×3**.

### SW-1409 decode refinement (NSIS installer, nppsetup)
The NSIS 8.9.8.1 extraction path is now verified **byte-exact up to the LZMA decoder input**:
- self-reopen + trailer probe (`SetFilePointer(6951149)` + 4-byte read) correct;
- temp stage `ns*.tmp` + `System.dll` staging correct;
- `ReadFile(fd=5, 16384) @190492` delivered `5d 00 00 80 00 00 35 00` — exactly the host bytes (LZMA props 0x5D + 8 MiB dictionary, matching the guest's `GlobalAlloc(0x800000)` window).
**Conclusion: the file plane is innocent; the divergence is INSIDE the guest LZMA range-coder execution** (PE32 CPU emulation plane, 69.7M steps deep). Queued as the CPU-frontier decode.

### Results
| Guest | Verdict | Notes |
|---|---|---|
| **7zsetup** (NEW program — official 7-Zip 25.01 x64 installer, sha 78afa2a1, 7-Zip's own installer tech) | FRONTIER_SW-1413 rc=86 ×3 | PE32-FATAL during CRT init (`__set_app_type` / `__p__fmode` stage) — third installer technology characterized honestly. |
| **towers** (NEW game — Simon Tatham Towers) | **PASS_INPUT_STATE_CHANGE first contact** | rc=0 ×3, 6 frames / 3 unique states. |

### Installer scorecard (3/3 technologies)
| Tech | Guest | Stage reached |
|---|---|---|
| WinRAR SFX | winrar712.exe | **INSTALL_OK — 28 files / 9.56 MB installed** (SWEEP-086) |
| NSIS solid-LZMA | npp.8.9.8.1.Installer.x64.exe | deep extraction (LZMA CPU frontier SW-1409) |
| 7-Zip own | 7z2501-x64.exe | CRT-init frontier (SW-1413) |

### Next wagons
SW-1412 installed-WinRAR heap-corruption decode · SW-1413 7zsetup CRT-init · SW-1401 idman CreateProcessW child exec · SW-1400 winrar SFX interactive modal · +1 game +1 program per train.
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
