# SWEEP-026 evidence «ادامه فیل یوز با ۵۰ تسک»

- sweep026_sweep.tsv — 16-program corpus x3 on sha 51bc4fb3 (pump-off):
  putty 9->0 (BUG-082 CLOSED), 45/45 deterministic, unresolved deltas
  autoruns 52 / bginfo 201 / cports 20 / procexp 161, zero regressions.
- gui_battery.tsv — pump-on: procmon64 998,347 SendMessageW/12s (was
  26.35M -> SW-1023 real elapse gate, 26x) + api_trace capped at exactly
  1,000,002 lines (SW-1090); putty/bginfo64 frames rendered.
- putty_bug082/ — stdout/stderr + rendered error-dialog BMP
  (miniwin-results/putty.exe/screenshots/messagebox_1.bmp).
- procmon_flood/ — cap marker + SendMessageW count proof.
- G1 pin: evidence/master/g1/20261020_080749 (sha 51bc4fb3, 12/12).
- Calc battery 15/15 + snake/breakout exit0: rebuilt via pip ziglang 0.17
  (artifacts/sweep026/*.exe, battery rerun in artifacts/sweep023/calc_battery).
