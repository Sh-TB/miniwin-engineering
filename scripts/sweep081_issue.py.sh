#!/bin/bash
# SWEEP-081 Issue #1 progress comment
cd "$(dirname "$0")/.." || exit 1
TOKEN=$(git remote get-url origin | sed -n 's|.*://\([^:]*\):\(.*\)@github.*|\2|p')
BODY=$(cat <<'EOF'
## SWEEP-081 — plugin architecture + the user-named trio re-verified + 4 new app-plugins

**Base:** origin/main 829d1560 (SWEEP-080) == local; byte-exact rebuild `123d7750` before any change; G1 pre-change PIN 12/12 (20261026_161027). Final engine `92e7df46` (4,465,312 B); G1 post-change PIN 12/12 (20261026_165111). Commit c850003.

### Plugin architecture (new)
- `plugins/<name>/plugin.tsv` — per-app manifest: official URL + SHA-256, artifact, unpack mode, exe, input probes, known root causes.
- `scripts/plugin_run.sh` — generic runner: hash verify → unpack → N cold runs → frames (initial + post-input diff) → verdict → evidence PNGs → `plugins/status.tsv` row.
- Standing rule: the user-named trio (winrar/idman/npp64) runs every train + one NEW unknown per train.

### Engine fixes
- **SW-1372** — 23-API shlwapi/user32/kernel32 batch (StrCpyW/StrCpyNW/StrCatW/StrNCatW/StrCatBuffW/A, PathIsPrefixW/PathCommonPrefixW/PathRenameExtensionW/PathQuoteSpacesW/PathUnquoteSpacesW/PathUnExpandEnvStringsW/PathRelativePathToW, PrivateProfile SectionW pair, IsChar A-variants, MessageBoxExW→real MessageBoxW, DrawAnimatedRects, SHCreateDirectoryExW recursive mkdir, SHGetDataFromIDListW honest E_FAIL) + x86 stdcall arity rows + SHELL32.180 (wine spec: SHGetDataFromIDListW). notepad2-mod unresolved imports **23 → 0**.
- **SW-1334 observability** — class-proc dispatch trace now prints the numeric message id.

### Trio status (no regression)
| Guest | Result | Frontier |
|---|---|---|
| winrar 7.12 x64 | 13/13 STARTDLG children parse with real texts; clean rc=0 | SW-1361 (SFX self-resolver; synthetic export dirs for virtual DLLs = standing proposal) |
| idman 6.42b2 x86 | full unpacker + registry seeding + honest dialog rendered (evidence PNG) rc=0 | SW-1370 (CreateProcessW child chain) |
| notepad++ x64 | 2 real MessageBoxW frames; SW-1298-x64 **decoded to instruction level**: `mov [rcx],rbx` @RVA 0x1099b3 after `lea rcx,[rbx+0x20]` with rbx=0 (NULL C++ object), via the SW-1334 re-entry | SW-1298-x64 (next: walk back to the NULL producer) |

### New app-plugins (first results)
- **winnc 11.6** (Inno Setup): SW-1374 — `GetProcAddress(ntdll,"wine_get_version")` probe handled honestly, then host SIGSEGV in the cpu32 interpreter at guest 0x40722A (Inno runtime dispatch), step 551.
- **everything 1.4.1 x64**: SW-1375 — guest C++ throw then `mov rcx,[rsi+0x98]` rsi=0 (MFT/DeviceIoControl class).
- **renamer 7.5 x86**: SW-1376 — Delphi TlsGetValue probe loop → wild jump into the guest stack (SW-1358b class).
- **notepad2-mod x64** (new unknown): advanced — 0 unresolved imports; message pump runs (RegisterClassW + 44 class-proc SendMessage cycles); dies at COM vtable call `[rax+0x208]` on NULL this (SW-1373).
- **HxD**: BLOCKED_DOWNLOAD — mh-nexus.de TCP-443 refused from the sandbox (6 attempts across 2 sweeps, incl. http/UA/IPv4); same-category substitutes probed (Frhed/XVI32/Okteta dead, ImHex 66 MB not tiny). No unofficial mirrors. Plugin stays in the ledger and retries.

### Gates
- Corpus ×3 EXACT parity, zero crashes/fatals: winmine 82, sol 69, freecell 10, calc 1, breakout 10, snake 13 (spider 627–633 wall-clock band).
- Puzzle spot-check 4/4 (blackbox/bridges/cube/dominosa): rc=0, frames, zero crashes.
- Evidence: `evidence/sweep081/` (summary.md, dl_sha256.tsv, corpus_parity.tsv, per-plugin logs + crash contexts + frames), `plugins/status.tsv`.

### Next highest-priority step
SW-1298-x64 producer walk (identify the NULL-returning API feeding rbx at npp64 0x1099b3), then SW-1374 (Inno runtime dispatch decode) — both via the plugin runner; HxD download retry next train.
EOF
)
python3 - "$TOKEN" "$BODY" <<'PYEOF'
import json, sys, urllib.request
token, body = sys.argv[1], sys.argv[2]
data = json.dumps({"body": body}).encode()
req = urllib.request.Request(
    "https://api.github.com/repos/Sh-TB/miniwin-engineering/issues/1/comments",
    data=data, method="POST",
    headers={"Authorization": f"token {token}", "Content-Type": "application/json"})
r = urllib.request.urlopen(req)
print("comment id:", json.load(r)["id"])
PYEOF
