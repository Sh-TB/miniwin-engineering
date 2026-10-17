# NEXT ACTION (single, most-restrictive next step)

**Verified state (2026-10-17, HEAD d9eaf4f+wave12):** regression 12/12; G1 OK;
7-Zip round trip + BUG-054b 3/3 + zstd (LADDER-008) verified 3/3.

**NEXT = TASK-MSVC-001..003 (P0, per ADR-012):** census script dumping the CRT
version demanded by every ladder binary (artifacts/census/crt-versions.tsv),
vcruntime140 export diff, ucrtbase alias map. Then the T1 GnuWin32 wave
(LADDER-011..020) exercises the msvcrt.dll gate.

Parallel quick wins (P1):
- BUG-061 follow-up: audit _wstat64i32/_stati64 for the same layout/overflow class.
- LADDER-009/010 (lz4, gzip): same mingw profile as zstd — expect quick greens.
- ANDROID-001: Path A runbook (Termux+proot-distro+qemu-user) — zero-porting shortcut.
- TASK-OPENSRC-1 curl UCRT frontier remains the deep item (OPENSRC-CURL-1).

## CONTINUE #11 update (2026-10-17)
- BUG-062 CLOSED (TESTED) — real guest threads; LADDER-008 re-VERIFIED on
  >0x60000 payloads (previous wave used smaller inputs).
- Next micro-step (P0): LADDER-009/010 (lz4, gzip) via scripts/ladder_run.sh —
  same mingw/msvcrt profile as zstd, expect fast greens; then MSVC-001 census
  script (artifacts/census/crt-versions.tsv), then T1 GnuWin32 wave.
- Keep in mind: mw_CreateThreadpoolWork still stubbed; TLS shared across guest
  threads (documented in BUG-062) — revisit if a threaded rung shows TLS corruption.
