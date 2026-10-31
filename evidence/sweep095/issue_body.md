# SWEEP-095 — SW-1473: the wsprintfW wide-%s contract violation (Issue #3 root cause) fixed at the engine layer

Round type: engineering train (fix + gates + battery). Follows the closed
[RESEARCH-ONLY][TOOL-EVAL] Issue #3, whose final report (comment 6460839697)
identified a loader-side root cause for the IDM launch chain: `wsprintfW`
converted the WIDE format to UTF-8 and reused the ANSI format engine, whose
`%s` reads a narrow `char*`. A wide argument was consumed as `(char*)` and
stopped at the first `0x00` high byte, so every `%s` produced exactly one
character. This train converts that research finding into a fix, with
before/after measurement.

## SW-1473 — fix (src/loader/loader.c)

Contract implemented (MS wsprintf/wprintf family; cf. Wine
`dlls/user32/wsprintf.c` `WPRINTF_ParseFormatW`): in a W format `%s` = wide
arg, `%S` = narrow (case-inverted), `%hs` = narrow, `%ls`/`%ws` = wide.

- `wsprintfW` / `wvsprintfW` / `_vsnwprintf` / `wprintf` previously converted
  the wide format to UTF-8 and called the ANSI engine (`mw_vfmt_a`). All four
  now route through the native wide engine `mw_vfmt_w16` (which already
  implemented the wide-printf rule for `swprintf`/`vswprintf`).
- `mw_vfmt_w16` hardening: the `h` modifier was consumed and ignored, so
  `%hs` in a wide format read the wrong half of the varargs; it now forces a
  narrow argument. `%ws` (string-only) now routes wide instead of falling to
  the honest-literal path.
- `_vsnwprintf` keeps its real contract: truncate to `cnt`, NO terminator,
  return chars written. `wprintf` converts the finished wide string to UTF-8
  for host stdout.

## Direct evidence (IDM 6.42 build2 unpacker, full battery env, gdb probe)

The guest composes its child-process command line with
`wsprintfW(buf, L"\"%s%s\" -d \"%s\"%s", ...)`; that buffer is passed to
`CreateProcessW` as `cmd`.

| State | `cmd` (measured) |
|---|---|
| before fix (Issue #3 W2, measured) | `"CI" -d "C"` |
| after fix (SWEEP-095, measured) | `"C:\Windows\Temp\IDM_Setup_Temp\IDM1.tmp" -d "C:\Windows\Temp\IDM_Setup_Temp\"` |

`dir` = `C:\Windows\Temp\IDM_Setup_Temp\` (correct both before and after).
The `CreateProcessW` capability boundary (no child process support) is an
engine decision and remains unchanged — but the guest now builds a fully
correct launch string. The A03 "guest constructs broken launch" attribution
is superseded: the guest was correct all along; the loader's `wsprintfW`
broke the string.

## Secondary effect — SW-1469 garbled DLL names eliminated

The NPP installer (unicode NSIS) previously loaded
`C:\Windows\System32\<0x3f-garbled>.dll` 23x in an ERROR 126 loop (SW-1469,
"progressively truncated windows of a longer mangled sequence"). After
SW-1473 the same installer composes and loads **23/23 clean names**
(UXTHEME, USERENV, SHFOLDER, SETUPAPI, RichEd32, RichEd20, PROPSYS, OLEACC,
...). The mangling was downstream damage of the same `%s` bug (each retry
re-fed a truncated buffer). The nppsetup run now reaches
`DialogBoxParamW` + `WM_INITDIALOG` and sets progress text; the remaining
frontier is the unresolved `RichEd20.dll`/`RichEd32.dll` requests
(`ERROR_MOD_NOT_FOUND`, honest miss — not in the known-DLL plane) and an
undecoded crash at the same step count (139,972,897). Also noted: one
destination path joins as `Program Filesry` (missing separator) — recorded
for a later train.

## Gates

- G1 pin: `evidence/master/g1/20261028_045014` — clean build, binary sha
  `c630edb6a9377582...` (4,654,488 B), UPX banner exact, regression 12/12,
  no crash.
- Corpus gate (`scripts/corpus_gate.sh sweep095`): 17 rows, rc=0 in 12,
  crash total 0, x3 determinism green — identical shape to SWEEP-094.
  `mini.flood` was rebuilt from tracked source (`tests/games/miniwin_flood.c`,
  ziglang 0.17.0) after the sandbox reset lost `.cache/miniwin/built/`;
  output size matches the SWEEP-093 pin byte-count exactly (223,744 B);
  manifest re-pinned to the new sha (zig embeds timestamps — documented
  non-reproducibility), then 3x rc=0, 13 frames, crash=0 fatal=0.
- Battery (SWEEP=sweep095, 3 runs each): idman rc=0, npp64 rc=0, winrar rc=0,
  winrarinstd alive-at-timeout 124 crash=0, resourcesextract rc=0,
  7zsetup rc=86 (own decision), everything/tcpview64/produkey alive-at-timeout
  124 crash=0, nppsetup rc=139 (documented SW-1469 successor frontier).
  Zero unexpected outcomes.
- `repo_size_guard.sh`: GUARD OK (source-only policy clean; the 2 documented
  re-pin WARNs shexview/hxd unchanged).

## Next trains (queued, not implemented)

- riched20/riched32 known-DLL plane entries for the NPP installer chain.
- Main-window content paint (children round-robin) for NPP/WinRAR.
- ReadEventLogA honest empty-log contract; CW_USEDEFAULT positioning.
- `Program Filesry` destination-path join decode.
