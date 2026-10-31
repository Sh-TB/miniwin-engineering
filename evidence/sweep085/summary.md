# SWEEP-085 — the "error dialogs are not execution" train: generic file-plane + focus + child-paint fix set

User critique this train: the previous "PASS" frames for Notepad++/IDM were ERROR dialogs, not the
apps running. This train decodes and fixes the generic engine causes, honestly re-classifies what
still fails, and decodes the next frontiers (SW-1399/1400/1401).

Base: origin/main ac1a0e9 == local at session start. Engine CHANGED (4 fix classes below).
G1 pre-change 12/12 (20261026_201812, rebuild 80bca026 byte-exact); post-change G1 12/12
(20261026_205504, engine ad0e9a35). Corpus parity 21/21 rc=0 crash=0 fatal=0 x3
(evidence/sweep085/corpus_parity.tsv) — ZERO regressions across all four fixes.

## Engine fixes (all generic; zero app special-casing)

- **SW-1395 — shlwapi/CopyFile path mapping**: the SW-1101e tails block (PathIsDirectoryW,
  PathFileExistsW, CopyFileW, GetCompressedFileSizeW) passed RAW "C:\..." strings to
  access()/stat()/fopen() — always false on the host, while CreateFileW's plane normalizes via
  w3_normalize_path (SW-1234). Result: files that EXISTED looked missing (Notepad++ first-run
  probe: PathFileExistsW(langs.model.xml) = FALSE -> "Load langs.xml failed!" without ever
  copying). All four now normalize; buffers bumped to 1024 (w3_normalize_path writes up to 1024).
  MEASURED: langs.xml + stylers.xml now SELF-CREATE at first run exactly like real Windows;
  both first-run error dialogs GONE.
- **SW-1396 — real focus tracking**: SetFocus was a NULL-returning stub and ShowWindow never
  activated — GetFocus() was always NULL. Notepad++'s main wndproc thunk (0x140109870) passes
  GetFocus() as the hwnd to its inner dispatcher (IAT call 0x140452098 -> mov rax,rcx): with NULL
  it early-returned and the main window never painted. SetFocus now tracks + returns previous;
  ShowWindow sets focus+active for show-state cmds (1/3/5/9), not for NOACTIVATE variants.
  MEASURED: GetFocus -> 0x30001 during the paint dispatch.
- **SW-1397 — ReadFile observability**: large reads (>=1024) now trace fd, result, bytes got and
  the first 8 payload bytes (wrong-content/zero-byte read class made visible; used to pin the
  langs.xml byte story: 544768+1804 = 546572 delivered, first '<?xml ve').
- **SW-1398 — real per-window paint delivery**: (a) mw_gui_dispatch_msg routed ONLY the main
  hwnd; every child hwnd got "no registered wndproc" -> 0. Now routes child hwnds to the
  guest-owned proc (instance subclass wndlong(-4) first, else the SW-1334 captured class_proc) —
  real DispatchMessage semantics. (b) the pump delivered WM_PAINT only to the main frame once;
  now arms a child-paint round-robin after the main frame's paint (one child WM_PAINT per pump
  round, engine-owned defproc controls stay compositor-drawn). MEASURED: `#2 GetMessageW ->
  WM_PAINT child=0x30001 (SW-1398)` and the Scintilla proc EXECUTES BeginPaint.

## Results (3x cold runs each)

| Guest | Before train | After train |
|---|---|---|
| npp64 (user-named) | 2 error dialogs (langs/stylers) then abort | config files self-create; NO dialogs on warm runs; first run shows npp's own designed recovery dialog (see SW-1399); rc=0 x3, no crash |
| idman (user-named) | "Could not create/open error log file" | decoded to root cause SW-1401 (unchanged behavior) |
| winrar (user-named) | rc=0 frames=0 silent | decoded: SFX wizard template RESOLVED (890B, children minted, real dlg proc 0x14001f5b0); modal lifecycle = SW-1400 |
| fifteen (NEW game, SWEEP-072 A-group U05) | — | **PASS_INPUT_STATE_CHANGE FIRST CONTACT** (rc=0 x3, 30 frames / 15 unique states — tiles slide under the probes) |
| resourcesextract (NEW program, NirSoft B-group) | — | x86 guest: 402k interpreter steps, main() returns 0 pre-dialog (x86 dialog-path frontier, queued) |

## New frontiers decoded (queued, not fixed this train)

- **SW-1399** — npp64 remaining: (a) the shipped langs.model.xml contains 3 RAW unescaped '&'
  ("Baan version & TIV level") — strict tinyxml parse fails, the recover path copies the same
  bytes back and re-fails (content defect interacting with npp's recover loop); (b) npp's own
  layout computes 32x32 (MoveWindow(hwnd=NULL) x2) so the paint surface stays minimal.
- **SW-1400** — winrar SFX: DialogBoxParamW resolved the real wizard template and minted its
  children; the modal event lifecycle (3 dispatches then end) needs the dialog-modal pump.
- **SW-1401** — idman: `CreateProcessW() -> fail (no child process support)` — setup falls back,
  opens its silent switch "/s" as a log path, errors. Generic v1: re-exec the loader on the
  child exe (high yield for ALL installers: winnc Inno, winrar SFX payload, idman).

## Gates

- corpus parity 21/21 x3 (evidence/sweep085/corpus_parity.tsv); G1 12/12 pre + post (80bca026 / ad0e9a35).

## Next wagons

SW-1399 npp layout-size decode (where do the 32x32 MoveWindows come from) + model-content policy;
SW-1400 modal dialog pump; SW-1401 CreateProcessW v1 re-exec; more puzzles (netgame/lightup/pattern);
hxd retry; everything/tcpview64 poll loops; renamer rc=204 pre-render path.
