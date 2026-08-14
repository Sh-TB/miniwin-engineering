# Known Issues

## AI-Native Windows Compatibility Runtime

**Version:** 0.1.0-pre
**Last Updated:** 2026-07-30

---

## Critical Issues

### 1. Wine Relay Trace Is Extremely Verbose

**Severity:** High
**Impact:** A simple HelloWorld generates 90MB of trace data (1.3M lines, 593K API calls).
Most of these calls are Wine internal operations (ntdll.RtlFreeHeap alone = 332K calls),
not calls made by the target application.

**Root Cause:** Wine's `+relay` channel traces ALL processes including wineserver,
wineboot, and all Wine infrastructure DLLs. There is no way to filter by target process.

**Workaround:** None currently. The parser captures everything and post-processes.

**Proposed Fix:** Use Wine's `+relay` with ` RelayExclude` or `RelayInclude` registry
settings to filter to specific DLLs. Alternatively, use a DLL injection approach to hook
only the target process's API calls.

---

### 2. Three Separate Executions Required

**Severity:** Medium
**Impact:** The trace collector runs the target EXE three times:
1. Clean run (WINEDEBUG=-all) — stdout capture
2. Relay run (WINEDEBUG=+relay) — API call trace
3. Module run (WINEDEBUG=+module) — DLL loading trace

Each run has different timing and thread behavior. The traces are not from the same execution.

**Root Cause:** Wine only supports one WINEDEBUG value per execution. Different channels
(+relay, +module, -all) cannot be combined into a single run without capturing all channels.

**Proposed Fix:** Wine can actually combine channels with comma separation:
`WINEDEBUG=+relay,+module`. However, the combined output mixes API call lines with
module loading lines, requiring more complex parsing. Additionally, `+relay` and `-all`
are mutually exclusive — you cannot get clean stdout AND API traces in one run.

---

### 3. No Windows Oracle for Comparison

