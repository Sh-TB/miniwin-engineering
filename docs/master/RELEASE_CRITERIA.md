# RELEASE CRITERIA — MiniWin Runtime

## G1 — Primary functional acceptance (UPX 4.2.4 decompressed)
**STATUS: MET — 2026-10-15 (CONTINUE #5, EXP-039; evidence `evidence/master/bug031/`; binary sha256 e0bdbdcf…)**
- [x] `./minwin_loader samples/upx_decompressed.exe --version` prints `upx 4.2.4` (exact stdout match, byte-for-byte on the version line)
- [x] Process exit status 0
- [x] No hidden crash: no SIGSEGV/SIGABRT swallowed, crash_handler path unused on this run
- [x] No fake success: output originates from PE code through the runtime, not from loader-side shortcuts — the run exercises the real GCC SEH chain (raise → walk → phase-1 → bridge resume-command → cleanup LP → continuation unwind → catch LP → C++ runtime)
- [x] Observed and recorded in `evidence/master/bug031/` (trace `upx_trace_g1.json` 943 events + stdout + exit code + hashes + 12/12 regression log)

## G2 — Regression integrity
- [x] Regression suite green (12/12 with expectations updated to the *accepted* behavior — test-10 asserts exit 0 + UPX banner since G1 met 2026-10-15)
- [ ] Tier-A unit suite green (incl. ALLOC_LARGE spec vectors)
- [ ] Tier-B synthetic exception suite green (catch, cleanup, selector, nested frames)
- [ ] EXP-NEXT-2 algorithm proof still green

## G3 — Clean-clone reproducibility
- [ ] Fresh `git clone` + documented sample-recovery step + `make` → binary hashes recorded
- [ ] Test manifest executes green end-to-end on a clean environment
- [ ] Build warnings: only accepted classes remain (no new `-Wformat`/use-after-free)

## G4 — Scope honesty
- [ ] `API_COMPATIBILITY_DATABASE.md` classifications match code
- [ ] No undocumented target-modifying behavior (EH_FIX/CXX-EH-FIX either removed or flag-gated default-off and documented)
- [ ] `README.md` "Current Capabilities" updated to verified-only claims (remove stale VirtualAlloc/Heap claims)
- [ ] ENGINEERING_RULES.md "no fake success" audited against the database (no STUB-FAKE on the acceptance path)

## G5 — Memory & mapping safety (blocks second target, not UPX)
- [ ] D-MAP-A collision detection with MAPPING-02/07 green
- [ ] D-MAP-C bounds validation with Tier-D suite green
- [ ] Relocation support or explicit refusal documented (D-MAP-B) with MAPPING-03/04/05 outcomes recorded

## G6 — Continuation health
- [ ] `.agent/state.md` matches repo reality (verified by CONTINUE Step A checks)
- [ ] Evidence index links resolve; every important claim carries an evidence pointer
- [ ] Last session handoff names the next bounded task

## Non-goals for first release
GUI, multi-threading, DLL loading, MSVC C++ exceptions, resource loading — all documented limitations, none silently pretended.
