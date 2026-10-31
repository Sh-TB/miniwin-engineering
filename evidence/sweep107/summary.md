# SWEEP-107 — registry caps + seed semantics + guest backtrace (WinRAR GUI / Notepad++ round)

Date: 2026-10-29 · Base: origin/main 4366a47 ·
Final binary sha256 bf03edc984c2bdb68e012189957428342d2dd8dfc32d25236097a7222f4fa2ff
(G1 PIN 12/12, evidence/master/g1/20261029_233823)

## Round goal (user instruction translated)
"Use Wine as the behavioral reference, read the issues, and actually load the
graphics / run the simple programs (notepad, notepad++ and the rest)."

## Wagon 1 — SW-1503: guest-caller attribution + first working guest backtrace

MINWIN_REG_CALLER=1 now attributes every RegQueryValueExW to the guest
module+offset (SW-1262 CreateFileW pattern) and — for the CustomExt probes —
walks the REAL x64 guest stack through the engine's internal unwinder
(seh_lookup_rf_for_pc + seh_internal_virtual_unwind + explicit RA pop: the
internal unwinder restores RSP but leaves RIP; the walker pops
[new_rsp] per frame). First full guest backtrace of the engine:

    EP-region 0x1c6b32 -> WinMain 0x1a73a6 -> 0x98a9c -> 0x9d6b0
      -> 0xfdd9 (ReadRegString call) -> wrapper 0x12b3ae -> helper 0x12b58a

This decoded BOTH open frontiers on the winrarinstd path (below). The tool
replaces hours of static guesswork for every future guest hang.

## Wagon 2 — SW-1504 (issue #4 [REG-1]): registry hard caps removed

Decode: WinRAR 7.12 GUI's first-run default-profile writer produces ~77 values
per profile key (Name/Default/ImmExec/ExclNames/.../SaveSymLinks). The old
hive capped each key at MW_REG_MAXVAL=24 values and returned
ERROR_OUT_OF_MEMORY(14) at value #25. api_trace.json showed the writer
retrying the whole Profiles\0..4 sequence forever — 77x
RegCreateKeyExW('HKCU\Software\WinRAR\Profiles\0') in one run, 77 identical
profile-write storms, SetLastError(14) after every value.

Fix (src/loader/loader.c): per-key value array is now heap-backed and grows by
doubling (mw_reg_vals_reserve), value payloads are exact-size heap buffers
(mw_reg_val_store, 16 MiB sanity cap), RegDeleteValueW frees the vacated slot
before the shift (no pointer duplication), snapshot load/skip bounds loosened
from MW_REG_MAXVAL/MW_REG_DATA to 16 MiB sanity checks (MWREG001 format
unchanged, old snapshots still load). Measured: SetLastError(14) count 77 -> 0;
the profile write sequence now completes exactly once per key.

This is the failure class issue #4 predicted ("remove hard caps (64/16/512)"):
WinRAR is the first guest that actually overflowed them.

## Wagon 3 — SW-1505 (SW-1498 class): regseed_extend.py dropped the NUL

scripts/regseed_extend.py val_sz() serialized REG_SZ without the terminator:
L"" became size=0, "abc" became 6 bytes instead of 8. WinRAR's
Setup\CustomExt wait-loop rejects a size-0 read as invalid (same defect the
loader's own seed had as SW-1498: real size is (len+1)*2). Fixed to append
b"\x00\x00". With the extended fixture (real-world post-install Setup values
ShellExt=1/CascadedMenu=0/MenuIcons=1/LegacyMenu=0/CustomExt="" — byte-verified
against a real WinRAR settings .reg export) the CustomExt read now returns
SUCCESS size=2 exactly like a real machine.

## Wagon 4 — SW-1506 (NEW frontier, honest): WinRAR GUI config-walk loop

With CustomExt present-and-valid the installed WinRAR.exe still loops at
~0.8 reads/s: open Setup -> query CustomExt -> L"" -> heap churn, never
reaching GetMessageW (pump armed, paint pending, window tree complete: main
frame + STATUSBAR/COMBOBOX/TOOLBARx2/SysListView32 + WM_SIZE sync). The
backtrace is byte-identical every iteration (flat loop, not recursion):
WinMain -> 0x98a9c -> 0x9d6b0 -> 0xfb88(ReadRegString Setup\CustomExt,
default L"") -> wrapper 0x12b3ae -> helper. Static decode found the
'*'/'.' mask-scan loop at 0x14000fee6 with back-edge 0x1400100a8 and the
'.exe' comparison in [0x9d298] — the terminator that real Windows satisfies
and our engine does not is still unidentified. Registered as SW-1506; the
SW-1503 backtrace tool is the instrument for the next round. NOT hidden.

## Wagon 5 — SW-1499 decode advanced (nppsetup, Notepad++ installer)

The 191,602,595-step SIGSEGV (guest 0x40696c `mov bp,[edi]`, EDI=0) is now
instruction-traced end-to-end (MW32_STEP_TRACE + MW32_TRACE_RANGE). The NSIS
wide-char copy loop (dest = 0x427a00 + idx*2, the "Notepad++" string arena)
re-enters with a NULL SOURCE pointer: the untraced variable-resolver path
(0x406997 helper, outside the traced range) returns NULL for a stack variable
and the copy loop crashes on [0]. Same latent anomaly SWEEP-096/097 parked
(g_stacktop frame-integrity). Next round: decode 0x406997 with the entry
trace (MW32_ENTRY_TRACE) — the SW-1503 lesson applied to the m32 side.

## Battery (sweep107, serial, 1 run each)

| program | verdict | evidence |
|---|---|---|
| npp64 (Notepad++ 8.9.8.1 portable x64) | PASS_INPUT_STATE_CHANGE frames=35 uniq=16 | plugin_run |
| notepad2mod | PASS_INPUT_STATE_CHANGE frames=4 uniq=3 | plugin_run |
| idman | PASS_LAUNCH_RENDER rc=0 frames=1 | plugin_run |
| winrar (SFX install) | EXIT rc=0 | plugin_run |
| winrarinstd (installed GUI) | SPIN_AT_TIMEOUT frames=0 — SW-1506 | plugin_run |
| nppsetup | FRONTIER_SW crash=1 steps=191602595 — SW-1499 (decode advanced) | plugin_run |

## Console workflow re-verification (issue #18)

WinRAR 7.12 Rar.exe under the loader, fresh run:
`a` (create, 2 members) -> `t` (All OK) -> `x` (All OK) ->
SHA-256 of extracted files byte-identical to originals
(/tmp/winrar_smoke, orig vs out diff clean). Registry changes caused no drift.

## Gates

- G1 PIN 12/12 (bf03edc9, evidence/master/g1/20261029_233823)
- corpus_gate sweep107: 15 rows, rc=0 in 9, crash total 0 (mini.flood + 5 xp.*
  honestly MISSING — sandbox-reset wiped .cache/miniwin/seed/; SEED retest owed)
- WinRAR console a/t/x SHA-exact (this file, Console workflow section)
- No force-push.

## Next round

1. SW-1506: decode the CustomExt walk terminator with the SW-1503 backtrace
   (candidate: WinRAR's per-extension dialog/mask init reading a table our
   engine answers differently than real Windows).
2. SW-1499: MW32_ENTRY_TRACE on 0x406997 (NSIS variable resolver).
3. Issue #4 follow-ups: hierarchical tree (flat path-keyed hive is still the
   model), key-count cap 256, value-name length cap 64.
