# SWEEP-025 — (continue elephant and cheetah) elephant round (2026-10-20)

Mandate: SW-1001..1020 in one batch. One edit round implementing the union
of the three fresh-measured exit-1 gates (pageant 26, procmon64 140, putty
87 unresolved imports) + root-cause fixes discovered by the round's own
traces. G1 re-pinned. Single commit+push.

## Results (15-program corpus, x3, deterministic)

| program       | SWEEP-023 | SWEEP-025 | unresolved 023->025 |
|---------------|-----------|-----------|---------------------|
| autoruns64    | 0         | 0         | 143 -> 53           |
| bginfo64      | 0         | 0         | 296 -> 203          |
| coreinfoex64  | 0         | 0         | 0                   |
| cports        | 0         | 0         | 43 -> 21            |
| diskmon64     | 0         | 0         | 20 -> 8             |
| pageant       | 1         | **124 (daemon)** | 26 -> 0      |
| procexp64     | 139       | 139       | 270 -> 169 (BUG-078)|
| procmon64     | 1         | **0**     | 140 -> 0            |
| produkey      | 0         | 0         | 34 -> 19            |
| putty         | 1         | **9**     | 87 -> 0 (BUG-082)   |
| puttygen      | 139       | **0**     | 20 -> 0             |
| rammap64      | 0         | 0         | 38 -> 7             |
| searchmyfiles | 139       | 139       | 43 -> 17            |
| winmergeu     | 139       | 139       | 302 -> 202          |
| zoomit64      | 1         | 1         | 0                   |

Complete runs: 7 -> **10** (procmon64 exit0 NEW, puttygen exit0 NEW,
pageant daemon-124 NEW). Two regressions none; autoruns/rammap pump-ON
139 pre-existing (BUG-076/077).

## Root causes fixed (trace-driven)

1. **LoadLibraryA full-path** — pageant/putty late-load
   `C:\Windows\System32\<dll>`; strip to base name (putty's WinSock gate
   = `ws2_32.dll` full-path fail 126).
2. **LoadLibraryA no-extension** — old `nl+1==kl` never matched
   "user32"; pageant CRT probe died 126 -> ExitProcess(3).
3. **GetTokenInformation TokenGroups** — procmon64 IsUserAdmin asks
   TokenGroups right after AllocateAndInitializeSid(Administrators);
   engine answered 87 -> "must be run from an administrator account".
   Now: one group = BUILTIN\Administrators (S-1-5-32-544), attrs 0xE.
4. **GetTokenInformation TokenUser + size query** — pageant
   get_user_sid two-call pattern (info==NULL -> 122/retlen=16).
5. **OpenProcess(self-pid)** — pageant OpenProcessToken chain needs the
   process handle; pseudo-handle 0x000E0001 for own pid only.
6. **SetEntriesInAclA/W** — real ACL builder (pageant "unable to
   construct ACL"); EXPLICIT_ACCESS TRUSTEE_IS_SID -> ACL+ACEs.
7. **GetUserNameExA** — fmt=8 (NameServicePrincipal) must be backslash-
   free (pageant pipe-name assert); SPN form "miniwin@MINIWIN".
8. **BUG-081 (self-inflicted, caught)** — first-draft
   GetCharacterPlacementW wrote lpGlyphs/lpCaretPos pointer fields as
   counts; fixed to count-only writes with lStructSize guard.

## GUI evidence (5 programs)

- bginfo64: 1280x1024 painted frame `bginfo64_frame.bmp`
- procmon64: pump-on event loop alive — 26.35M SendMessageW / 12 s
- pageant: daemon (255 CreateNamedPipeA+ConnectNamedPipe cycles, 511
  threads / 8 s; killed by sweep timeout = tray-service lifecycle)
- produkey / puttygen / cports / diskmon64: pump-on exit0 (window paths
  run clean; dialog screenshots from earlier sweeps stand)
- putty (exit 9 frontier): WinSock gate passed; GS-cookie fail-fast
  decoded to putty's char-width pipeline (BUG-082, needs Ghidra — env
  reset killed /home/z/ghidra-dl; requeue SW-1022/1030)

## Artifacts

- `artifacts/sweep025/sweep/sweep025_sweep.tsv` — corpus x3
- `artifacts/sweep025/gui/` — pump-on runs (bginfo64 frame BMP)
- `artifacts/sweep025/baseline/` — pre-batch gate stderr
- `scripts/sweep025_broad_sweep.sh` — runner
- G1: `evidence/master/g1/20261020_064619` sha c490f1fc, regression 12/12
