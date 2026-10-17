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
