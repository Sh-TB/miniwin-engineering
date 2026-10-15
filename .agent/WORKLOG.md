# WORKLOG — MiniWin (append-only)

---
Task ID: TASK-ISS1-P1
Agent: Super Z (engineering agent)
Task: Read GitHub Issue #1 line-by-line; execute its Phase-1 6-target plan (UPX + t08 + t09 + t10 + t11 + t12); fix the first general root cause found; keep evidence; report per §19.

Work Log:
- Fetched https://github.com/Sh-TB/miniwin-engineering/issues/1 (anonymous HTML → cleaned text, 1095 lines) and read it in full.
- Phase 0 audit: HEAD 17c024aab5733b92dd8b1f21e7db444e1fb30d36 = origin/main (0/0), historical commits 08d94ac + 4039b61 exist; wrote artifacts/audit/repository_state.md.
- Baseline: clean build sha256 40646f4b…; scripts/g1_accept.sh → G1 PIN OK (UPX banner exact, exit 0, regression 12/12); UPX 3× repeat deterministic (stdout sha 1bfb775b…).
- Phase 1 finding: t08–t12 fixtures MISSING from repo (0 grep hits). Installed mingw-w64 locally (dpkg-deb extraction, no sudo) and recreated all 5 as real PE32+ x64 C/C++ fixtures in tests/corpus/ with host-verified expected values (t11 probe value 1761356 computed on host; t12/t11 arithmetic bugs in my first drafts found and fixed BEFORE blaming the runtime).
- First run: t12 = SEGV (9 unresolved imports: fopen/fclose/fread/fseek/feof/fgetc/remove/_strtoi64/_strtoui64). t08 = exit 1 (159/255). t09/t10/t11 = PASS (t11 after fixture fix).
- WS-D slice in src/loader/loader.c (forbidden zones untouched): implemented fopen/fclose/fread/fseek/ftell/feof/fgetc/remove/_strtoi64/_strtoui64; upgraded fwrite/fputs/fputc/fflush/getc/fprintf/vfprintf to delegate; then TWO deeper semantic fixes driven by live traces:
  (1) FILE* must be MSVCRT `struct _iobuf` layout (guest CRT introspects _file for lock indexing and _flag bits; opaque handle caused heap corruption via garbage CRITICAL_SECTION);
  (2) real ungetc via _charbuf (stub lost pushback → every fscanf token shifted one char: d=42 word=[xt] dbl=0.75 hex=15 → after fix d=-42 word=[txt] dbl=3.75 hex=255).
- Added errno sync into g_errno_storage on fopen/remove failure paths.
- Final acceptance: 6 targets × 3 runs → upx/t09/t10/t11/t12 exit 0 ×3 (byte-identical stdout each), t08 exit 1 ×3 (159/255, deterministic). Regression 12/12. G1 green.
- x86 classification (Issue §4): built real PE32/i386 binary with i686-mingw → loader refuses explicitly `[ERROR] Not a PE32+ file (magic=0x10b)`, exit 1 — recorded as UNSUPPORTED, not a runtime defect.
- Artifacts: artifacts/audit/{repository_state,issue_reconciliation}.md, artifacts/research/tooling_and_upstream_audit.md, artifacts/results/issue1_phase1_manifest.md; .agent/{EVIDENCE_INDEX,NEXT_ACTION,CURRENT_TASK,SESSION_HANDOFF}.md updated.

Stage Summary:
- 5/6 Phase-1 targets PASS deterministically; 1 PARTIAL with a precisely classified open root (BUG-038 lineage: multi-clause trivial-type catch selector + stack clobber).
- General fix landed (WS-D stdio file I/O family), proven by t12 SEGV→PASS and unchanged 12/12 regression.
- All evidence real: commands, exit codes, SHA-256s, traces under evidence/master/ and artifacts/.
