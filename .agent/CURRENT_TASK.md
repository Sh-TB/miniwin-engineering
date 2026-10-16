# CURRENT TASK

**Task ID:** TASK-OPENSRC-1 (canonical, P1 — see `.agent/NEXT_ACTION.md`)
**Title:** Unblock ffmpeg.exe and magick.exe (real famous open-source targets)
**Status:** READY — merged state (2026-10-16): parallel appcompat/ISS1/OPENSRC-1 line + TEST-004 slice 2 line integrated
**Priority:** P1

## Canonical frontier (from NEXT_ACTION.md @ f75aa97/00898fe, re-verified at merge)
- **ffmpeg.exe** (BtbN win64 GPL): ~425 unresolved imports; deep into CRT init (WSAStartup,
  vulkan probe, per-library constructors). BLOCKED root: heap metadata corruption
  ("malloc(): invalid size (unsorted)") during late CRT init, before main.
  Next command: `timeout 150 ./minwin_loader artifacts/opensource/ffmpeg/ffmpeg-master-latest-win64-gpl/bin/ffmpeg.exe -hide_banner -loglevel error -f lavfi -i "testsrc2=size=64x48:rate=1" -frames:v 1 -y /tmp/out.png`
  Tooling: MINWIN_HEAP_GUARD diagnostics already in the tree.
- **magick.exe** (ImageMagick 7.1.2-34 portable): BLOCKED root: UCRT locale ctype-init
  delivers a corrupted frame to GetStringTypeW (R9 = string data as out-ptr).
  Next: decode the caller at RVA 0x3d87b2 frame + the locale struct layout.
- Acceptance: ffmpeg reaches main() and produces a valid PNG; magick produces rose.png;
  regression 12/12 + sweep 11/12 stay green.

## Also completed in this merge (TEST-004 slice 2, commit 824230f)
- Builder B-1/B-2/B-3 closed: all 4 synthetic dispatch tests raise through the real IAT
  call, reach RtlDispatchException, get a handler invoked, resume cleanly (4/4, 3x
  deterministic; byte-proof `09 05 02 00 | 05 52 | 01 30`); verifier non-vacuous.
- Queued micro-tasks: TASK-TEST-004-C1 (real ContinueSearch two-handler walk) and
  TASK-TEST-004-D1 (test_d is a byte-identical duplicate of test_a — differentiate).
- TASK-BUG-038 design (target-frame delivery) remains in backlog/history — P2,
  llvm-mingw/MS-ER family only.

## Ground rules
- Loader changes must keep: regression 12/12, UPX G1 (exit 0 + banner), sweep 11/12,
  dispatch suite 4/4. Every fix = separate report + evidence dir + 3x runs.
- Credentials: ~/.git-credentials LOST between sessions (OWNER-004) — push blocked until
  the user re-supplies the token; never store it in the repo.
