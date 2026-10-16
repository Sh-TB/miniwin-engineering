# EXP-041 — BUG-037: the unwind-opcode spec layer was wrong (three stacked defects), fix + expanded program sweep

**Date:** 2026-10-15 (CONTINUE #7)
**Binary:** minwin_loader sha256 70289790af5d69c11846bf5f5f564fc2066c788484e399eb2edba9df7163168c (final this session)
**Toolchain re-established:** llvm-mingw 20240917 (clang 19.1.0), downloaded from
mstorsjo/llvm-mingw releases (ucrt tarball, 87,950,564 bytes) — the environment
reset wiped the previous copy.

## 0. Summary

BUG-037 was registered as "the opcode interpreter faults on the mingw
CRT-startup UNWIND_INFO". Investigation found the fault was only the
symptom: the loader's entire UNWIND_CODE opcode layer used a deprecated
pre-release numbering, misread CountOfCodes (codes vs slots), and swapped
the frame-register nibbles. After fixing all three (plus FP-frame resume
semantics), t08 no longer faults in the interpreter at all; the walk
completes and resumes. The remaining t08 failure is a NEW distinct bug
(BUG-038, landing-pad placeholder). As part of the same session the toolchain
was re-established and three NEW test programs (t10/t11/t12) were authored,
built and brought to PASS via a small UCRT API wave (TASK-API-011/012).
Sweep: 7/9 → **10/12 PASS**. Regression 12/12 + G1 gate held at every step.

## 1. The three root causes (with the evidence that pinned each)

### 1.1 Opcode numbering was the deprecated pre-release set
- File truth: t08's CRT UI (RVA 0x50458, file 0x4ea58) head word `0x6822` =
  op 8, info 6, END-offset 0x22 with a ×16-scaled operand — semantically
  SAVE_XMM128 xmm6 of `movaps [rbp+0xb0],xmm6` (fp-anchored offset 0x130/16).
- Loader truth: `include/pe.h` had `UWOP_SAVE_XMM128 6`, `UWOP_PUSH_MACHFRAME 8`
  → the head decoded as PUSH_MACHFRAME: `new_rsp += 40; rip = *(new_rsp+16);
  new_rsp = *(new_rsp+8)` — junk RSP (traced 0x1f → +8 = 0x27 → SIGSEGV).
- Authoritative numbering (4 independent sources):
  - Microsoft "Exception handling x64" doc: `UWOP_SAVE_XMM128 (8) 2 nodes`,
    `UWOP_SAVE_XMM128_FAR (9) 3 nodes`, `UWOP_PUSH_MACHFRAME (10) 1 node`.
  - LLVM 19 Win64EH.h: `UOP_Epilog=6, UOP_SpareCode=7, UOP_SaveXMM128=8,
    UOP_SaveXMM128Big=9, UOP_PushMachFrame=10` — and MCWin64EH.cpp emits
    `b2 = inst.Operation & 0x0F` verbatim.
  - .NET diagnostics win64unwind.h: same order, slot table with
    "UWOP_SPARE_CODE // previously 64-bit UWOP_SAVE_XMM_FAR".
  - ReactOS sdk/lib/rtl/amd64/unwind.c: same numbers with the migration
    comment ("UWOP_EPILOG 6 // previously UWOP_SAVE_XMM").
- The old 6/7/8 set never shipped in retail Windows — the loader inherited a
  fabricated/misremembered table. (Resolves the "llvm-mingw binaries are
  broken on Windows?" paradox — they are not.)

### 1.2 CountOfCodes counts SLOTS, not code words
- MS doc: each opcode occupies "2 or 3 nodes" — operand slots are UNWIND_CODEs.
- ReactOS: `while (i < CountOfCodes) { ... i += UnwindOpSlots(...); }` — the
  bound is on the slot cursor.
- t08 CRT frame: 13 slots = [SAVE_XMM128(2) SET_FPREG(1) ALLOC_LARGE(2)
  8×PUSH(8)] + 1 pad slot; count byte = 13 ✓.
- Reproduced with a standalone C harness (`scripts/bug037_alloc_harness.c`)
  running the loader's exact loop: `alloc_total = 0xff0191` — the loop ran 13
  CODE iterations (slot cursor reached 15), consuming the pad slot as a push
  and the handler RVA 0x131b0 as ALLOC_LARGE u32 (0xff0001). Correct total =
  **0x188** = 0x148 (sub rsp) + 0x40 (8 pushes).
- Fix: all five decode loops bound on the slot cursor; skip path advances by
  the full slot size via the new shared `seh_unwind_op_slots()`.

### 1.3 Frame-register nibbles swapped
- winnt.h bitfields are little-endian: `UCHAR FrameRegister:4; UCHAR
  FrameOffset:4;` → register = LOW nibble.
- mingw `lea rbp,[rsp+0x80]` → byte 0x85 = RBP(5) low / off(8) high.
  LLVM's emitter `(reg & 0x0F) | (offset & 0xF0)` writes exactly this and is
  correct; our `ui_base[3] >> 4` read was wrong.
- Consequence: FP establisher would have been computed into R8's ctx slot
  with offset 0x50 instead of RBP/0x80.
- Fix: `frame_reg = ui[3] & 0xF; frame_off = ui[3] >> 4` at all 3 sites.
- Additionally SET_FPREG now re-anchors the simulated RSP from the LIVE FP
  register minus FrameOffset*16 (ReactOS semantics: `Context->Rsp =
  GetReg(Context, Reg) - FrameOffset*16`), and mw_RtlUnwindEx computes the
  FP-frame landing-pad RSP as `tgt_frame - FrameOffset*16` (body RSP).
  For t08's CRT frame: FP=RBP=entry-0x108 → body RSP = entry-0x188 = the
  true post-prologue RSP ✓ (the old `tgt_frame - alloc_total` would give
  entry-0x290).

## 2. Verification chain

| Check | Result |
|---|---|
| Golden vector decode (scripts/bug037_ref_decode.py) | 13 slots, descending END-offsets, handler 0x131b0 @+32 ✓ |
| alloc harness | 0xff0191 (bug) → 0x188 (fix) |
| Regression suite | **12/12** at every build |
| UPX G1 gate | exit 0, exact banner, 3 pins (063340, 064317, 073727) |
| t08 walk | completes; alloc_total=0x188; FP body RSP correct; resume attempted |
| t08 remaining failure | SIGILL at 0x1c85 — NEW bug BUG-038 (placeholder TargetIp), NOT the interpreter |
| Sweep | **10/12 PASS** (see §4) |

## 3. Toolchain re-establishment + new test programs (t10/t11/t12)

The environment reset removed /home/z/my-project/tools. llvm-mingw 20240917
was re-downloaded and verified (clang 19.1.0). Three new programs were
authored with fresh coverage areas, built `-O1 -lucrt`:

| Test | Size | New coverage | Result |
|---|---|---|---|
| t10_float_math | small | printf %f/%e/%g, snprintf, sscanf, doubles through varargs, math.h | **PASS** (exit 0) |
| t11_abi_deep | small | structs by value (8/16/>16B), 7-arg mixes, recursion, fn-pointer tables, stdarg, qsort bridge | **PASS** (exit 0) |
| t12_libc_io | small | heap churn, FILE*-level IO (fopen/fread/fgets/fgetc/rewind/remove), qsort strings, time/gmtime/strftime, atexit LIFO | **PASS** (exit 0, exit-handler PASS) |

The new exes import the UCRT api-ms-win-crt-* apiset surface. Bringing them
up exposed and fixed two API gaps (TASK-API-011/012):

- **TASK-API-011 (UCRT startup + unified stdio):** `__acrt_iob_func`,
  `_configure_narrow_argv`, `_initialize_narrow_environment`, `_set_new_mode`,
  `_set_invalid_parameter_handler`, `signal` (stub), `_tzset`,
  `_set_app_type` (apiset alias), and the REAL bridges
  `__stdio_common_vfprintf/vsprintf` (via the proven fmt-aware MS→SysV va_list
  repack; vsprintf through fmemopen), `__stdio_common_vsscanf` (GP-only
  repack → glibc vsscanf), `__stdio_common_vfwprintf` (loud stub).
- **TASK-API-012 (FILE-level stdio + UCRT __p_* functions):** fopen/fclose/
  fread/fgets/fgetc/rewind/remove/strftime as glibc bridges; and the discovery
  that ALL UCRT `__p__*` exports are FUNCTIONS returning &var — proven by
  three successive crash signatures (0x2039038 __p__fmode, 0x2039034
  __p__commode, 0x2039050 __p___argc — the IAT value used as a call target).
- **map_crt_stream fallback:** the "unknown stream → stdout" fallback silently
  consumed fmemopen/fopen output; unknown pointers now pass through verbatim
  (synthetic iob-window pointers are still remapped by the in-window branch).
  Reproduced with /tmp/snapt.exe (r=5 buf=[] → r=5 buf=[3.142]).

Honest notes:
- `__stdio_common_vfwprintf` (wide) is a returning-stub — no test exercises it.
- t10's sscanf repack assumes the uniform 8-byte MS slot model (same model as
  the printf path, already validated by t01-t07).
- t12's gmtime/strftime go through the existing _gmtime64 + a new strftime
  bridge; tz data is UTC-stubbed (g_tzname/g_timezone_val).

## 4. Final sweep (evidence/master/appcompat/run-20261015_073729)

```
t01_hello          PASS      t07_big_c_app      PASS (BIG C)
t02_math           PASS      t08_big_cpp_stl    CRASH (BUG-038 frontier; 0 unresolved)
t03_mem            PASS      t09_big_cpp_iostream CRASH (wave-3 API frontier)
t04_string         PASS      t10_float_math     PASS (NEW)
t05_file           PASS      t11_abi_deep       PASS (NEW)
t06_setjmp         PASS      t12_libc_io        PASS (NEW)
```

Progress vs session start: 7/9 → 10/12; t08's failure point moved from
"interpreter faults mid-walk" (no resume) to "resumes with correct RSP/FP and
dies at the landing-pad placeholder" — one bounded fix (BUG-038 target-frame
delivery) from the catch block running.

## 5. Registered follow-ups

- **BUG-038** (P0): OS-faithful target-frame delivery in mw_RtlUnwindEx
  (barrier-cache LP installation). Full design in docs/bugs/BUG-038.md.
- TASK-BUG-039: PUSH_MACHFRAME arithmetic verification vs ReactOS.
- TASK-BUG-040: v2 UNWIND_INFO/EPILOG synthetic golden tests.
- TASK-API-013/014/015 + t09 wave-3 list (see backlog).
- TASK-TEST-006/007: golden-vector + UI-scanner regression automation.
