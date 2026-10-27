# SWEEP-086 — the installer-ground-truth train

User mandate: «برنامه‌های نصبی — فایل‌های ریجیستری و پوشه‌ها باید درست در مسیرهای
خودشان ساخته شوند» + the standing protocol (one new game + one new program per train).

## Engine changes (all generic, zero app special-casing) — src/loader/loader.c

- **SW-1402** on-disk registry tree mirror (`MINWIN_REG_TREE=<dir>`): at exit the
  live hive is exported as a real filesystem tree — key paths become directories
  (`HKLM\SOFTWARE\X` → `<tree>/HKLM/SOFTWARE/X`), values become files `@<name>`
  (`@(Default)` for unnamed), rendered by type (REG_SZ text / dword:0x… / hex),
  plus `_REG_MANIFEST.tsv` ledger (regpath, value, type, size, hostfile).
  Registry files are now literally created in their own paths.
- **SW-1402b** installer-grade hive capacity: MAXKEY 64→256, MAXVAL 16→24,
  PATHN 160→256, DATA 512→1024 (snapshot format stays load-compatible).
- **SW-1403** guest install-log (`MINWIN_INSTALL_LOG=<file>`): every guest FS
  mutation (CreateDirectory, create-with-write CreateFile, CopyFile W/2,
  MoveFileEx, DeleteFile, RemoveDirectory) appends
  `op<TAB>guest-path<TAB>host-path<TAB>result`. Read-only opens never log.
- **SW-1404** runner sandbox mode: manifest `sandbox=yes` → guest CWD =
  `evidence/<sweep>/<name>/sandbox` (SW-1234 drive-root = loader CWD), with
  install_log + regtree + snapshot + MINWIN_WINTEMP=1 captured inside.
  Non-sandbox plugins are byte-identical.
- **SW-1405** booted-system truth: the Windows directory skeleton seeding
  (SWEEP-058) is now shared by `CreateDirectoryW` too, and `GetTempPathW` gains
  the `MINWIN_WINTEMP=1` world mode returning `C:\Windows\Temp\` (default stays
  POSIX TMPDIR — corpus untouched). IDM's `IDM_Setup_Temp` now self-creates.
- **SW-1407** module self-path round-trip: `win32_module_path` expresses the exe
  relative to the drive root when it lives under it; NSIS/Inno re-open their own
  image (`CreateFile(GetModuleFileName())`) to read the embedded payload — the
  old `C:home\z\...` form could never re-normalize. Runner-side: sandboxed
  installers are copied into `<sandbox>/Installers/` first.
- **SW-1408** PE32 arity: `MessageBoxIndirectW/A = 4` (one pointer arg). The
  arity-0 under-pop left 16 bytes on the guest stack; NSIS 8.9.8.1's first
  message box returned into the thunk region on a drifted RSP (SIGSEGV).
- `mw_ExitProcess` now flushes the registry snapshot + regtree explicitly
  (it is `_exit`-based; loader atexit handlers never ran).

## Results

| plugin | verdict | ground truth |
|---|---|---|
| **winrar** (official 7.12 SFX installer) | **INSTALL_OK** rc=0 ×3 | **28 files / 9,564,664 B installed into `sandbox/WinRAR/`** — WinRAR.exe, Rar.exe, UnRAR.exe, Uninstall.exe, RarExt.dll, 7zxa.dll, 6 SFX modules, docs; 93 install-log ops, every write result=1. The *installed* WinRAR.exe now exists on the MiniWin "C: drive". |
| **idman** (official 6.42 build 2 installer) | EXIT rc=0 ×3 | `C:\Windows\Temp\IDM_Setup_Temp` **created** (historical blocker, SW-1405); extraction stage reached; child-exec frontier (SW-1401) unchanged. |
| **nppsetup** (NEW — official 8.9.8.1 NSIS installer, sha256 26f3bcce…) | FRONTIER_SW-1409 crash=1 ×3 | Deep run: 69.7M steps, 1093 API calls, self-reopen + temp stage (`ns*.tmp`) + `System.dll` staged + payload `ReadFile` loop (16 KiB chunks, 8 MiB LZMA window) → SIGSEGV inside LZMA output write (si_addr 0x73df300f). Wizard pre-render frontier decoded and queued (SW-1409). |
| **lightup** (NEW game — Simon Tatham Light Up) | **PASS_INPUT_STATE_CHANGE first contact** | rc=0 ×3, 6 frames / 3 unique states, official zip sha a5b00f63 re-verified. |
| npp64 (portable, re-verify) | PASS_LAUNCH_RENDER rc=0 | 1 frame, zero regressions. |
| mines (re-verify) | PASS_INPUT_STATE_CHANGE | 6 frames / 3 uniq, zero regressions. |

## Gates

- Corpus parity `scripts/sweep086_corpus.sh`: 7 guests × 3 runs = 21/21
  (rc=0, crash=0, fatal=0) — `evidence/sweep086/corpus_parity.tsv`.
- G1 PIN **12/12** — `evidence/master/g1/20261027_053105`, binary
  `b24475ff2ef3a6e6d176b3caa9ea60c9c1ea237a6e16efd970a091ca3160b2f3`.

## Honest-classification note

`EXIT rc=0 frames=0` rows are **not** render claims. WinRAR's row is
INSTALL_OK because the install-log + filesystem tree prove the files;
IDM/nppsetup rows are pre-wizard frontiers with their stage of arrival named.

## Next wagons

- SW-1409: NSIS LZMA extraction desync (nppsetup) — likely a SetFilePointer/
  ReadFile position or chunk-size decode defect at dist=190492.
- SW-1401: idman CreateProcessW child exec (the extracted stage child).
- SW-1400: winrar SFX *interactive* modal (the silent install path works; the
  wizard-dialog path still to render).
- Run the INSTALLED `sandbox/WinRAR/WinRAR.exe` (user's third goal: the
  installed program, not the portable one).
