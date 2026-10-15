# NEXT ACTION (single, most-restrictive next step)

**Root cause:** BUG-039 CLOSED TESTED (2026-10-15) — two general roots fixed:
(1) UWOP_PUSH_NONVOL replay restored from the wrong slot (read-after-increment;
ReactOS PopReg = read-then-increment); (2) landing-pad resume used the
search-walk context whose target-frame codes had clobbered throw-time
registers (now restored from the per-frame pre-unwind snapshot).

**Verified state:** t08_big_cpp_stl 255/255 exit 0 (×3 deterministic);
t08_probe 3/3 exit 0 (×3); regression 12/12; UPX G1 exit 0; appcompat sweep
11/12 PASS (t09 TIMEOUT = known TASK-API-009 iostream frontier).

**NEXT (P1):** TASK-API-009 — t09_big_cpp_iostream TIMEOUT frontier. First
step: run t09 with a 20s slice and read the last trace events before the
hang (evidence/master/appcompat/run-20261015_102219/t09*.stderr + the
miniwin-results trace) to identify the first unresolved/blocked API
(iostream family: locale/ios_base init, __stdio_common_vswprintf waves,
basic_filebuf). Reproduce command:
`timeout 20 ./minwin_loader tests/appcompat/t09_big_cpp_iostream.exe`

**Also open (P2):** BUG-038 re-scoped to the llvm-mingw/MS-ER family
(target-frame delivery, ExceptionFlags 0x2|0x4, Info[1]=target_frame) —
needed only when an MSVC/llvm-mingw C++ target is added to the corpus.
