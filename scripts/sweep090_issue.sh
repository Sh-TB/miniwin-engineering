#!/bin/bash
# SWEEP-090 Issue #1 progress comment
cd "$(dirname "$0")/.." || exit 1
TOKEN=$(git remote get-url origin | sed -n 's|.*://\([^:]*\):\(.*\)@github.*|\2|p')
BODY=$(cat <<'EOF'
## SWEEP-090 — post-recovery reconciliation + SW-1305 FindFirstFileA struct contract; NEW game netgame first-contact PASS + NEW program shexview first-contact PASS

**Protocol:** +1 new game +1 new program, engine fix, gates. Commit 09d78b1. Engine **changed** — G1 **12/12** (`evidence/master/g1/20261027_081203`, binary 4,615,008 bytes); corpus parity **21/21 ×3** (rc=0, crash=0, fatal=0).

### Session recovery note (honest ledger)
The workspace had silently rolled back to a SWEEP-030-era snapshot while origin/main had advanced 86 commits to SWEEP-089. Local re-derivations made there (drive-tree sandbox, registry tree mirror, CloseHandle slot release, lstrcatW) were checked one-by-one against the mainline: every one already exists in a train-hardened form (SW-1234 cwd drive root, SW-1402 regtree, SW-1405 skeleton+WINTEMP, SW-1407/1407b module-path+staging, SW-1244 CloseHandle release). Dropped per the no-history-rewrite rule (preserved on branch `sweep031-local-divergent`); **one finding was genuinely still live and is fixed in this train.**

### SW-1305 — FindFirstFileA/FindNextFileA wrote the W struct into A buffers (memory corruption, now fixed)
- Callers of `FindFirstFileA`/`FindNextFileA` pass **WIN32_FIND_DATAA** (318 bytes; CHAR name @+44, cAlternate @+304). The shared `fill_find_data` always wrote the **592-byte W layout** (wide names @+44..564) → **274-byte stack overflow** into the guest frame → /GS cookie smash → `__report_gsfailure` → FASTFAIL(2).
- Latent since BUG-049: every prior A-variant call hit a *missing* directory so the fill never ran. Proven on the stale snapshot: putty's `FindFirstFileA("C:\Windows\*")` enumerated a seeded dir → "System32" entry → overflow → FASTFAIL; difflib trace alignment isolated the exact call; fixing the layout restored putty/puttygen to rc=0.
- Fix: `W3Find.wide` flag + `fill_find_data_a` (318B, CHAR names) + `find_first_impl(…, wide)`; `FindFirstFileW`/`ExW`/`NextW` paths unchanged.

### Results
| Wagon | Verdict | Notes |
|---|---|---|
| **netgame** (NEW game — Simon Tatham Net) | **PASS_INPUT_STATE_CHANGE first contact** | rc=0 ×3, 20 frames / 10 unique states. |
| **shexview** (NEW program — NirSoft ShellExView x64 2.01) | **PASS_LAUNCH_RENDER first contact** | rc=0 ×3, main window renders; GUI ListView walking HKCR CLSID — pairs with the installer-written shell-extension keys from the SWEEP-086 regtree. |
| corpus (winmine/sol/freecell/calc/breakout/snake/spider ×3) | 21/21 rc=0 crash=0 fatal=0 | Identical to SWEEP-089 except breakout/snake now render frames (10/13 vs 0) — improvement. |

### Queued
SW-1416 handle value-space collision (file handles 4..67 share the kobj small-int space — latent) · SW-1306 PE32/WoW64 (32-bit NSIS installer stubs) · SW-1307 real guest-DLL module loading (7z.exe codec path) · SW-1412 installed-WinRAR heap-corruption decode · +1 game +1 program per train.
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
