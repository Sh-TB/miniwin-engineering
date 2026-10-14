# MASTER PLAN — MiniWin Runtime Engineering

**Living document.** Every completed task updates `.agent/state.md`; every discovery feeds back here. Current revision: TASK-005, 2026-10-14, HEAD `39181aa` (local commit `miniwin-master-foundation-001`).

---

## 1. Vision

The smallest Windows x64 PE runtime for Linux that runs **real Windows console applications** without Wine, VMs, or recompilation — built target-by-target with evidence-based engineering, where every crash is converted into permanent capability and permanent documentation.

**Primary functional objective (unchanged):** `./minwin_loader samples/upx_decompressed.exe --version` prints `upx 4.2.4` and exits 0. **Not yet achieved.** Generalization to further targets follows only after this is reproducibly green.

## 2. Current Verified State (2026-10-14)

- Repository truth established (REPOSITORY_BASELINE.md): HEAD `39181aa`, main-only, no tags, 210 files, duplicates inventoried, UPX sample recovered & hash-verified.
- History reconciled (ENGINEERING_HISTORY_RECONCILIATION.md): 34 claims statused; 5 contradictions; 2 undocumented target-modifying hacks discovered (EH_FIX override; load_pe LSDA byte patch).
- Reproducible baseline (REPRODUCIBLE_BASELINE.md): clean build OK (gcc 14.2, 0 errors); regression **10/12**; EXP-NEXT-2 PASS; dispatch tests broken/vacuous; unwind units 35/35; **UPX acceptance FAILS (exit 134, no output)**.
- Pipeline mapped (EXECUTION_PIPELINE.md): full stage map, implemented-vs-executing distinction, ABI inventory.
- Failure forensics (UPX_FAILURE_FORENSICS.md): **verified root cause BUG-026** — `g_cap_rip` capture loads `[rsp+0x00]` instead of saved rip `[rsp+0x08]`; probe-proven; historical landing-pad crash arithmetically reconstructed (RSP window 0x68); 5 new latent defects catalogued (N-1..N-5).
- Mapping/relocation audited statically (IMAGE_MAPPING_AND_RELOCATION_AUDIT.md): forced preferred-base mapping OK for current target class; no relocations; bounds/adversarial gaps queued (MAP-01..07).
- API surface measured (API_COMPATIBILITY_DATABASE.md): 161 code + 4 data imports; mission-flagged APIs classified; several documented APIs (VirtualAlloc/Heap*) absent from current table.

## 3. Primary Blocker

**BUG-026** — one-operand defect in `mw_RaiseException` stub makes every dispatch start with `CONTEXT.Rip` = stack address → immediate walk termination → `terminate`/abort(134). Fix queued as next task (see `.agent/CURRENT_TASK.md`). All deeper exception-path work is **blocked** until it lands.

## 4. Dependency Graph (summary — full version in .agent/DEPENDENCY_GRAPH.md)

```
BUG-026 (g_cap_rip) ──► EXP-036 re-run UPX (live dispatch path)
   │                        ├─► EXP-037: landing-pad state verification (EXP-035 arithmetic live)
   │                        ├─► API-004/EXP-038: OriginalContext/RDX selector
   │                        └─► API-005: flag EH hacks off → compare paths
   ├─► TEST-004/005 (dispatch tests + Tier-B builder) ──► Tier-B suite
   └─► API-002/003 (trace flush, UEF wiring)
ALLOC_LARGE fix (API-006) ──► TEST-002
Mapping decisions (D-MAP-A/B/C) ──► MAPPING-01..07 ──► second-target readiness
Acceptance gate (TEST-008) ──► RELEASE_CRITERIA G1
```

## 5. Phase Gates (acceptance criteria per phase — phases/ directory)

| Phase | Gate to exit |
|---|---|
| 00 repository recovery | DONE this session (baseline + reconciliation committed) |
| 01 foundation | DONE this session (master system + CONTINUE protocol + backlog; this document) |
| 02 UPX forensics | DONE for current failure (BUG-026 root cause verified); re-opens for landing-pad failure after EXP-036 |
| 03 memory & relocation | Static audit DONE; dynamic tests MAPPING-01..07 pending; closed when MAP-1/2/8/9 mitigations land with tests |
| 04 exception correctness | BUG-026 fix + Tier-B exception suite green + EXP-037/038 conclusions recorded |
| 05 API compatibility | P0/P1 API backlog items verified; no fake-success on the UPX path |
| 06 generalization | TEST-008 acceptance green + one additional independent console target green |
| 07 second target | Track common roots, no app-specific patches |
| 08 runtime architecture | Module boundaries per evidence (dependency map exists; no speculative split) |
| 09 stability & release | RELEASE_CRITERIA.md all gates |

## 6. Risks

| Risk | Mitigation |
|---|---|
| Landing-pad state still wrong after BUG-026 (multiple stacked causes: EXP-035 arithmetic, RDX selector, EH hacks) | Experiments isolated one-at-a-time; OriginalContext adoption is the architecturally clean path; Tier-B synthetic PEs make each cause independently testable |
| EH hacks masked real defects in historical sessions | API-005 defaults them OFF; behavior differences documented per run |
| MAP_FIXED destroying memory on new targets | D-MAP-A collision detection before any second target |
| Documentation drift (root cause of this crisis) | Rule: every task updates state.md + evidence index in the SAME commit; CONTINUE protocol Step F enforces |
| Push unavailable (no credentials) | Local commits + explicit "not pushed" status in SESSION_HANDOFF; owner pushes or provides credentials |

## 7. Continuation Procedure

Owner sends `CONTINUE` in a fresh session → agent executes `.agent/CONTINUE_PROTOCOL.md` (steps A–H) starting from `.agent/state.md` + `.agent/CURRENT_TASK.md`. One bounded task per invocation by default.
