# Engineering State — CANONICAL COMPACT SNAPSHOT

**Updated:** 2026-10-17 (CONTINUE #9 — OPENSRC battery wave: **7-Zip 25.01
full round trip 3x, sqlite3 3.50.4 file-DB round trip 3x, jq 1.7.1 filters
3x** — BUG-049..060 closed; BUG-053/OPENSRC-7ZIP-3/OPENSRC-CURL-1 open;
backlog expanded). Prior snapshot (CONTINUE #7): **BUG-037 CLOSED TESTED** — 3 spec fixes; sweep **10/12 PASS**
(+t10/t11/t12 NEW); UCRT wave TASK-API-011/012 CLOSED TESTED; **BUG-038 registered** (next);
EXP-041; backlog 117 tasks). Supersedes prior snapshots.

| Field | Value |
|---|---|
| Current HEAD (local) | CONTINUE #7 commit (see git log) |
| Origin/main | 17c024a at session start; push executed this session (credentials in ~/.git-credentials, outside repo) |
| Source SHA-256 (loader.c) | changed this session — see evidence/master/appcompat/run-20261015_053015/ + g1_accept run |
| Build status | CLEAN — 0 errors |
| Test status | Regression **12/12**; G1 UPX --version exit 0; **appcompat sweep 10/12 PASS** (t01..t07 + NEW t10/t11/t12; t08 = BUG-038 frontier, walk completes, resumes; t09 = wave-3 API frontier) |
| Current target | G2 (C++ EH acceptance) via TASK-BUG-038 |
| Exact current blocker | BUG-038: llvm-mingw libunwind passes ControlPc as RtlUnwindEx TargetIp PLACEHOLDER; real LP (0x1c9d) must be installed via target-frame delivery (ExceptionFlags 0x2|0x4, barrier cache) — docs/bugs/BUG-038.md |
| Active task ID | TASK-BUG-038 (READY) |
| Last completed task ID | TASK-BUG-037 + TASK-API-011 + TASK-API-012 (all CLOSED TESTED) — CONTINUE #7 |
| Next task ID | TASK-BUG-038 |
| Blocking dependencies | t08/G2 = TASK-BUG-038; t09 depends on TASK-API-009 |
| Evidence links | docs/master/experiments/EXP-041-bug037-unwind-opcode-spec.md; docs/bugs/BUG-037.md (CLOSED), BUG-038.md (OPEN); evidence/master/appcompat/run-20261015_073729/; evidence/master/g1/20261015_073727/ |
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

**Key facts established in CONTINUE #7 (EXP-041):**
- Correct x64 unwind opcode numbering (current MS doc = LLVM = .NET = ReactOS):
  6=EPILOG(v2), 7=SPARE, 8=SAVE_XMM128, 9=SAVE_XMM128_FAR, 10=PUSH_MACHFRAME.
  The old 6/7/8 table in pe.h never shipped in any retail Windows.
- CountOfCodes counts SLOTS (code words + operand slots); loop bounds must be
  the slot cursor (ReactOS advances by UnwindOpSlots). t08 CRT frame: 13 slots.
- UNWIND_INFO byte 3: FrameRegister = LOW nibble, FrameOffset = HIGH nibble
  (little-endian C bitfields). mingw lea rbp,[rsp+0x80] => 0x85.
- FP-frame landing-pad RSP = tgt_frame - FrameOffset*16 (body RSP), NOT
  tgt_frame - alloc_total.
- ALL UCRT __p__* exports are FUNCTIONS returning &var (crash signatures
  0x2039038/0x2039034/0x2039050 proved fmode/commode/argc call-style).
- map_crt_stream must PASS THROUGH unknown pointers (fmemopen/fopen-backed
  real FILE*); the old unknown->stdout fallback ate buffered output.
- llvm-mingw libunwind: RtlUnwindEx TargetIp from the personality = ControlPc
  PLACEHOLDER; real LP delivered at target frame via barrier cache (BUG-038).
