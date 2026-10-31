# SWEEP-054 — (10 famous programs and 5 famous old 2D games) — 10 famous programs +
# 5 classic 2D games, XP-era and new, ALL WITH SCREENSHOTS

Binary: **09c33df201af8ade** (sha256 prefix; G1 PIN 12/12 at
evidence/master/g1/20261023_030335). Reconciled start: origin/main 0f7bd36
(SWEEP-053) fast-forwarded locally before any work (user rule #1: GitHub
first, always).

## Mandate

User: (rule one: always search GitHub first, stay up to date, verify every push;
find 10 famous programs and 5 famous old 2D games, run them too, test the old
programs — Windows XP era — and the new ones) — (1) always check GitHub first and verify pushes; (2) find and RUN
10 famous programs; (3) find and RUN 5 famous old 2D games; (4) test old
(Windows XP) programs and new programs; plus the standing screenshot mandate
(also show screenshots of the programs running).

## Corpus re-staging (sha-pinned, gitignored payloads rebuilt)

- XP x64 ISO 19b5a382 (628,617,216 B, byte-verified) → 26/29 payloads
  extracted (pycdlib; NEW: SPIDER.EX_ 268,373 B + SPIDER.CH_) → expanded
  via the engine's own 7za, 22/22 rc=0 (scripts/sweep054_expand.sh)
- Everything 1.4.1.1028 zip 4f4a9b92 sha-verified → everything.exe
- LibreOffice MSI URL went 404 at downloadarchive (26.8.1.2 removed);
  LO re-staging deferred — SW-1263 frontier unchanged, not a regression
- Guests staged with support DLLs beside each exe (cards.dll for
  sol/freecell/mshearts/spider; mfc42u.dll for mshearts/mspaint/wordpad;
  riched20.dll for wordpad)

## Census — 15 guests x3 (artifacts/sweep054/census/census.tsv)

45/45 deterministic. **All 15 exit 0.** Stubs: winmine 0, sol 0, freecell 0,
mshearts 0, spider 0, calc 0, notepad 0, mspaint 0, wordpad 0, charmap 0,
clipbrd 0, write 0, winver 0, sndvol32 0, everything 0. The 5 games and 10
applets are ZERO-UNRESOLVED at this binary.

DL corpus census x3 (artifacts/sweep054/census/dl_census.tsv): 15/15
deterministic — 12 exit-0 (autoruns64, bginfo64, coreinfoex64, cports,
diskmon64, procmon64, produkey, putty, puttygen, rammap64, searchmyfiles),
pageant 124 = live pump to timeout (its normal full-GUI behavior), procexp64
139 (its baseline frontier), winmergeu 133 (its accepted /GS fail-fast
frontier), zoomit64 rc=1 clean, adexplorer64 NO-EXE (SW-1206 corpus loss,
still open).

## SWEEP-054 engine wagons LANDED (all root-caused by disassembly/trace)

1. **SW-1269 ShowWindow first-show paint** — real Windows generates the
   first WM_PAINT when a window is SHOWN; v1 only fired the CBT hook, so
   guests waiting for the system's first paint (XP spider:
   ShowWindow(SW_MAXIMIZE) + plain GetMessage loop) never painted — the
   pump saw "no event source" and quit with a white screen.
   + **SW-1269b**: SW_MAXIMIZE grows the paint surface to 1024x768 (real
   work-area semantics; spider's board is 704x540, CW_USEDEFAULT had left
   the surface at 32x32).
2. **SW-1270 posted-message queue** — PostMessage A/W DROPPED every message
   headlessly; real Win32 queues posted messages and delivers them from
   GetMessage BEFORE generated (lower-priority) messages. Spider posts its
   own WM_COMMAND (deal action) before its pump starts. Headless batch
   keeps the v1 drop contract (regression-pinned exit-0 census).
3. **SW-1271 REAL accelerators** — LoadAcceleratorsW/A now parse
   RT_ACCELERATOR (type 9) via the w3_res_find path and mint SW-1128
   store handles; TranslateAcceleratorW/A translate WM_KEYDOWN/WM_CHAR
   against the table (FVIRTKEY VK + FSHIFT/FCONTROL/FALT matched against
   the scripted-key modifier latch) and SEND WM_COMMAND on match.
   Root cause (spider disassembly, RVA 0x10000bebf): its classic pump
   `cmp msg.hwnd,main; jne dispatch; test rbx,hAccel; je loop` — with
   hAccel==NULL the main window's messages SKIP Translate/Dispatch
   entirely, so WM_PAINT was retrieved but never reached the WndProc.
   Real XP spider has RT_ACCELERATOR id=1: 96 B = 16 entries (F1..F5,
   D/M/N/O/S/Z + Ctrl variants) — LoadAccelerators must SUCCEED for the
   game to function. + IS_INTRESOURCE trace guard: MAKEINTRESOURCE names
   are small integers; the A-path trace formatted them as '%s' (SEGV —
   caught by the ProduKey/cports regression, fixed before landing).
4. **SW-1272 BitBlt frame capture** — a SRCCOPY INTO the window surface is
   a presented frame (SWEEP-006 breakout contract). XP spider never calls
   BeginPaint/EndPaint; its whole board is direct BitBlts, so the EndPaint
   dump path never fired. + **SW-1272b MINWIN_FRAME_EVERY=N** disk-write
   throttle (spider's deal animation presented ~3,700 frames x 2.3 MB =
   8.6 GB in one run — filled the disk; numbering stays unconditional).
5. **SW-1274 REAL timeGetTime** — spider's deal animation is paced by
   timeGetTime deltas; the v1 stub-fallback returned 0 forever, freezing
   the clock on the first card. Now the engine's monotonic ms clock.

## MEASURED (screenshots in evidence/sweep054/shots_png/)

- **XP SPIDER SOLITAIRE RENDERS REAL CARDS**: 5♥ 2♦ 10♣ faces + the iconic
  spider-logo deck, deal animation COMPLETES (rc=0, no stall) —
  05_xp_spider.png. First time spider has ever rendered anything.
- winmine 82 frames (mouse clicks on the board reveal cells), freecell 9,
  mshearts 2, sol 1, calc face, mspaint NOW RENDERS (SW-1269 first-show
  paint fixed it too), wordpad + dialog, sndvol32 + dialog, everything at
  778x605 with search-click, putty config dialog, RAMMap + dialog,
  Bginfo desktop — 15 PNGs total.
- G1 PIN 12/12 (evidence/master/g1/20261023_030335, binary 09c33df2);
  census ZERO deltas on the events=0 batch; DL corpus 45/45 deterministic.

## Open frontiers (registered)

- **SW-1275** spider board polish: class background brush (gray instead of
  the classic green — the guest's hbrBackground brush is not applied to
  the surface) + one blank card face (a LoadImage/RT_BITMAP card face the
  compositor misses).
- **SW-1273** LibreOffice re-stage: documentfoundation archive URL 404 —
  find the new mirror path for 26.8.1.2 MSI (sha c6298b10) or pin the
  current 26.8.x release; SW-1263 rtl_uString truncation frontier continues.
- **SW-1206** adexplorer64 corpus loss (NO-EXE) — still open.
