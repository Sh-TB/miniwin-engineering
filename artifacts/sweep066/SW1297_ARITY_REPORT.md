# SWEEP-066 — SW-1297 CLOSED (root cause class found): reloc-delta + 226 arity corruptions

Mandate: «ادامه» — the SW-1297 car from SWEEP-065 (notepadpp rc=86 wild jump).

## Bug #1 — base-relocation delta (load_pe32)

The PE32 loader maps the image AT its preferred ImageBase (delta = 0), but the
relocation stage ADDED image_base to every HIGHLOW slot (+0x400000),
corrupting every absolute pointer in the image. Evidence: npp EIP 0x7ef329 =
0x3ef329 + 0x400000 (0x3ef329 = .rdata, data-as-code). Fixed: delta32 =
actual - preferred (0 when mapped at preferred; generic for future fallback
mapping). Honest diagnostic side-fix: cpu32_fatal printed the x64-plane
g_image_base (always NULL in PE32 mode); now prints g_image_base32.

## Bug #2 — the arity table: 226 wrong stdcall pop-bytes (SW-1297b)

g_api32_arity[] (785 entries) cross-validated against wine .spec files
(8,345 names, 23 DLLs, scripts/sweep066_arity_check.py): **226 entries
wrong** — systematic generator errors (1-arg ptr APIs = 16 instead of 4;
cond-vars 48 instead of 12/16; ShowWindow 16→8; BeginPaint/EndPaint 16→8;
EnterCriticalSection 16→4; DebugBreak 16→0; CloseClipboard 16→0; ...).
The npp crash chain, fully decoded via the new eip-hist ring:

    781da5: call [0x7cb388]   (= KERNEL32!EnterCriticalSection, thunk 0xa006f0)
    781dab: pop ebp           (no ebp-frame prologue to resync esp)
    781dac: ret               -> pops garbage 0x7ef318 -> executes .rdata -> 0xee fatal

The old "wrong default self-heals at the next ebp prologue" assumption is
FALSE for modern MSVC tail-shapes (push/call/pop/ret). ALL 226 corrections
applied (5 excluded for manual review: Interlocked*SList x86-ECX family +
RtlUnwind SW-1151 heritage). Evidence: evidence/sweep066/arity_fixes.tsv.

## Diagnostics landed (SW-1297)

1. cpu32_fatal now dumps the 16-entry eip ring (parity with normal exit).
2. crash_handler prints GUEST EIP/ESP/EBP/steps + ring when the HOST faults
   inside the interpreter (mw_cpu32_crash_report).
3. Host-image guard: the flat 32-bit guest VA space overlaps the non-PIE
   host (EXEC at ~0x2000000). m32_* accessors now refuse guest access to the
   host span (one-time log, reads=0, stores dropped).
4. Import-hint bound check in the data-import walk (corrupt hint -> skip).

## Measured results (the tooling flywheel: decode -> fix -> retest in minutes)

| Guest | Before SWEEP-066 | After |
|-------|------------------|-------|
| notepadpp | rc=86 at ~CRT entry (data-as-code 0xee) | **100,518,627 steps** executed (MW GUI pump live), then wild branch @0x693fefee (new frontier) |
| driverview | rc=139 pre-CRT (0 steps) | **794,071 steps**, EBP=0xdead0001 = uxtheme pseudo-handle space escape (new frontier) |
| usbdeview | rc=139 pre-CRT | reaches real .text (EIP 0x4159e8, 27 steps), m32 wild read (si_addr needed — SW-1293b car) |
| 7zr | rc=139 pre-CRT | 74 steps, gate called via valid thunk 0xa000b0, host RIP = guest_va<<32 corruption signature (SW-1293b car) |
| winrar712 | rc=1 + dialog | rc=1 + dialog (unchanged, correct) |

x64 regression battery after rebuild — ALL BASELINE: winmine 82 / sol 38 /
freecell 10 / spider 618 (timer-noise, rc=0) / calc 1 / notepad+wordpad rc=0 /
winrar rc=1 + rendered dialog.

## Verdict

SW-1297 CLOSED: the wild jump was NOT a decode bug — it was stack corruption
from a wrong stdcall pop (EnterCriticalSection arity 16 vs 4) landing the
ret in .rdata, on top of the reloc-delta pointer corruption. The PE32 class
now executes orders of magnitude deeper (100M steps on the biggest guest).
Remaining pre-load crashers decompose into per-binary cars under SW-1293b
(gate host-RIP signature, m32 wild reads, uxtheme pseudo-handle escape).

Files: src/loader/loader.c (reloc delta, arity x226, diagnostics, guards),
scripts/sweep066_arity_check.py (wine cross-validator, reusable),
evidence/sweep066/{arity_fixes.tsv, crashes/}. No force-push.
