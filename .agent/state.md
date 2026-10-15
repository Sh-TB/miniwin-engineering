# Engineering State — CANONICAL COMPACT SNAPSHOT

**Updated:** 2026-10-15 (CONTINUE #6: **appcompat sweep 7/9 PASS**; BUG-032..036 CLOSED TESTED;
TASK-TLS-001 + TASK-API-007/008 CLOSED TESTED; EXP-040; backlog +31). Supersedes prior snapshots.

| Field | Value |
|---|---|
| Current HEAD (local) | see git log (CONTINUE #6 commit on top of 17c024a) |
| Origin/main | 17c024a at session start; push executed this session (credentials in ~/.git-credentials, outside repo) |
| Source SHA-256 (loader.c) | changed this session — see evidence/master/appcompat/run-20261015_053015/ + g1_accept run |
| Build status | CLEAN — 0 errors |
| Test status | Regression **12/12**; G1 UPX --version exit 0; **appcompat sweep 7/9 PASS** (t01..t07 incl. BIG C app; t08 = BUG-037 frontier with 0 unresolved imports; t09 = wave-3 API frontier) |
| Current target | G2 (C++ EH acceptance) via TASK-BUG-037 |
| Exact current blocker | BUG-037: unwind interpreter faults on mingw CRT-startup UNWIND_INFO (R9=0x27 slot-offset-as-address; RF ui=0x50458, v1, flags=0x03, 13 slots, frame 0x85) |
| Active task ID | TASK-BUG-037 (READY) |
| Last completed task ID | TASK-BUG-036 + TASK-TLS-001 + TASK-API-007/008 (all CLOSED TESTED) — this session |
| Next task ID | TASK-BUG-037 |
| Blocking dependencies | BUG-038 (G2) depends on BUG-037; t09 depends on TASK-API-009 |
| Evidence links | docs/master/experiments/EXP-040-appcompat-sweep.md; docs/bugs/BUG-032..037.md; evidence/master/appcompat/run-20261015_053015/ |
| Known repository inconsistencies | (1) _O_NOINHERIT(0x0080) mapped to O_APPEND — TASK-API-012; (2) mw_qsort comparator slot single-threaded — TASK-API-010 audit; (3) dispatcher passes post-unwind ctx to handlers (P-B variable, not needed so far); (4) dc-layout generalization audit vs OS canonical DISPATCHER_CONTEXT pending; (5) wchar_t 2↔4 byte passthrough only ASCII-safe (TASK-API-009 note) |

**Key facts established this session (EXP-040):**
- mingw CRT startup imports atexit/_exit/setvbuf/GetCPInfo/_XcptFilter unconditionally;
  an unresolved IAT slot keeps the raw hint/name RVA and `call [iat]` faults at that RVA.
- `__iob_func()` return is indexed with stride 0x30 (mingw FILE struct size) — the loader
  must map that window to real streams (map_crt_stream).
- printf-family must repack MS slots into a SysV va_list via fmt-aware GP/FP classification;
  integer-only formats "work" by accident, float reads fault.
- CRITICAL_SECTION is exactly 40 bytes on x64 and PEs place it flush against other globals —
  never write past byte 40 (sentinel in the SpinCount slot).
- glibc-side callbacks (qsort today, signal/threads next) need ms_abi bridges.
- setjmp/longjmp: loader-owned jmp_buf with the full ms_abi register file; resume at
  rsp = entry_rsp + 8.
- Native TLS: gs:0x58 = ThreadLocalStoragePointer; static TLS = slot 0; __tls_index written
  by the loader; TLS callbacks run with DLL_PROCESS_ATTACH.
- Unresolved-import crash signature: RIP = small RVA (< image size), "(NOT inside loaded PE)".
