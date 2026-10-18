# SWEEP-011 before/after/fast (median of 3 runs)

BEFORE = engine df28de0f (pre-SWEEP-011); AFTER = 937cdc0d parity (defaults, behavior-identical); FAST = AFTER + MINWIN_TRACE=0 + MINWIN_SCREENSHOT=0.

| program | exit B/A | wall BEFORE ms | wall AFTER ms | wall FAST ms | sys B ms | sys FAST ms | trace lines B->F |
|---|---|---|---|---|---|---|---|
| autoruns64 | 1/1 | 5.3 | 5.0 | 4.1 | 1.9 | 1.3 | 960->2 |
| rammap64 | 1/1 | 3.0 | 2.9 | 2.3 | 1.8 | 0.0 | 864->2 |
| procexp64 | 1/1 | 6.0 | 5.8 | 4.7 | 2.1 | 2.2 | 1348->2 |
| snake | 0/0 | 101.6 | 103.0 | 86.3 | 11.0 | 3.0 | 1175->2 |
| breakout | 0/0 | 1228.1 | 1037.8 | 896.1 | 292.1 | 4.0 | 21608->2 |
| t12_libc_io | 0/0 | 1.8 | 1.7 | 1.5 | 1.7 | 1.2 | 373->2 |
| t08_big_cpp | 0/0 | 3.4 | 3.0 | 1.7 | 2.4 | 1.5 | 2644->2 |
| t10_float | 0/0 | 1.5 | 1.5 | 1.2 | 1.3 | 1.0 | 354->2 |
| upx_sample | 1/1 | 3.4 | 2.7 | 2.5 | 3.2 | 1.2 | 455->2 |
| sqlite3 | 0/0 | 7.0 | 5.0 | 3.9 | 5.9 | 2.8 | 2177->2 |
| 7za | 0/0 | 4.7 | 4.5 | 2.7 | 3.4 | 1.6 | 4550->2 |
| curl | -11/-11 | 4.9 | 5.0 | 4.2 | 3.2 | 2.6 | 787->2 |
| puttygen | 139/139 | 2.8 | 2.7 | 2.5 | 1.7 | 1.8 | 430->2 |