**Severity:** High (for the project's core goal)
**Impact:** We can observe Wine execution but cannot compare it against "correct" Windows
execution. Without a Windows reference trace, we cannot detect Wine-specific bugs.

**Root Cause:** No Windows machine or Windows CI available in the current environment.

**Proposed Fix:** Set up a Windows CI pipeline or use cloud Windows VMs to generate
reference traces for comparison.

---

### 4. Thread ID Reuse Complicates Parsing

**Severity:** Medium
**Impact:** Wine recycles thread IDs across different processes. The relay parser uses
thread ID as the key for pairing Call/Ret lines. If two threads share the same ID at
different times, or if nested calls use the same thread ID, parsing can mis-pair calls.

**Root Cause:** Wine's relay output uses a 4-character hex thread ID. When Wine infrastructure
processes finish and new ones start, IDs can be reused.

**Proposed Fix:** Track call stack depth per thread ID. Only pop the pending call when
the stack is not empty. Use process ID (available in Wine relay lines) as an additional key.

---

### 5. No Memory Access Tracing

**Severity:** High (for crash analysis)
**Impact:** Wine's relay only traces API calls, not memory reads/writes. When an application
crashes due to an invalid memory access, we cannot see the specific memory operation that failed.

**Prop Cause:** Wine's debug channels do not include memory access tracing.

**Proposed Fix:** Use Wine's `winedbg` with breakpoints or GDB integration to capture
memory access violations. Alternatively, use Valgrind-style instrumentation.

---

## Architecture Issues

### 6. Two Separate Trace Formats

**Severity:** Medium
**Impact:** The Rust runtime produces JSON trace events with typed fields. The Wine trace
collector produces parsed Wine relay output with hex-encoded arguments. These cannot be
compared or analyzed together.

**Proposed Fix:** Design a unified trace format (see `docs/TRACE_FORMAT.md` — Future section)
and migrate both producers to use it.

---

### 7. AI Analysis Is Template-Based

**Severity:** Medium
**Impact:** The AI analysis engine (`runtime/src/analysis/mod.rs`) generates suggestions
based on hardcoded patterns, not actual AI/ML inference. It does not learn from traces or
generate intelligent insights.

**Root Cause:** The analysis module was designed as a placeholder. Real AI integration
requires connecting to an LLM API or running a local model.

**Proposed Fix:** Connect the analysis module to the z-ai-web-dev-sdk LLM API. Feed trace
data as structured prompts and parse the response into fix suggestions.

---

### 8. Simulated Execution Is Not Real Execution

**Severity:** Low (by design, but important to document)
**Impact:** The simulated execution backend does not actually execute x86-64 instructions.
It traces the PE load process and dispatches imported API functions with synthetic arguments.
It cannot test real application behavior.

**Note:** This was an intentional design decision. The project pivoted away from building
a CPU emulator. Real execution uses Wine. The simulated backend is for testing the pipeline
architecture.

---

## Environment Issues

### 9. No sudo Access

**Severity:** Low (workaround exists)
**Impact:** Cannot install system packages (wine, mingw) via apt. Must use portable binaries.

**Workaround:** All tools (Wine, MinGW) are downloaded as portable tarballs. No system
installation required.

---

### 10. Wine Wrapper Script Broken

**Severity:** Low (workaround exists)
**Impact:** The `wine` shell script in Wine's bin/ directory fails with:
```
/home/z/my-project/tools/wine-9.0-staging-tkg-amd64/bin/wine: 46: exec: .../bin/wine: not found
```

**Root Cause:** The `wine` wrapper uses `/bin/sh` which has issues in this container environment.

**Workaround:** Always call `wine64` binary directly, never the `wine` wrapper.

---

### 11. GUI Applications Cannot Be Tested

**Severity:** Medium (limits testing scope)
**Impact:** Console applications work, but GUI applications require an X11 display
or Wayland compositor, which are not available in this environment.

**Workaround:** None currently. Would need a virtual framebuffer (Xvfb) or VNC setup.

---

### 12. First Wine Run Is Slow

**Severity:** Low (cosmetic)
**Impact:** First execution after `wineboot --init` takes 5+ seconds due to Wine prefix
initialization, RPC server startup, and service registration.

**Note:** Subsequent runs are faster (~2 seconds for HelloWorld).

---

## Code Issues

### 13. Error Module Has Unused Variants

**Severity:** Very Low
**Impact:** `runtime/src/error.rs` defines a `NixError` variant using `nix::errno::Errno`,
but the `nix` crate is not in `Cargo.toml` dependencies. This will cause a compile error
if the variant is ever used.

**Proposed Fix:** Remove the `NixError` variant or add `nix` to dependencies.

---

### 14. goblin Dependency Is Unused

**Severity:** Very Low
**Impact:** The `goblin` crate (PE parsing library) is listed in `Cargo.toml` but the
primary PE parser is custom (`runtime/src/pe/mod.rs`). The `goblin` import appears in error
handling but is never called directly.

**Proposed Fix:** Either remove `goblin` from dependencies or use it as a secondary parser
for validation.

---

### 15. No Integration Tests

**Severity:** Medium
**Impact:** `runtime/tests/` is empty. There are unit tests in `pe/mod.rs` but no integration
tests for the full pipeline (parse → load → execute → trace → replay → analyze).

**Proposed Fix:** Add integration tests that:
1. Parse a PE file
2. Execute it under simulated backend
3. Verify trace output
4. Replay the trace
5. Verify regression spec
6. Run analysis and verify report

---

### 16. Relay Parser Does Not Handle All Return Formats

**Severity:** Low
**Impact:** Some Wine relay return lines may not match the current regex patterns.
Observed patterns that work:
- `TID:Ret  MODULE.Function() retval=VALUE ret=ADDR` (standard)
- `TID:Ret  PE DLL (...) retval=VALUE` (DLL notification)

Patterns that may not work:
- Functions with ` wine` in the name (wine-tkg specific functions)
- Functions with unusual characters
- Very long argument lists that wrap lines

**Proposed Fix:** Make the parser more robust by:
1. Logging unmatched lines for manual inspection
2. Using a more flexible parsing approach
3. Adding unit tests with real Wine output samples

---

## Summary

| Category | Critical | Medium | Low |
|----------|----------|--------|-----|
| Trace verbosity | #1 | | |
| Comparison baseline | #3 | | |
| Memory tracing | #5 | | |
| Execution consistency | | #2 | |
| Thread ID parsing | | #4 | |
| Format unification | | #6 | |
| AI quality | | #7 | |
| GUI testing | | #11 | |
| Integration tests | | #15 | |
| Parser edge cases | | #16 | |
| Code cleanup | | | #13, #14 |
| Environment | | | #9, #10, #12 |
| Design limitation | | | #8 |
