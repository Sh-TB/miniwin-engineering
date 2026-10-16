# LONG-TERM ROADMAP — Dependency-Driven

Principle: each phase has entry criteria, exit criteria, dependencies, and a rollback plan. No phase starts while its entry criteria are unmet; no phase declares exit while acceptance criteria are open. Detail lives in `docs/master/phases/PHASE_*.md`.

## Phase A — Repository truth & reproducibility ✅ (this session)
- Entry: none. Exit: baseline documented, history reconciled, clean build recorded, test manifest exists, sample verified. **EXITED 2026-10-14.**
- Residual: knowledge-transfer/working-tree mirror dedup (owner decision), push to GitHub.

## Phase B — UPX failure forensics 🔄 (in progress)
- B1 ✅ Reproduce current failure + root cause (BUG-026 verified).
- B2 ⬜ BUG-026 fix + EXP-036 live re-run (regression + UPX).
- B3 ⬜ Landing-pad forensics round 2 (EXP-037 EXP-035 live; EXP-038 OriginalContext/selector; EH-hack isolation API-005).
- B4 ⬜ Minimal failing test for whatever failure remains (Tier-B synthetic mirroring the real path).
- Exit: root cause of each *observed* failure documented with evidence; UPX reaches catch landing pad with correct state OR the residual cause is identified with a distinguishing experiment queued.

## Phase C — Correct Windows x64 exception semantics
- Entry: Phase B exit. Scope: CONTEXT creation; RF lookup; UNWIND_INFO decoding (incl. ALLOC_LARGE spec fix); frame walking; handler invocation & dispositions; target-frame unwinding; machine-state restoration; cleanup/termination semantics within declared scope; landing-pad continuation; explicit documentation of which exception ABI the target uses (GCC DWARF-LSDA-via-SEH for UPX — NOT interchangeable with MSVC C++ EH; `__C_specific_handler` work belongs to a later phase unless a target needs it).
- Exit: Tier-B exception suite green end-to-end through the real loader; no app-specific bypasses active.

## Phase D — API correctness
- Entry: Phase C exit (UPX path stable). Scope: eliminate false-success stubs on required paths (P0/P1 of API_IMPLEMENTATION_BACKLOG), module/procedure lookup honesty, real memory-management behavior, file/process semantics, error codes & side effects validated.
- Exit: API database classifications match behavior for every API exercised by UPX + Tier-B synthetics; unresolved-import trap active.

## Phase E — Generalization
- Entry: Phase D exit + G5 mapping safety. Scope: minimal independent Windows x64 console target (built from source we control) → second independently selected target → only then further targets. Common roots tracked (e.g., if both targets need VirtualAlloc, implement it once, generally).
- Exit: TEST-008-style acceptance green for two independent targets; zero UPX-specific code paths in general runtime.

## Phase F — Maintainability & release quality
- Entry: Phase E exit. Scope: evidence-based module boundaries (loader.c split ONLY with mapped seams), memory-safety checks (ASan/valgrind runs in CI), negative tests, stable CLI, reproducible build instructions, clean-clone verification, documented compatibility limitations.
- Exit: RELEASE_CRITERIA.md all gates green.

## Standing rules across all phases
- One bounded task at a time; preserve known-good baseline; checkpoint before risky change; never delete historical evidence; never equate compilation/advance/crash-free with correctness.
