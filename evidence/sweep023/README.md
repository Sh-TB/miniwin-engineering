# SWEEP-023 — (60-task continuation) 60-task batch evidence (2026-10-19)

One loader.c batch: SW-961..1000 LANDED (25 imports + 3 data models + registry
overwrite FIX) + SW-1001..1020 queued. Commit series on top of 05449c5.

## What to look at

| Artifact | What it proves |
|---|---|
| `sweep/sweep023_sweep.tsv` | 15-prog corpus x3 deterministic (45/45), ZERO regressions (7 exit0 / 4 exit139 / 4 exit1 parity with SWEEP-022) |
| `sweep/procexp64/run*/stderr.txt` | stub call #1 @0x14006c328 deterministic x3 -> BUG-078 |
| `pumpon/*/` | autoruns64/rammap64 139 isolated (BUG-076/077); produkey/cports/bginfo64/diskmon64 pump-on exit0 |
| `GUI_EVIDENCE.tsv` | 10+ GUI programs with lifecycle evidence |
| `calc_battery/` | 15/15: A/C/D clicks 9/9 + K `12+3=15` via WM_KEYDOWN + F focus walk (Tab/arrows) exit0 frames |
| `reg_stderr.txt` | mw_reg_test `pass=19 fail=0` — REG_MULTI_SZ byte-exact, DWORD overwrite fix, SHGetValueW end-to-end |
| `miniwin_*.exe` | zig toolchain restored (SW-997); rebuilt from tests/games/ |
| `STAGE_TABLE_100_REFRESH.tsv` | honest 100-list refresh scope (16 local programs; wider corpus cleaned -> SW-1003) |
| `sweep023_stub_decode.json` | static stub decode attempt + why it is impossible for packed procexp64 |

## Exit1 gates now SELF-NAMED (queued as SW-1001..1015)

- pageant: own MessageBox "Unable to access security APIs" (CopySid,
  GetLengthSid, InitializeSecurityDescriptor, SetSecurityDescriptorDacl,
  CreateNamedPipeA, ConnectNamedPipe)
- procmon64: AdjustWindowRectEx / BeginDeferWindowPos / CallNextHookEx family
- putty: CreateFontA / CreatePalette / ChooseFontA / Beep / ClearCommBreak
- coreinfoex64: exit0 x3, 0 unresolved — SW-561 version drift RESOLVED (verified)

## Build

`make` clean; binary 1,880,672 -> 1,935,792 bytes.
G1 re-pin: evidence/master/g1/<timestamp of this sweep>/.
