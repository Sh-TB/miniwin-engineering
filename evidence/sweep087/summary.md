# SWEEP-087 — the installed-program train

User protocol: run the INSTALLED programs (not just portables) + one new game + one new program.

## Engine changes (generic) — src/loader/loader.c

- **SW-1410 — Microsoft x64 ABI on every fake-COM vtable member** (16 fns):
  `SHGetMalloc` IMalloc (9 members), `SHGetDesktopFolder` IShellFolder (7),
  `CoGetMalloc` OLE allocator (8). These were plain SysV functions exposed to
  the guest: MS x64 passes args in RCX/RDX/R8/R9, SysV reads RDI/RSI —
  `IMalloc::Free(p)` executed `free(garbage)` and crashed inside libc reading
  the chunk header (installed WinRAR 7.12's first Release/Free round-trip).
- **SW-1411 — CharLowerW/CharLowerA/CharUpperA low-word contract**: real
  WinAPI treats HIWORD==0 as a single CHARACTER, not a pointer. Installed
  WinRAR passes `CharLowerW(1)` during init — dereferencing it was SIGSEGV @0x1.
  (CharUpperW already honored the contract.)
- **SW-1407b — runner `sandbox_tree=<dir>`**: a whole Windows-style tree is
  staged into the sandbox before the run; installed-program plugins run their
  exe from its own install dir with every sibling file present (a real C:
  layout), otherwise the installer path (`<sandbox>/Installers/`) is used.

## Results

| plugin | verdict | notes |
|---|---|---|
| **winrarinstd** (NEW program — WinRAR 7.12 INSTALLED by the official installer in SWEEP-086; 28-file install tree staged, exe runs from its own dir) | FRONTIER_SW-1412 crash=1 ×3 | Progress ladder this train: (1) IMalloc ABI crash → SW-1410 fixed; (2) CharLowerW(1) crash → SW-1411 fixed; (3) now: SHGetMalloc OK + IMalloc round-trips OK + child window message loop (`SendMessageW` to 0x30002) + `SHGetFileInfoW` probes → **heap-corruption crash inside libc** (RIP in libc, malloc metadata poisoned: RCX=0xfffffffffc000000). Queued SW-1412 (heap-guard decode). |
| **pattern** (NEW game — Simon Tatham Pattern/Nonogram) | **PASS_INPUT_STATE_CHANGE first contact** | rc=0 ×3, 6 frames / 3 unique states. |

## Gates

- Corpus parity `scripts/sweep087_corpus.sh`: 21/21 (7 guests × 3, rc=0/crash=0/fatal=0).
- G1 PIN **12/12** — `evidence/master/g1/20261027_055118`, binary `62dea8e0…`.

## Next wagons

- SW-1412: installed-WinRAR heap corruption decode (guard run: which guest write
  poisons malloc metadata between SHGetFileInfoW and the child SendMessageW loop).
- SW-1409: NSIS LZMA extraction desync (nppsetup, dist=190492 position story).
- SW-1401: idman CreateProcessW child exec; SW-1400 winrar SFX interactive modal.
