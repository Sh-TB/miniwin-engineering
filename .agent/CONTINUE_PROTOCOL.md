# CONTINUE PROTOCOL — MiniWin Master Engineering System

**Trigger:** the owner sends exactly `CONTINUE` in a new session with access to this repository.
**Contract:** this protocol is self-contained. It must be executable without any prior conversation history. If any file it references is missing, treat that as a BLOCKER and report it (do not improvise a replacement).

---

## Step A — Synchronize (never skip)

1. Read `.agent/mission.md` and this file.
2. Fetch the remote and record truth: `git fetch origin && git rev-parse origin/main HEAD` — note whether they differ and whether local is ahead (previous session committed without push).
3. `git status` — inspect working-tree changes. If unexplained changes exist (user or another agent), **do not overwrite or discard them**; record them in the session report and work around them.
4. Read, in order: `.agent/state.md`, `.agent/backlog.md`, `.agent/CURRENT_TASK.md`, `.agent/SESSION_HANDOFF.md`.
5. Read the latest experiment report under `docs/master/experiments/` that state.md references, and the evidence files it cites.
6. **Reality check (mandatory):** verify the previous session's claims — (a) claimed build: run `make clean && make` if source changed since; (b) claimed tests: re-run the focused test(s) named in CURRENT_TASK.md; (c) claimed source state: `git log -3 --stat` + `sha256sum src/loader/loader.c include/pe.h` and compare with state.md. Any mismatch → record it, trust the observed reality, update state.md.

## Step B — Select exactly one bounded task

1. If `.agent/CURRENT_TASK.md` has a task `IN_PROGRESS` with a valid partial implementation → **resume it**; do not restart.
2. Otherwise pick the highest-priority `READY` task from `.agent/backlog.md` whose dependencies are all `VERIFIED`/`TESTED`.
3. Prefer the smallest task that removes a verified blocker or unlocks the most downstream tasks (see `.agent/DEPENDENCY_GRAPH.md`).
4. Do not start unrelated work while an active task exists. If new evidence invalidates the task's hypothesis, update the hypothesis in the task entry and select the better-supported experiment instead.

## Step C — Establish task baseline (before editing)

1. Record current commit SHA + dirty/clean status in the task entry.
2. Run the relevant baseline tests (at minimum: `make clean && make` and `./tests/regression/test_pe_loader_regression.sh .` when loader.c changes) and record exact results.
3. For risky changes: create `checkpoints/checkpoint_pre_<taskid>.zip` (source + Makefile + relevant tests).
4. List the files you intend to change and the expected regression surface.

## Step D — Execute (bounded, reversible)

1. One root cause → one minimal implementation → one focused test, whenever possible.
2. Forbidden substitutes for correct general implementation: stack scanning, arbitrary register substitution, fabricated success values, hardcoded application addresses, app-specific patches. **If such a mechanism exists in the code you are touching (see EH_FIX L1607-1644, CXX-EH-FIX L3263-3287), investigate and isolate it first (API-005) rather than silently preserving or deleting it.**
3. Update `docs/master/ARCHITECTURE_DECISIONS.md` if you make an architecture-level choice.

## Step E — Verify

1. Run focused tests + regression suite. Record **exact exit codes and outputs**.
2. If a required binary/dependency is missing → record the exact blocker in the task entry, mark task BLOCKED, do not claim success.
3. If a regression appears → repair or revert **before** proceeding. Never leave the tree red.

## Step F — Commit engineering knowledge (same commit where possible)

Update, as applicable: task report (backlog entry Result field), `docs/master/experiments/` (if an experiment ran), `docs/bugs/` (new bug → BUG-0XX file), `docs/master/ARCHITECTURE_DECISIONS.md`, `.agent/backlog.md`, `.agent/state.md`, `.agent/CURRENT_TASK.md`, `.agent/SESSION_HANDOFF.md`, `.agent/EVIDENCE_INDEX.md`, `evidence/master/` artifacts, `docs/master/MASTER_PLAN.md` phase status, `docs/master/phases/PHASE_*.md`.
Then commit locally with a message prefixed by the task ID (e.g. `BUG-026: restore g_cap_rip capture operand`).
Push **only if** credentials permit; if the push fails or is unconfigured, state "committed locally, NOT pushed" — never claim a push that GitHub did not confirm (`git rev-parse origin/main` after push must equal local HEAD).

## Step G — Stop at the boundary

One CONTINUE = one bounded task or one explicitly defined investigation stage. Do not attempt large unverified rewrites to "finish a phase". If the owner explicitly requests multiple steps AND evidence/boundaries remain valid, you may continue — but each task still follows C–F individually.

## Step H — Return a concise handoff (final message to owner)

Report exactly:
1. What was investigated.
2. What was actually changed (files + nature).
3. What tests ran and their exact outcomes.
4. What evidence was added.
5. Whether committed and/or pushed (with SHAs).
6. The current blocker.
7. The next task ID.
8. The exact next command: `CONTINUE`.

If blocked: preserve investigation state (experiment file + evidence) so the next session resumes without repeating the analysis.

---

## First-task defaults (when no active task and backlog is ambiguous)
Next task = `BUG-026` (see `.agent/CURRENT_TASK.md`). Do not skip it even if another fix looks obvious — the dependency graph roots at it.
