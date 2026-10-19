# SWEEP-022 — «ادامه» elephant batch (SW-921..960 landed + BUG-074/075 fixes)

Date: 2026-10-19 (UTC). Loader change: ONE batch — dialog walk + ListView/TreeView
finals + GDI DIB family + fonts + registry hive + CRT radix converts, ~45 new or
upgraded imports. Build: 1,836,696 -> 1,880,672 bytes (+43,976). One `make`,
zero errors.

## What landed (SW-921..960, 8 families in one batch)

- **SW-921..925 dialogs**: DialogBoxIndirectParamW/A walk the real DLGTEMPLATE
  (word-aligned SZ_OrOrd, DS_SETFONT, DWORD-aligned items, class ordinals
  0x80..0x85) and register template children in the shared child registry so
  GetDlgItem binds for real; SendDlgItemMessageA/W routed.
- **SW-926..930 ListView** (+BUG-074): 0x1008/0x1009 constant swap fixed
  (DELETEITEM row-shift / DELETEALLITEMS rows-only); LVM_GETITEMA/W,
  SET/GETCOLUMNA/W (header store), SORTITEMS typed no-op.
- **SW-931..935 TreeView** (+BUG-075): TVGN values corrected to the real
  commctrl.h table (ROOT=0 ... CARET=9, LASTVISIBLE=10) — SWEEP-021 answered
  wp==4 (CHILD) with the root handle; NEXT/PREVIOUS now walk insertion order;
  GETITEMRECT / GETVISIBLECOUNT(20) / HITTEST(TVHT_NOWHERE) / SORTCHILDREN.
- **SW-936..940 GDI DIB**: GetDIBits/SetDIBits real 24bpp bottom-up pixel
  paths over the CreateDIBSection pool; MaskBlt SRCCOPY->BitBlt routing;
  BitBlt now strips CAPTUREBLT(0x40000000)/NOMIRRORBITMAP(0x80000000) from
  rop so SRCCOPY|CAPTUREBLT reaches the copy path (bginfo64 desktop paint).
- **SW-941..945 fonts**: EnumFontFamiliesExW/A (ENUMLOGFONTEX shape),
  GetTextFaceA/W, SetTextJustification; CreateFontIndirectA/W +
  GetTextMetricsA/W verified pre-existing.
- **SW-946..950 network**: audited complete (htons/ntohl/ntohs already wired
  incl. fixed-ordinal rows 9/14/15; ioctlsocket/getsockopt/setsockopt/
  WSAIoctl FIONREAD pre-existing) — no dead code added.
- **SW-951..955 registry — in-memory hive**: empty-at-start contract (reads of
  never-created keys fail ERROR_FILE_NOT_FOUND exactly as before); create/set/
  query(enum)/delete/value-enum/info all real; A-tails (OpenKeyExA/
  CreateKeyExA/QueryValueExA/EnumKeyExA/EnumValueA/DeleteKeyA/DeleteValueA/
  QueryInfoKeyA) route through the W hive; RegNotifyChangeKeyValue -> typed 0.
- **SW-956..960 CRT**: _itoa/_ltoa real signed radix converts (beside _ultoa);
  _stricmp/_strnicmp/_wcsicmp verified pre-existing.

## Measured

- 15-program corpus x3: **exit-deterministic 45/45; ZERO regressions vs
  SWEEP-021** — parity 7 exit0 / 4 exit139 / 4 exit1.
- Corpus unresolved 1505 -> 1476 (**-29 on real call paths**): bginfo64 -4,
  winmergeu -6, putty -4, produkey -4, cports -3, procmon64 -1, pageant -2,
  puttygen -3, searchmyfiles -1, procexp64 -1 (plus one NEW stub call on
  procexp64 @guest_rip 0x14006c328 — deeper progress past the old frontier,
  queued as SW-964).
- TSV: `artifacts/sweep022/sweep/sweep022_sweep.tsv` (chunked via
  `scripts/sweep022_chunk.sh`).

## GUI accounting (user standing question: «۵ تا برنامه گرافیکی اجرا کردی؟»)

**YES — 6 GUI programs with lifecycle evidence THIS sweep (>= 5 mandate):**

| program | kind | mode | exit | evidence |
|---|---|---|---|---|
| autoruns64 | real Sysinternals GUI | pump-off x3 | 0/0/0 | sweep022_sweep.tsv |
| bginfo64 | real Sysinternals GUI | pump-off x3 | 0/0/0 | sweep022_sweep.tsv |
| cports | real NirSoft GUI | pump-off x3 | 0/0/0 | sweep022_sweep.tsv |
| diskmon64 | real Sysinternals GUI | pump-off x3 | 0/0/0 | sweep022_sweep.tsv |
| produkey | real NirSoft GUI | pump-off x3 + pump-on | 0/0/0 + 0 | sweep022_sweep.tsv + pumpon/ |
| miniwin_calc | in-house Win32 GUI (tests/gui/calc.exe) | pump x3 x3 scenarios | **9/9 PASS** | artifacts/sweep022_calc/ |

Plus pump-on exit0: produkey, bginfo64, cports, diskmon64
(`artifacts/sweep022/pumpon/`). Cumulative GUI lifecycle accounting
(SWEEP-021 + this sweep): **9 programs** — the six above + snake/breakout
games whose binaries are untracked-lost (sources intact; rebuild queued as
SW-997).

Honest note: autoruns64/rammap64 **pump-on** exit 139 is PRE-EXISTING —
verified by rebuilding origin/main (SWEEP-021) and reproducing the identical
crash signature (`artifacts/sweep022/pumpon_baseline/`); NOT a regression of
this batch (pump-off both exit 0 x3).

## G1 re-pin

**OK** — regression 12/12, UPX banner exact, no crash dumps, binary sha256
`aecfc024ea2fa8bd192dda37b1358182c72d448b9d1f47e6b6d94eb97966aeaf`,
`evidence/master/g1/20261019_193310/`. calc acceptance re-run post-pin: 9/9.

## Still open (honest)

- exit139: puttygen (SW-807), searchmyfiles (SW-814), winmergeu (SW-815),
  procexp64 (SW-809 + new stub SW-964).
- exit1: pageant, procmon64, putty, zoomit64 (SW-553 family).
- autoruns64/rammap64 pump-on 139 — pre-existing, queued SW-971.

## SW-961..1000 — continuation queue (40 items, «ادامه سه برابری» standing order)

See `.agent/SWEEP50_WORKLIST.md` — crash frontier x5, exit1 family x5,
pump-on frontier x5, ListView/Tree data x5, GDI/text x5, network x5,
registry depth x5, infra x5 (PE-DLL loader promote, zig restore,
100-program re-run).
