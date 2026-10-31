# SWEEP-097 — SW-1476: honest empty-log eventlog plane (psloglist64's imports fully resolved)

Round type: engineering train (small win + decode notes). Also carries the
SWEEP-096 follow-up decode of the NPP installer crash.

## SW-1476 — eventlog plane (src/loader/loader.c)

Sysinternals tools import the eventlog API set directly. Before this train
psloglist64 2.82 ran with **2 unresolved imports** (`advapi32!ReadEventLogA`,
`advapi32!OpenBackupEventLogA`) served by the generic 0-stub, and the W-side
`ReadEventLogW` model returned 0 WITHOUT setting last-error — guests read a
stale error. The modeled log has zero records, and the real Windows contract
for reading an empty log is documented: FALSE + `GetLastError()==
ERROR_HANDLE_EOF` (259).

Landed (all honest-contract models, no fake data):

| API | model |
|---|---|
| `OpenEventLogA` | cookie handle (same plane as the existing W twin) |
| `ReadEventLogA` / `ReadEventLogW` | FALSE + `SetLastError(259)`, `*read=0`, `*needed=0` |
| `OpenBackupEventLogA` | NULL + `ERROR_NOT_SUPPORTED` (saved .evt files not modeled) |
| `BackupEventLogA` | FALSE + `ERROR_NOT_SUPPORTED` (no log source to save) |
| `ClearEventLogA` | FALSE + `ERROR_ACCESS_DENIED` (cannot mutate a log we do not have) |
| `NotifyChangeEventLog` | FALSE + `ERROR_NOT_SUPPORTED` (no events will ever arrive) |
| `GetNumberOfEventLogRecords` | TRUE + `*n=0` |
| `GetOldestEventLogRecord` | TRUE + `*oldest=0` |

Measured (psloglist64 2.82, direct run): unresolved imports 2 → **0**
(IAT listing shows all 8 advapi32 eventlog imports resolved); the guest's
`GetLastError()` reads after the read attempt now return 259 consistently
(trace: `SetLastError(259) caller=0x208718e` then `GetLastError() = 259` x3);
the tool takes its read-failure branch ("Error reading System event log on :"
— its own handling of a genuinely empty log) and exits rc=0. The v1
"stale last-error" path is gone.

## SWEEP-096 follow-up decode — NPP installer crash (g_stacktop probe)

The SWEEP-096 report queued the decode of the guest fault at EIP 0x406740
(NSIS exehead string resolver, negative-index branch). gdb state probe at
SIGSEGV (full battery env):

- `[0x428a3c]` (the `g_stacktop` slot) = **0x111301c2 — NOT null**. The
  SWEEP-096 "g_stacktop = 0" hypothesis is CONTRADICTED. The value points
  into the extracted NSIS header data block (0x1113xxxx arena region) —
  consistent with the one writer found statically
  (`mov [0x428a3c], edx` with `edx = header_ptr+10` at 0x404106).
- The faulting guest instruction is `mov eax,[ecx]` (0x406740) with the
  derived pointer = 0; `lea eax,[eax*4+4]; sub ecx,eax` math over
  g_stacktop cannot produce 0 for any negative arg2 with a non-null
  g_stacktop — so the faulting call's `[esp+8]` (arg2) or the stack frame
  itself is inconsistent at that point. The post-fault guest stack window
  (0x11a54fa0..0x11a54fbf) reads all zeros, which cannot legally satisfy
  the jge-taken path either — the frame contents are the anomaly, not the
  global.

Status: **UNRESOLVED, decode advanced.** Next experiment (queued): catch the
crashing call at function entry (0x406726) and dump `[esp]`/`[esp+4]`/
`[esp+8]` plus the caller's frame to establish whether the caller's stdcall
frame is intact — i.e. whether an earlier stack imbalance (an arity-resync
case in the cpu32 call machinery) corrupted the frame. No code changed for
this decode (research step only).

## Gates

- G1 pin: `evidence/master/g1/20261028_060651` — binary sha `d61c7a6e1cdd2119...`,
  regression 12/12, no crash.
- Corpus gate (`scripts/corpus_gate.sh sweep097`): 17 rows, rc=0 in 12,
  crash total 0 — psloglist64 3x rc=0 with the new plane.
- Battery spot-check (SWEEP=sweep097): idman / npp64 / winrar all rc=0 x3 —
  no regression from the dispatch additions.
- `repo_size_guard.sh`: GUARD OK (2 documented re-pin WARNs unchanged).

## Next trains (queued, not implemented)

- NSIS crash frame-integrity probe at 0x406726 (see decode above).
- Main-window content paint (children round-robin) for NPP/WinRAR.
- `Program Filesry` destination-path join decode (SWEEP-095 observation).
- CW_USEDEFAULT cascading position — parked: sizing (SW-1471) landed; no
  guest evidence yet that position affects any run (evidence-first).
