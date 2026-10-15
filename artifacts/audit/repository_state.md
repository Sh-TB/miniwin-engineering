# Repository State Audit — Issue #1 Phase 0

Date (UTC): 2026-10-15
Auditor: MiniWin engineering agent (Issue #1 master campaign, Phase 0/1 slice)

## Observed values (actual commands, not assumptions)

| Item | Value |
|---|---|
| Canonical repository URL | https://github.com/Sh-TB/miniwin-engineering.git |
| Owner | Sh-TB |
| Current branch | main |
| Local HEAD (full SHA) | 17c024aab5733b92dd8b1f21e7db444e1fb30d36 |
| Remote HEAD (origin/main) | 17c024aab5733b92dd8b1f21e7db444e1fb30d36 (identical) |
| Ahead / behind | 0 / 0 (in sync before this slice) |
| Previously reported commit 08d94ac62fe9cfdb79d061b57d82ba4bc5c9d12d | EXISTS locally (`git cat-file -t` = commit) |
| Previously reported commit 4039b61 | EXISTS locally |
| Build target | `minwin_loader` (gcc -O2 -no-pie -Wl,-Ttext-segment=0x2000000, from src/loader/loader.c + include/pe.h) |
| Loader binary SHA-256 (pre-slice clean build) | 40646f4b973236af6e9e7fe5ac1917a66495048ad4578e8909583d35073348c3 |
| Loader binary SHA-256 (post-slice, includes WS-D file-I/O fix) | dea39e83d01295a16d112921803a7df0d55b6402ff08344955f096bff1494013 |
| Compiler (host) | gcc 14.2.0-19, Debian x86_64 |
| Cross toolchain (fixtures) | x86_64-w64-mingw32-gcc/g++ 14.2.0-win32, binutils-mingw-w64 2.44 (installed locally under scripts/mingw-debs/root, no root required) |
| Python | 3.12.15 |

## Baseline verification before changes

- Clean build: 0 errors.
- `scripts/g1_accept.sh` @ 2026-10-15T08:38:25Z: binary sha256 40646f4b…, UPX `--version` printed exact banner `upx 4.2.4` + no-warranty line, exit 0, no `[CRASH]` in stderr, regression suite **12 passed / 0 failed**. Evidence: `evidence/master/g1/20261015_083825/`.
- UPX sample `samples/upx_decompressed.exe`: PE32+ / machine 0x8664 / 10 sections / sha256 14e07e294cb8aac61603c95e31b2c364ebd7d0887506a7e156044b262c0d147b.

## Discrepancies with previous reports

1. Historical test identifiers `t08_big_cpp_stl`, `t09`, `t10_float_math`, `t11_abi_deep`, `t12_libc_io` (Issue #1 §11.3) **do not exist** anywhere in the repository (grep across all tracked files: 0 hits). Prior reports referencing them did not leave sources, binaries, or logs. Action taken: recreated as clearly-labelled new fixtures (see results manifest); t09's original semantics are unknown and were NOT fabricated — the replacement is explicitly labelled `t09_recon_c_smoke`.
2. Working tree contained pre-existing modified files in `.agent/`, `docs/`, `checkpoints/` (from earlier sessions). None were reset or discarded. This slice's changes are additive and were committed on top.
3. Issue #1 was found already OPEN on GitHub (created 2026-10-15T08:31:12Z, exactly the "master engineering issue" described by the user mission) — no duplicate issue was created (per Issue #1 §2.2 / §15).
