# Knowledge Gap Report

**Date**: 2026-08-14
**Repository**: Sh-TB/miniwin-engineering

---

## Methodology

Read all 6 root documentation files (README, ARCHITECTURE, ENGINEERING_RULES,
KNOWLEDGE_BASE, ROADMAP, CHANGELOG) and evaluated whether a new developer or AI
agent can answer these 6 questions:

1. What is MiniWin?
2. Why does it exist?
3. What is the current architecture?
4. What is the current implementation status?
5. What are the remaining problems?
6. How should I continue?

---

## Question-by-Question Assessment

### Q1: What is MiniWin?
**Answered**: YES — comprehensively.

README.md lines 8-29 clearly define MiniWin as a "minimum viable Windows x64 PE
runtime compatibility layer for Linux" with a comparison table against Wine
and ReactOS. The distinction from Wine/ReactOS is well-articulated: MiniWin
implements only the behavior required by target applications, not a full
Windows stack. The AI-assisted debugging loop is described in 6 steps.

### Q2: Why does it exist?
**Answered**: YES.

README.md attributes inspiration to the SharpEmu PS5 emulator project's
"minimum viable emulation" principle. ENGINEERING_RULES.md provides the
philosophical foundation ("We discover the minimum Windows required by
applications"). docs/architecture/project_overview.md gives additional
context on the project's origin and design goals.

### Q3: What is the current architecture?
**Answered**: YES — excellently.

ARCHITECTURE.md (403 lines) provides:
- ASCII system overview diagram (loader, PE image, memory layout)
- 8-phase execution pipeline with pseudo-code for each
- ABI bridge documentation (ms_abi vs SysV, 4-point bridging strategy)
- Data structure definitions (RUNTIME_FUNCTION, UNWIND_INFO, CONTEXT offsets)
- Module dependency tree
- Known architecture issues (monolithic design, bump allocator, single TEB)

docs/windows_abi/windows_x64_complete.md (472 lines) provides the complete
Windows x64 ABI reference including calling convention, prolog/epilog rules,
and stack layout.

### Q4: What is the current implementation status?
**Answered**: YES — thoroughly.

Multiple documents provide status:
- README.md "Current Capabilities" section: 14 verified working items
- README.md "Current Limitations" section: 8 issues including critical blocker
- KNOWLEDGE_BASE.md "UPX Real Execution: Full Dispatch Trace": 5-frame walk detail
- docs/reports/current_status.md: table with 15 components and evidence
- ROADMAP.md milestones table: M1-M7 DONE, M8 BLOCKED, M9-M13 PENDING
- CHANGELOG.md: 7 development sessions with hashes

### Q5: What are the remaining problems?
**Answered**: YES.

The critical blocker is documented in 4 places:
- README.md lines 148-155: RtlUnwindEx context restoration
- KNOWLEDGE_BASE.md lines 244-259: Full dispatch trace showing exact failure point
- docs/reports/current_status.md lines 34-75: Complete flow diagram from RaiseException
  to SIGSEGV, with root cause explanation
- ROADMAP.md Phase 1.1: Specific approach for fixing RtlUnwindEx

Known issues beyond the blocker:
- __C_specific_handler is a no-op (GCC LSDA interpretation needed)
- Bump allocator never frees
- No DLL loading
- 236/3030 malformed UNWIND_INFO entries (documented as systematic GCC issue)

### Q6: How should I continue?
**Answered**: YES — with specific steps.

README.md "Quick Start for New Developers" (lines 258-267) gives 8 steps.
ROADMAP.md provides 5 development phases with specific tasks.
docs/reports/current_status.md gives 3 tiers of next steps (immediate, short, medium).
ENGINEERING_RULES.md provides 12 rules to follow during development.

---

## Identified Gaps

### Gap 1: Dual-Architecture Confusion [MEDIUM]

**Problem**: The `docs/` directory contains documentation from TWO different projects:

1. **C loader** (the active project): loader.c + pe.h, documented in root .md files
2. **Rust runtime** (separate reimplementation): `runtime/src/*.rs`, documented in
   `docs/BUILD_GUIDE.md`, `docs/KNOWN_ISSUES.md`, `docs/PROJECT_HISTORY_AND_STATUS.md`,
   `docs/TRACE_FORMAT.md`

A new agent reading `docs/BUILD_GUIDE.md` will expect Wine, MinGW cross-compiler,
and `cargo build` to be relevant. They are not — the C loader uses a simple Makefile.

**Recommendation**: Add a prominent note at the top of the Rust-era docs stating
these describe a separate Rust reimplementation, not the active C loader. Or move
them to a `docs/rust-runtime/` subdirectory.

### Gap 2: No Sample Binary [HIGH]

**Problem**: The regression test script requires `samples/upx_decompressed.exe`
(2.1MB). This binary is excluded from git. No document explains how to obtain it.

**Recommendation**: Add a `samples/README.md` explaining:
1. Download UPX 4.2.4 from https://upx.github.io
2. Run `upx -d upx.exe -o upx_decompressed.exe` to get the test binary
3. Place it at `samples/upx_decompressed.exe`
4. Hash: `254ac80deb8fc54bda028d574022e8231a3b684c177c2d35da75802ff78d4f4e`

### Gap 3: Compile Errors Not Documented [HIGH]

**Problem**: The current `loader.c` has 3 compilation errors:
1. `g_cap_er` undeclared (line 1177)
2. `EH_UNWINDING` redefined (lines 3113-3115)

No document mentions these errors. The FINAL_ENGINEERING_TRANSFER_REPORT.md
claims "Repository builds" — this is incorrect.

The pre-built binary at `/home/z/my-project/minwin/minwin_loader` (268KB) works
correctly, suggesting these errors were introduced in the final development session
and the binary was built from an earlier version.

**Recommendation**: Document these errors in BUILD_VERIFICATION.md (done in this audit).
The new agent must either fix these errors or use the baseline backup
(`loader.c.baseline_2134`) as a starting point.

### Gap 4: Makefile Path Mismatch [LOW]

**Problem**: The Makefile was written for the old flat structure (`src/loader.c`).
The repository uses `src/loader/loader.c`. The Makefile was fixed during this audit
but the fix needs to be committed.

### Gap 5: No Execution Log Evidence [LOW]

**Problem**: The archive has 28 execution logs and API trace JSON files from
development sessions. These are excluded by .gitignore (correctly — they are large
and generated). However, no summary of their contents exists in the committed docs.

The exception trace in `docs/reports/current_status.md` (lines 81-97) partially
compensates for this. The KNOWLEDGE_BASE.md also quotes key trace excerpts.

**Assessment**: Acceptable. Key evidence is quoted in documentation.

---

## Completeness Score

| Question | Score | Notes |
|----------|-------|-------|
| Q1: What is MiniWin? | 10/10 | Clear, concise, comparative |
| Q2: Why does it exist? | 9/10 | Philosophy clear; could mention target use cases |
| Q3: Architecture? | 10/10 | Excellent — diagrams, pipeline, ABI bridge, data structs |
| Q4: Status? | 10/10 | Multiple sources, evidence-backed |
| Q5: Problems? | 9/10 | Blocker well-documented; compile errors undocumented |
| Q6: How to continue? | 8/10 | Steps clear; dual-architecture confusion is a risk |
| **Overall** | **56/60** | **Substantially complete** |

---

## Verdict

The knowledge base is **substantially complete** for a new agent to continue
development. The main risks are:

1. **Dual-architecture confusion** (medium) — mitigated by reading root docs first
2. **Missing sample binary** (high) — blocks regression tests
3. **Undocumented compile errors** (high) — blocks clean build

All three risks are addressable with the information already in the repository:
- Gap 1: Root docs clearly describe the C loader
- Gap 2: Binary hash and version documented in KNOWLEDGE_BASE.md
- Gap 3: Baseline backup (`loader.c.baseline_2134`) compiles and works
