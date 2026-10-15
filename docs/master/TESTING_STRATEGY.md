# TESTING STRATEGY — Master Record

**Produced:** TASK-005, 2026-10-14, at HEAD `39181aa`
**Machine-readable manifest:** `evidence/master/test_manifest.json` (maintained by TASK-005 and updated by every task that adds/changes tests; current results in `evidence/master/baseline_results.json`)

---

## 1. Audit of Existing Tests (executed fresh this session)

| Test | Tier | Verdict | Notes |
|---|---|---|---|
| `tests/regression/test_pe_loader_regression.sh` (12 checks) | E | **KEEP, re-scope** | Valid regression *pins*, but test #10 asserts exit 139 (a crash) as success — it pins "known failure state", not functionality. After BUG-026 the expected exit must be revisited; keep 139-pinning until acceptance path changes, then update deliberately |
| `tests/exp_next2/exp_next2_harness.c` | B* | **KEEP with caveat** | PASS (exit 0). Embeds its own lookup/unwind/dispatch copies — validates algorithms, NOT the integrated loader. Label as algorithm-proof, not integration proof |
| `tests/dispatch_tests/test_dispatch_harness.c` + test_a–d.exe | B | **BROKEN/VACUOUS** | (a) import patching never engages (no `[IMPORT]` log; IAT stays 0 → test_a SIGSEGV via null call, test_b/c/d never raise); (b) verdict defaults to PASS when nothing dispatches. Repair or replace (Phase-7 task TEST-004) |
| `working-tree/tests/exception_dispatch/test_unwind_unit.c` (35 assertions) | A* | **KEEP, port** | PASS 35/35 from source rebuild; include-path fragile (`../../include` only resolves from odd CWDs); embeds algorithm copies — port into `tests/unit/` with proper includes and keep as Tier A |
| `tests/dalvik_frame/test_frame.c` | out-of-scope | **PRESERVE** | 24/24 from source build; unrelated artifact (mission rule) |
| `tests/synthetic_pe/hello.c` | B | **REVIVE** | Source for synthetic PEs; needs a build script wired into the test flow |

## 2. Test Tiers (mission §8)

- **Tier A — Unit (C, no PE):** PE field parsing, RUNTIME_FUNCTION lookup edges, unwind-code decoding (every opcode incl. ALLOC_LARGE both infos), CONTEXT layout/reg_to_ctx_offset, LSDA decode (LPStart/TType/CS/actions), boundary math. Sources: port `test_unwind_unit.c`; add `test_alloc_large.c`, `test_lsda_decode.c`.
- **Tier B — Synthetic PE integration:** harness loads a generated PE into the REAL loader path and asserts end-to-end behavior. Required builders/cases:
  - no-exception happy path (hello.c) — output + exit 0
  - single-frame catch (func throws, same func catches, landing pad asserts registers/stack)
  - nested frames + ContinueSearch chain
  - cleanup (UHANDLER) invocation order during unwind
  - selector-dependent landing pad (validates RDX propagation / API-004)
  - data-import consumption
- **Tier C — Real targets:** UPX 4.2.4 decompressed (`--version` acceptance), then independently selected console targets (see LONG_TERM_ROADMAP Phase E). No UPX-specific code paths allowed — targets must be interchangeable.
- **Tier D — Negative/adversarial:** truncated PE, bad magics, OOB `PointerToRawData`, OOB RVA directories, unsupported relocation types (post D-MAP-B), unmapped IAT call (post API-007 trap), malformed UNWIND_INFO (count/slot overflow — partially exists in the wild sample), stack exhaustion via deep throw, mapping-collision refusal (post D-MAP-A).
- **Tier E — Regression:** the 12-check suite (updated deliberately), plus "golden trace" assertions for the dispatch path (frame count, handler RVAs, dispositions) so behavior changes are visible.

## 3. Acceptance rules (hard)

1. A test passes only if the intended behavior actually executed (handler invoked, landing pad reached, output matched, exit code exact).
2. No verdict-by-default: every harness must FAIL if its scenario did not run (dispatch tests' current flaw is the canonical counterexample).
3. Source-build only: prebuilt artifacts may exist for provenance but never count as PASS.
4. Every task ships: focused test(s) + Tier E green before/after, recorded in `evidence/master/test_manifest.json`.
5. Environment-blocked tests are recorded BLOCKED with the exact missing dependency — never skipped silently.

## 4. Queue (bounded tasks)

| ID | Task | Tier | Priority |
|---|---|---|---|
| TEST-001 | Port unwind unit tests to `tests/unit/` with portable includes | A | high |
| TEST-002 | Add ALLOC_LARGE unit tests (both infos; spec-correct vectors) | A | high (pairs API-006) |
| TEST-003 | Add LSDA decode unit tests incl. cleanup-vs-catch action distinction | A | high (pairs API-005) |
| TEST-004 | Fix dispatch_tests: import patching + fail-if-not-dispatched verdicts | B | high |
| TEST-005 | Synthetic PE builder v2 (scripts/build) emitting the Tier-B case set | B | high |
| TEST-006 | Tier-D adversarial suite (bounds/trap cases) | D | medium (pairs D-MAP-C, API-007) |
| TEST-007 | Golden-trace assertions for dispatch path | E | medium |
| TEST-008 | UPX acceptance harness (stdout match "upx 4.2.4", exit 0) as the single acceptance gate | C | high |
