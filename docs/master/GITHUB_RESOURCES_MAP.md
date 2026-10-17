# GitHub Resources Map — what to reuse, what to read, what to avoid

**Status:** ACTIVE. **Created:** 2026-10-17. **Driver:** user directive — use
existing open-source code/tools to save hours of coding; take patterns,
inspiration and guidance; **but stay inside our own standard** (ADR discipline,
evidence harness, irreversible status labels, no license contamination).

---

## 1. The rule (our standard, applied to borrowing)

1. **License first**: code may be *copied* only from MIT / Apache-2 / BSD sources,
   with attribution recorded in `docs/master/BORROWED_CODE.md` (new task LIC-002).
   LGPL/GPL sources are **read-and-learn only** (design reading), unless the whole
   project ever re-licenses — a decision that belongs to ADR-014.
2. **Evidence always**: anything borrowed enters through a BUG-*/API-* task with
   the usual 3× test discipline. No blind ports.
3. **Tools are free**: using external *tools* (pefile, capstone, objdump, gdb,
   wine as a test oracle) has no license coupling with our code.
4. **Every borrow must replace hours**: if reading is cheaper than porting, read.

## 2. Projects, licenses, and the concrete take-away

| Project | License | What we COPY | What we READ for design | Where it plugs in |
|---|---|---|---|---|
| **decompals/wibo** | MIT | msvcrt export implementations (pure-C semantics: ctype, mbcs, exit handling, env), PE loader structure for 32-bit later | its "implement exactly what compilers import" philosophy | MSVC_RUNTIME_PLAN Mode 1/3; x86 (Path B of mission) feasibility |
| **taviso/loadlibrary** | Apache-2.0 | peloader layout ideas; its C++ EH shim comments (documented protocol quirks) | API-forcing technique (generate stubs from real DLL export tables) | loader.c architecture reviews; RES-005 report |
| **evmar/retrowin32** | (check per-file) | — | emulate-vs-load trade-offs; win32 API coverage tables | long-term architecture notes |
| **pe-parse / LIEF** | MIT | nothing vendored (tools exist) | PE edge cases (odd section tables, rich header) | census scripts RES-001..003 |
| **Wine** | LGPL-2.1 | **nothing (license)** | unwind.c/signal_x86_64.c semantics; msvcp/msvcrt per-export behavior; dlls/ucrtbase tests as *specification* | ADR-008 lineage; MSVC plan Mode 1 |
| **ReactOS** | GPL/LGPL | **nothing** | rtl/amd64/unwind.c walk logic; their Jan-2026 Wine-10.0 msvcrt sync proves which exports matter | BUG-027 lineage verification |
| **msvc-wine (mstorsjo)** | MIT (scripts) | the **downloader pattern**: fetch VC redist/SDK from MS servers after license acceptance, cabextract extraction | VS-version → DLL mapping tables | MSVC plan Mode 2 acquisition script (MSVC-020) |
| **winetricks** | LGPL (script) | **nothing (license)**; but the *verb list* is a public catalog of what old apps need | verbs: vcrun6..2022, msvcr60/71/80/90/100, VB runtimes, comctl | ladder CRT priority matrix |
| **box64** | BSD-3 | nothing yet | dynarec architecture notes (for future Path B engine) | ANDROID Path B2/B3 |
| **FEX-Emu** | MIT | nothing yet | engine interface shape (how Hangover drives it) | ANDROID Path B2 |
| **Hangover** | LGPL | **nothing** | the **loader/engine split** — proof that "PE loader on ARM + external x86 engine" is a production architecture | ADR-013 Path B |
| **Winlator / Mobox** | LGPL / open | **nothing** | prereq lists, container layout, known-broken app lists (free compatibility intelligence!) | ANDROID Path C oracle; ladder triage |
| **unicorn / qiling** | GPL-2 | **nothing, no embedding** | hook/memory-model ideas | ANDROID note; future GPL decision |
| **otvdm/winevdm** | GPL | **nothing** | how 16-bit VMM seams work | LADDER-094 writeup |
| **encounter/winedll** | LGPL (Wine builds) | **nothing vendored**; optional *user-supplied external* prebuilts are the wibo precedent if we ever need a stopgap | — | MSVC plan Mode 2 fallback (kept out of repo) |

## 3. Tooling we already use (keep, document)

pefile + capstone (census), objdump/gdb (forensics), Wine (behavioral oracle for
RtlUnwindEx/msvcrt semantics), GitHub API (issue/commit mining), MS Learn docs
(PE/COFF + x64 ABI = normative), ReactOS/wine sources (read-only spec).

## 4. Concrete savings realized (fill as we go)

- 2026-10-17: msvc-wine's download pattern adopted for MSVC plan Mode 2 → saves
  the entire "how do we legally obtain ucrtbase/vcruntime140" research thread.
- 2026-10-17: Winlator/Mobox known-issues lists adopted as ladder triage oracle →
  saves per-app debugging guesses on Android.
- wibo MIT msvcrt implementations identified as the only copy-safe CRT source →
  saves reimplementing ~40 boring ctype/locale exports when T1 hits.
