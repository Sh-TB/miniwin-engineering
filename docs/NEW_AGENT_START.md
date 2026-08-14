# New Agent Quick Start Guide

**Read this file first. Everything you need is here.**

---

## Project Goal

Build the smallest possible runtime that can execute real Windows x64 PE applications
on Linux — without Wine, without a Windows VM, without recompilation.

Not a Windows clone. The minimum Windows required by applications.

---

## Current State

**The runtime can load a PE, resolve all imports, run CRT initialization, and
dispatch exceptions. It cannot yet complete exception unwinding.**

Milestone status:
- M1-M7: DONE (PE loading through handler invocation)
- M8: BLOCKED (RtlUnwindEx context restoration)
- M9-M13: PENDING

The target application (UPX 4.2.4 `--version`) reaches RaiseException,
walks 5 frames, calls 2 EH handlers, but crashes with SIGSEGV because
RtlUnwindEx does not properly restore context to resume at the catch
landing pad.

---

## Golden Rules (from ENGINEERING_RULES.md)

1. **Evidence First** — Never implement without evidence from a real binary
2. **Never Hide Failures** — Every crash becomes BUG-XXX.md
3. **Build From Smallest Target** — Hello PE -> Simple C -> CLI -> GUI
4. **Separate Layers** — Loader -> API -> Win32 -> CRT -> GUI
5. **Every Feature Needs a Test** — No untested features
6. **Checkpoint Before Major Changes** — Never destroy the only working version
7. **Oracle Comparison** — Compare against Wine/Windows behavior
8. **Implement Behavior, Not Names** — A function returning TRUE is not compatibility
9. **AI Assisted Evolution** — Every app run produces knowledge
10. **Prefer Minimal Implementation** — Smallest Windows for maximum software
11. **No Fake Success** — Correct output, not just "doesn't crash"
12. **Knowledge Must Survive** — All discoveries documented permanently

---

## Do NOT Break These

These components are verified working. Do NOT modify them without running
the full regression suite (12 tests) before AND after:

- PE loader (`load_pe` function)
- Import resolver (162/162 resolved)
- CRT stubs (`_initterm`, `__getmainargs`, `__set_app_type`)
- TEB/PEB setup
- Heap allocator (bump allocator)
- RtlLookupFunctionEntry (binary search through 3030 .pdata entries)
- RtlVirtualUnwind (9 opcodes + GCC extensions 9-15)
- .pdata parser
- Naked RaiseException stub (`__attribute__((naked, ms_abi))`)
- Signal handler (SIGSEGV/SIGABRT)

---

## Important Files

| File | What it tells you |
|------|-----------------|
| **README.md** | Project overview, build, test, capabilities, structure |
| **ARCHITECTURE.md** | System design, execution pipeline, ABI bridge, data structures |
| **KNOWLEDGE_BASE.md** | All accumulated engineering knowledge (PE loading, imports, SEH, ABI) |
| **docs/bugs/BUG-024-rtl-dispatch-exception.md** | Current task: exception dispatcher |
| **docs/reports/current_status.md** | What works, what's blocked, exact failure trace |
| **ROADMAP.md** | What to work on next (5 phases) |
| **ENGINEERING_RULES.md** | How to develop (12 rules) |
| **docs/SOURCE_MAP.md** | What code is where in the monolithic loader.c |
| **docs/EXPERIMENT_INDEX.md** | History of all experiments and bugs |
| **docs/windows_abi/windows_x64_complete.md** | Windows x64 ABI reference |
| **docs/reports/ARCHIVE_INTEGRITY_REPORT.md** | What's in the repo, what's missing |
| **docs/reports/KNOWLEDGE_GAP_REPORT.md** | Gaps in documentation |
| **docs/reports/BUILD_VERIFICATION.md** | Build and test results |

---

## Architecture at a Glance

Everything is in **one C file**: `src/loader/loader.c` (3115 lines).

```
load_pe() -> resolve_imports() -> setup_teb_peb() -> jump to EP
                                                      |
                                                  CRT init runs
                                                      |
                                              RaiseException called
                                                      |
                                              seh_dispatch_exception()
                                                      |
                                          RtlLookupFunctionEntry()
                                              + RtlVirtualUnwind()
                                                      |
                                              Handler found? Call it
                                                      |
                                          Handler wants unwind?
                                              RtlUnwindEx() [BLOCKED HERE]
```

