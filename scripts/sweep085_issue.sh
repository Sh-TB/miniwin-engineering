#!/bin/bash
# SWEEP-085 Issue #1 progress comment
cd "$(dirname "$0")/.." || exit 1
TOKEN=$(git remote get-url origin | sed -n 's|.*://\([^:]*\):\(.*\)@github.*|\2|p')
BODY=$(cat <<'EOF'
## SWEEP-085 — "error dialogs are not execution": generic file-plane + focus + per-window-paint fix set; honest reclassification

**User critique adopted:** the previous Notepad++/IDM "PASS" frames were ERROR dialogs, not the apps running. This train decodes and fixes the generic engine causes, re-classifies what still fails honestly, and decodes the next frontiers. Commit c8cd9c8. Engine **changed** — G1 12/12 pre (80bca026 byte-exact) + post (ad0e9a35); corpus parity **21/21 ×3, zero regressions** across all four fixes.

### Engine fixes (all generic; zero app special-casing)
- **SW-1395 — shlwapi/CopyFile path mapping**: the late-added tails block (`PathIsDirectoryW`, `PathFileExistsW`, `CopyFileW`, `GetCompressedFileSizeW`) passed **raw `C:\...` strings** to `access()/stat()/fopen()` — always-false on the host — while `CreateFileW`'s plane normalizes (SW-1234). Files that EXISTED looked missing: Notepad++'s first-run probe `PathFileExistsW(langs.model.xml)` returned FALSE, so the model→langs.xml copy never fired and both first-run dialogs appeared. All four now normalize. **Measured: `langs.xml` + `stylers.xml` self-create at first run like real Windows; both first-run error dialogs GONE.**
- **SW-1396 — real focus tracking**: `SetFocus` was a NULL stub and `ShowWindow` never activated → `GetFocus()` was always NULL. Notepad++'s main wndproc thunk (0x140109870, disassembled) passes `GetFocus()` **as the hwnd** to its inner dispatcher (`call [GetFocus]; mov rax→rcx; call dispatcher`) — with NULL it early-returned and the main window never painted. SetFocus now tracks + returns previous; ShowWindow sets focus+activation for show-state cmds (1/3/5/9).
- **SW-1397 — ReadFile observability**: large reads now trace fd/result/bytes-got/first-8-payload — pinned the langs.xml byte story (546,572 bytes delivered, valid XML head `<?xml ve`).
- **SW-1398 — real per-window paint delivery**: dispatch routed ONLY the main hwnd (children got "no registered wndproc" → 0); now routes child hwnds to the **guest-owned proc** (subclass wndlong(-4) first, else the SW-1334 captured class proc) — real DispatchMessage semantics. The pump delivered one paint to the main frame only; now arms a **child-paint round-robin** after it. **Measured: `WM_PAINT child=0x30001` delivered; the Scintilla proc EXECUTES BeginPaint.**

### Honest reclassification + new-program wagons
| Guest | Status |
|---|---|
| **npp64** | config self-provisioning works; warm runs clean rc=0 ×3 no dialogs; first run shows npp's **own designed recovery dialog** — SW-1399 queued: the shipped `langs.model.xml` contains **3 raw unescaped `&`** ("Baan version & TIV level") which strict tinyxml rejects, and the recover path re-copies the same bytes; plus the guest computes a **32×32 layout** (`MoveWindow(hwnd=NULL)` ×2) so the paint surface stays minimal |
| **winrar** | decoded: the SFX wizard template **resolved** (890 bytes, children minted, real dlg proc 0x14001f5b0) — the modal-dialog event lifecycle is the frontier (**SW-1400**) |
| **idman** | decoded to root cause: `CreateProcessW() -> fail (no child process support)` → setup falls back, opens its silent switch `/s` as a log path → error dialog (**SW-1401**; generic fix = loader re-exec on child exe — high yield for ALL installers) |
| **fifteen (NEW game, A-group U05)** | **PASS_INPUT_STATE_CHANGE FIRST CONTACT** (rc=0 ×3, 30 frames / 15 unique states — tiles slide under the probes; third consecutive Simon Tatham first-run PASS) |
| **resourcesextract (NEW program, NirSoft)** | official re-download verified byte-exact (`cdccfe00…`); x86 guest: 402k interpreter steps, main() returns 0 pre-dialog — x86 dialog-path frontier queued |

### Gates
corpus 21/21 ×3 (evidence/sweep085/corpus_parity.tsv); G1 12/12 pre+post. GitHub: origin/main == local verified at session start and after push (fast-forward ac1a0e9..c8cd9c8, no force).

### Next wagons
SW-1399 npp layout-size decode; SW-1400 modal dialog pump; SW-1401 CreateProcessW v1 re-exec; more puzzles (netgame/lightup/pattern); hxd retry; everything/tcpview64 poll loops.
EOF
)
curl -s -X POST -H "Authorization: token $TOKEN" \
  -H "Content-Type: application/json" \
  -d "$(python3 -c 'import json,sys; print(json.dumps({"body": sys.stdin.read()}))' <<< "$BODY")" \
  "https://api.github.com/repos/Sh-TB/miniwin-engineering/issues/1/comments" | python3 -c "import json,sys; d=json.load(sys.stdin); print('comment id:', d.get('id'), '| url:', d.get('html_url', d.get('message','ERR')))"
