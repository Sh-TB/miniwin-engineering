#!/bin/bash
# SWEEP-082 Issue #1 progress comment
cd "$(dirname "$0")/.." || exit 1
TOKEN=$(git remote get-url origin | sed -n 's|.*://\([^:]*\):\(.*\)@github.*|\2|p')
BODY=$(cat <<'EOF'
## SWEEP-082 — the user-named trio driven to full graphical init; the ms_abi callback-class fix; WinNc/renamer/notepad2-mod un-crashed

**Base:** origin/main 12d7c8b (SWEEP-081 addendum) == local after sandbox reset; byte-exact rebuild `92e7df46` before any change; G1 pre-change PIN 12/12 (20261026_171541). Final engine `0eaff275` (evidence/sweep082/engine_sha256.txt); G1 post-change PIN 12/12 (20261026_181029). Commit 4bd053e.

### Engine fixes (all generic; no app special-casing)
- **SW-1380 — top-level WM_NCCREATE delivery**: real CreateWindowEx sends WM_NCCREATE(0x81, &CREATESTRUCT) → WM_NCCALCSIZE(0x83) → WM_CREATE(0x01). Child paths had it since SW-1335; top-level paths never did — guest frameworks of the "staticProc: WM_NCCREATE → SetWindowLongPtr(GWLP_USERDATA, CS->lpCreateParams)" family never registered their C++ window objects (npp64: zero SetWindowLongPtrW in every trace). FALSE return cancels creation (real contract). Both x64 and i386-staged paths.
- **SW-1381 — COMCTL32.411 GetWindowSubclass** (real search of the engine subclass chain, honest TRUE/FALSE + ref out) and **SHELL32.165 SHCreateDirectory** wiring. **Correction**: live wine shell32.spec says **SHELL32.155 = ILFree** — the SWEEP-029 map had it as SHCreateDirectory (wrong function class for a deallocator); ILFree implemented, 155 re-pointed, 165 kept.
- **SW-1382 — top-level identity minting for window #2+**: SW-1256 minting extended beyond WM_CREATE nesting. Window #1 keeps the legacy identity; later top-levels mint distinct handles with the full SW-1380 sequence. Surface store / class-proc snapshot gated to window #1 (a 0×0 helper window used to shrink the main surface to 32×32).
- **SW-1383 — GetWindowLongPtrW caller-RVA observability** (Phase-5 class): pinned the SW-1384 producer (caller=0x140235125 inside Scintilla's SWndProc) in ONE rerun.
- **SW-1384 — the ms_abi guest-callback class fix**: `mw_call_cb2/cb4` (SW-1354e) typed guest function pointers without `ms_abi` — the host emitted SysV args (rdi,rsi,rdx,rcx) while MSVC guest procs read rcx,rdx,r8,r9: **guest arg0 was our arg3** across ALL 2/4-arg guest callbacks (wndproc dispatch, dialog procs, enum procs, subclass chains). **SW-1384b**: 64-bit LRESULT returns (an int-returning cb_t truncated Scintilla's real DirectFunction pointer 0x1402350b0 → 0x402350b0). Pointer args widened to uintptr_t — EnumFontFamilies* truncated HOST-STACK LOGFONT/TEXTMETRIC pointers to 32 bits (guest FONTENUMPROC scanned lfFaceName(+0x1c) of a truncated stack address; crash RVA 0x152f13, si_addr=0x68a9e20c = 0x7ffc68a9e1f0+0x1c).
- **SW-1385 — cpu32 o32 immediate family**: 66-prefixed `mov r16,imm16` (B8-BF), `mov r/m16,imm16` (C7) and accumulator ALU ops (05/0D/15/1D/25/2D/35/3D) read imm16 with 16-bit semantics (the unconditional imm32 reads desynced eip mid-block → garbage decode → NULL-ea store; WinNc's Delphi CRT `66 b9/ba/b8` prologue crash at 551 steps).

### Trio results (3× cold runs each)
| Guest | Before | After |
|---|---|---|
| **notepad++ 8.9.8.1 x64** | FRONTIER_SW-1298-x64 crash @RVA 0x1099b3 (×3) | **RESOLVED — PASS_INPUT_STATE_CHANGE rc=0 ×3** — full init: window object registered via WM_NCCREATE, Scintilla children 0x30001/0x30003 with a real SCI_GETDIRECTFUNCTION/POINTER handshake, font/style init completes, ExitProcess(0) after 13,876 API calls |
| winrar 7.12 x64 | PARITY_FRONTIER_SW-1361 | unchanged (no regression) |
| idman 6.42b2 x86 | PASS_LAUNCH_RENDER | unchanged (honest dialog frame) |

### Other plugins (3× each, final engine)
- **winnc 11.6**: SW-1374 crash → **EXIT rc=86, crash=0** (CRT now runs; installer-stub exit — payload route next).
- **renamer 7.5**: SW-1376 crash → **EXIT rc=86, crash=0**.
- **notepad2-mod**: SW-1373 crash → **EXIT rc=0, crash=0**.
- **everything**: TIMEOUT unchanged (pre-existing poll loop).
- **NEW tcpview64** (SWEEP-072 B-group U23; official Sysinternals zip, sha256 4fc5ceba…): reached the real EULA dialog (STATIC + id=1/2 Agree/Disagree buttons + RICHEDIT + TOOLBAR), then a TOOLBAR poll-loop frontier → TIMEOUT (honest; next-train decode).
- **HxD**: BLOCKED_DOWNLOAD stands — mh-nexus.de TCP refused again (8th attempt, IPv4 variant included). No unofficial mirrors per protocol; retries next train.

### Gates
- G1 PIN 12/12 twice (pre 92e7df46 / post 0eaff275).
- Corpus 3× after ALL fixes: winmine/sol/freecell/calc/breakout/snake/spider rc=0, ZERO crashes, ZERO regressions.
- Ledger: `plugins/status.tsv` sweep082 rows; evidence: `evidence/sweep082/`.

### Next wagons
npp64 interactive probes (menus/tabs/editing through the now-registered window objects) → tcpview64 toolbar-loop decode → winnc unpacked-payload route → everything poll-loop decode.
EOF
)
URL=$(git remote get-url origin | sed -n 's|.*://\([^@]*@\)github|https://\1github|p' | sed 's|https://|https://|')
API="https://api.github.com/repos/Sh-TB/miniwin-engineering/issues/1/comments"
curl -s -X POST -H "Authorization: token $TOKEN" -H "Content-Type: application/json" \
  -d "$(python3 -c 'import json,sys; print(json.dumps({"body": sys.stdin.read()}))' <<< "$BODY")" \
  "$API" | python3 -c "import json,sys; d=json.load(sys.stdin); print('comment id:', d.get('id', d))"
EOF
bash scripts/sweep082_issue.sh 2>&1 | tail -1