Header: `include/pe.h` (269 lines) — PE structures, CONTEXT offsets, SEH types.

---

## Current Blocker

**RtlUnwindEx context restoration is incomplete.**

When the GCC personality routine at Frame[4] requests an unwind to the catch
landing pad (target_ip=0x40331c), RtlUnwindEx longjmps back to the dispatcher.
The dispatcher receives the longjmp but does not:
1. Walk frames from exception site to target frame
2. Restore nonvolatile registers from the unwind
3. Set RSP = target frame
4. Jump to target_ip

Result: SIGSEGV at 0x49c9c6 (PE's UnhandledExceptionFilter path).

---

## Build Status

**The current `loader.c` has 3 compile errors:**
1. `g_cap_er` undeclared (line 1177) — missing global variable declaration
2. `EH_UNWINDING` redefined (lines 3113-3115) — duplicate constant definitions

**Workaround**: Use `src/loader/loader.c.baseline_2134` (2134 lines) as the
starting point. It compiles and passes all 12 regression tests. Then port
the exception dispatcher code from the current loader.c into it.

The pre-built binary at the original location works correctly and passes
all regression tests — the compile errors were introduced in the final
development session.

---

## Recommended First Task

### Option A: Fix compile errors and continue RtlUnwindEx (direct path)

1. Fix the 3 compile errors in `src/loader/loader.c`
2. Verify build: `make`
3. Run regression: `./tests/regression/test_pe_loader_regression.sh .`
4. Implement context restoration in RtlUnwindEx (ROADMAP Phase 1.1)
5. Test with UPX `--version`

### Option B: Start from baseline and port forward (safer path)

1. Copy `src/loader/loader.c.baseline_2134` to `src/loader/loader.c`
2. Verify build and regression pass
3. Port the exception dispatcher (~lines 1100-1550 from exp_next2_backup)
4. Port the naked RaiseException stub
5. Test with UPX `--version`
6. Continue with RtlUnwindEx

### Option C: Read and understand first (recommended for new AI agents)

1. Read ARCHITECTURE.md completely
2. Read docs/windows_abi/windows_x64_complete.md
3. Read docs/bugs/BUG-023.md (full SEH history)
4. Read docs/experiments/EXP-NEXT-3-real-binary-exception-dispatch.md
5. Read src/loader/loader.c lines 1100-1550 (the dispatcher)
6. Read src/loader/loader.c lines 800-850 (RaiseException implementation)
7. Read src/loader/loader.c lines 1550-1900 (unwind engine)
8. Then proceed with Option A or B

---

## Critical Numbers

| Item | Value |
|------|-------|
| PE ImageBase | 0x00400000 |
| Loader text-segment | 0x2000000 |
| UPX EntryPoint (RVA) | 0x000014F0 |
| UPX .pdata entries | 3030 |
| Total imports | 162 (KERNEL32: 68, msvcrt: 94) |
| EH handlers in UPX | ~20 (all at GCC personality RVA 0xe0220) |
| Malformed UNWIND_INFO | 236/3030 |
| Exception class | 0x474E5543432B2B00 ("GNUCC++") |
| Target unwind IP | 0x40331c |
| CONTEXT struct size | 0x4D0 bytes |
| UPX binary hash | 254ac80d...f78d4f4e |

---

## Getting the Test Binary

The regression tests need `samples/upx_decompressed.exe`:

```bash
# Download UPX 4.2.4
wget https://github.com/upx/upx/releases/download/v4.2.4/upx-4.2.4-linux64.tar.xz
tar xf upx-4.2.4-linux64.tar.xz
# Decompress the binary (keeps PE format but removes compression)
upx-4.2.4-linux64/upx -d upx-4.2.4-linux64/upx.exe -o samples/upx_decompressed.exe
# Verify hash
sha256sum samples/upx_decompressed.exe
# Expected: 254ac80deb8fc54bda028d574022e8231a3b684c177c2d35da75802ff78d4f4e
```
