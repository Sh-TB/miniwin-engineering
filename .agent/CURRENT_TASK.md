# CURRENT TASK

**Task ID:** TASK-OPENSRC-BATTERY (P1) — 7-Zip + sqlite3 + jq + curl
**Status:** 3 of 4 programs GREEN (2026-10-17, wave W3+W4); curl = frontier

## Achieved (all evidence in evidence/master/)
- **7-Zip 25.01 (7za.exe, x64, official)**: banner/i/a/x/t/l all exit 0;
  archive create -> extract -> SHA-256 round trip **3/3 deterministic**.
  46-import surface closed (BUG-052) incl. OLEAUT32 fixed ordinals
  (2/4/6/7/9/10/149); BUG-054 path normalization. 7z.exe+7z.dll banner OK
  (0 unresolved); full ops need LoadLibraryW = OPENSRC-7ZIP-3 (open).
- **sqlite3 3.50.4 (official)**: --version banner; file-DB create/insert/
  SELECT round trip **3/3 deterministic**. Closed: BUG-055 (strdup registry),
  BUG-056 (GetStdHandle registry handles), BUG-057 (WCTMB fixed-length +
  LoadLibrary api-ms-win-* pseudo-handles + GetModuleHandleExW +
  InitializeCriticalSectionEx), BUG-058 (OVERLAPPED offsets).
- **jq 1.7.1 (official)**: --version + filters **3/3 deterministic**.
- **curl 8.17.0 (official)**: IAT page-crossing fix (BUG-059) + InitOnce
  nesting deadlock fix (BUG-060); remaining = UCRT state-pointer crash,
  OPENSRC-CURL-1 (open, next micro-steps listed).

## Gates kept green after every wave
G1 PIN OK (UPX banner, regression 12/12); sweep t01..t07+t10..t12 PASS
(t08 = BUG-038 G2 frontier, t09 = API-009 frontier — unchanged);
t13_wide_abi PASS; t14_file_read PASS; dispatch 4/4.

## Next micro-tasks
1. TASK-OPENSRC-CURL: OPENSRC-CURL-1 micro-steps (UCRT state layout).
2. TASK-LOADER-DLL: PE DLL loading (unblocks 7z.exe full ops; also
   ffmpeg PNG pipeline uses avcodec DLLs? no — ffmpeg build is static).
3. BUG-053: wprintf/wide-%s output path (7za `i` Formats table mojibake).
4. TASK-TEST-014 remainder: pin the sweep binary set in-repo (gitignore
   policy decision) + add t13/t14 to run_sweep.sh TESTS list.
