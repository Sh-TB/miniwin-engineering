# Tooling & Upstream Audit — Issue #1 §12 / §12.3

Date (UTC): 2026-10-15. Scope: local, deterministic, CPU-only tools actually exercised this slice.

## Installed & used (verified in this environment)

| Tool | Version | Used for | Offline | License | Recommendation |
|---|---|---|---|---|---|
| gcc (host) | 14.2.0-19 | Build minwin_loader | Yes | GPL | keep |
| x86_64-w64-mingw32-gcc/g++ | 14.2.0-19+27+b1 (win32) | Build real PE32+ x64 C/C++ fixtures (t08–t12) | Yes (debs extracted locally, no root) | GPL | **ADOPT** — key enabler; produces genuine MSVCRT-CRT-coupled binaries |
| binutils-mingw-w64 | 2.44 | link/objdump of fixtures | Yes | GPL | keep |
| i686-w64-mingw32-gcc | 14.2.0 | Build PE32 x86 fixture (x86 classification test) | Yes | GPL | keep for x86 baselines |
| pefile / capstone (python) | present | PE metadata inspection, disassembly | Yes | MIT/BSD | keep |
| upx (target binary) | 4.2.4 win64 | end-to-end packed execution target | n/a (sample) | GPL | keep as corpus |

## How to reproduce the local mingw install (no sudo)

```bash
cd scripts/mingw-debs
apt-get download binutils-mingw-w64-x86_64 mingw-w64-common mingw-w64-x86-64-dev \
                 gcc-mingw-w64-base gcc-mingw-w64-x86-64-win32 g++-mingw-w64-x86-64-win32
for d in *.deb; do dpkg-deb -x "$d" root/; done
ln -s root/usr/bin/*-win32 symlinks as needed   # e.g. x86_64-w64-mingw32-gcc -> …-gcc-win32
export PATH=$PWD/root/usr/bin:$PATH
```
Debian `alternatives` symlinks are not created by dpkg-deb -x; create `x86_64-w64-mingw32-gcc` → `x86_64-w64-mingw32-gcc-win32` etc. manually (already done in this workspace).

## Upstream references consulted (semantic, per Issue #1 §12.1)

| Reference | What it settled |
|---|---|
| mingw-w64 headers (local: `stdio.h` `struct _iobuf`) | Exact MSVCRT FILE layout (`_ptr,_cnt,_base,_flag,_file,_charbuf,_bufsiz,_tmpfname`) → drove the fopen ABI fix |
| Issue #1 §7 links (MS x64 EH docs, LLVM Win64EH.h) | Pending for the BUG-038 catch-selector slice (next) |
| Wine/ReactOS ntdll RtlUnwindEx sources | Consulted in prior BUG-026/027 slices (see docs/bugs, knowledge-transfer/) |

## Not used / not needed now

- Native Windows oracle: unavailable in this environment; documented as an environmental prerequisite, not silently skipped.
- Fuzzers/differential testers: PENDING (Phase 3 material; no blocker this slice).
