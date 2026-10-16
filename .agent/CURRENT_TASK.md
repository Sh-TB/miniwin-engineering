# CURRENT TASK

**Task ID:** TASK-OPENSRC-2 (P1) — ffmpeg PNG frontier
**Status:** READY (2026-10-16, commit 6140184)
**Priority:** P1

## Milestone reached this session
- **`ffmpeg.exe -version` prints the FULL banner through the real loader, exit 0,
  3/3 deterministic** (evidence/master/bug041-048/) — the OPENSRC-class "G1 moment".
- BUG-040..048 closed (build repair, realloc-on-guard, wchar 2↔4 family x2,
  console struct overrun, console-mode fidelity, aligned-heap family, registry
  linear probing). All gates green: G1 pin (12/12), dispatch 4/4.
- llvm-mingw toolchain + ffmpeg + ImageMagick corpora restored in-container.
- Local commits 394f6d4 (merge) -> 3c1fea9 (BUG-040) -> 6140184 (BUG-041..048).
  PUSH BLOCKED: ~/.git-credentials lost between sessions (OWNER-004) — needs the
  token re-supplied by the owner; never store it in the repo.

## Next micro-tasks (see backlog for full detail)
1. TASK-OPENSRC-2: ffmpeg frame-thread NULL deref (RVA 0x8070d2d, after
   CreateEventW x4) — the PNG-pipeline acceptance.
2. TASK-TEST-014: rebuild sweep t01..t12 with the restored toolchain, re-pin 11/12.
3. TASK-OPENSRC-3: magick silent -version (trace the output path).
4. TASK-OPENSRC-4: magick coder SIGSEGV.
5. Queued: TEST-004-C1 (real ContinueSearch chain), TEST-004-D1 (dedupe test_d).
