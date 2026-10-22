# SWEEP-053 — real-program SCREENSHOTS batch: XP calc renders, XP games render

Binary: **4e6e4a44234521f9** (sha256 prefix of minwin_loader after
SW-1264/1265/1266/1266b). G1 PIN 12/12 at evidence/master/g1/20261022_215001
(binary f26bf49a — pre-1266b state; the 1266b surface-grow change is covered
by the census below + a second G1-equivalent run note in README).

## Mandate context

User: «ادامه اسکرین شات اجرای برنامه ها رو هم نشون بده» — continue the train
AND show screenshots of the running programs. This batch makes the four
real-program mandate targets VISIBLE, not just exit-code-clean.

## Corpus re-staging (all sha-pinned)

artifacts/sweep050 + sweep051 are gitignored — re-staged from the pinned
sources (evidence/sweep051/payloads_sha256.txt):
- XP x64 ISO 19b5a382 (628,617,216 B) -> 23 payloads extracted (pycdlib)
  -> expanded via the engine's own 7za (rc=0 x21)
- LibreOffice 26.8.1.1 MSI c6298b10 (374,870,016 B) -> payload CAB
  71dfff0b (354,681,357 B) -> 19427/19427 files, 0 failures
  (libmspack built from source: cabd+crc32+system+lzxd+mszipd+qtmd);
  soffice.bin cb08cffd / mergedlo.dll 017d0c21 = byte-exact pins
- Everything 1.4.1.1028 zip -> everything.exe
- staging gaps FIXED: mfc42u.dll beside mspaint/mshearts/wordpad (the
  MFC ordinal imports crash the guests without it — rc=139 vs the
  SWEEP-052 baseline 0/0/0), riched20.dll beside wordpad, cards.dll
  beside sol/freecell/mshearts

## Train 1 — SW-1264: resource DATA_ENTRY -> data conversion

Decode: XP calc.exe creates its main window via CreateDialogParamW(hInst,
MAKEINTRESOURCE(0x66), 0, NULL, 0) — dialog-based app. Two engine bugs made
that dialog EMPTY:

1. w3_res_find returns the IMAGE_RESOURCE_DATA_ENTRY; mw_dlg_from_resource
   handed it to mw_LockResource — an IDENTITY no-op since SW-1259 — so the
   template parser read the 16-byte ENTRY HEADER as a DLGTEMPLATE
   (dlgver = low half of the data RVA, sig = high half — never 1/0xFFFF),
   the EX branch skipped, the STD walker consumed a header -> ZERO children.
   Fix: engine-internal consumers convert entry -> data
   (g_image_base + entry.OffsetToData) exactly like mw_LoadResource.
   Same fix in mw_loadimage_bitmap_dib (RT_BITMAP DIBs were equally
   unreadable).
2. (Train 3) the subclass gate — see SW-1266.

MEASURED: calc dialog 0x66 now resolves (size 1196, DLGTEMPLATEEX cdit=30),
30 children materialize (EDIT id=403 + 29 BUTTONs with template rects).

## Train 2 — SW-1265: real LoadStringW/A

v1 stub always returned 0 ("resource absent") — XP calc's 86 LoadStringW
calls (control labels) all came back empty. Implemented RT_STRING blocks:
resource id = (id >> 4) + 1, 16 length-prefixed UTF-16 strings per block,
index = id & 15, real truncation contract (copies min(len, cch-1), returns
copied count). MEASURED: LoadStringW(id=0) -> 3 wchars, id=1 -> 1, id=3 ->
9 ... all 86 resolved.

## Train 3 — SW-1266: per-window subclass chain + standard-control compositor

Decode (disassembly of calc.exe WinMain 0x100002364..0x1000023a7):
GetDlgItem(main, 403) -> GetWindowLongPtrW(child, GWLP_WNDPROC) -> if
nonzero, SetWindowLongPtrW(child, GWLP_WNDPROC, 0x100008bc0). The v1
GetWindowLongPtrW answered 0 for fresh windows, so calc's `test rax,rax;
je` skipped the subclass and calc's real WndProc NEVER RAN. Also: calc's
subclass proc (0x100008bc0) is a pass-through — HideCaret + CallWindowProc
(saved old) — the painting belongs to the ORIGINAL control class proc.

Fixes (all in src/loader/loader.c):
1. mw_engine_defproc — a real guest-callable DefWindowProc-subset function;
   GetWindowLongPtrW(GWLP_WNDPROC) answers it (non-zero) for engine windows
   with no guest subclass, so the classic `old = Get(...); if (old) Set(...)`
   pattern proceeds. CallWindowProc(old) chains honestly.
2. SetWindowLongPtrW(GWLP_WNDPROC) is PER-WINDOW state (store slot -4);
   the global g_wndproc_fwd stays the main-window channel (SWEEP-013
   semantics preserved for main/toplevel hwnds).
3. SendMessageW routes messages for guest-subclassed children through
   their stored proc (after the engine's SETTEXT/GETTEXT/BM_/commonctl
   handling — superset paint honesty class).
4. Standard-control compositor: mw_engine_paint_children(hwnd) renders
   every dialog-template child (rect from the EX item walker, DLU x2)
   with Win95 chrome (mwb_3d raised buttons, sunken white EDIT, labels)
   into the paint surface, one frame per UpdateWindow. Surface sizing:
   the dialog template's own cx,cy sizes the surface (grow-only resize in
   mw_gui_store_window — the first guest window is often a 0x0 message
   helper clamped to 32x32).
5. (SW-1266b) SetWindowPos/MoveWindow grow the surface (winmine sizes its
   board via MoveWindow AFTER a 2x0 create; without the grow the board
   BitBlts clip at 32x32).

## MEASURED (the screenshots — evidence/sweep053/shots_png/, user copy at
## /home/z/my-project/download/screenshots/)

| program | frame | what it shows |
|---|---|---|
| **XP calc.exe** | 338x258 | the real XP calc face: display "0.", Backspace/CE/C, MC/MR/MS/M+, 7 8 9 / sqrt, 4 5 6 * %, 1 2 3 - 1/x, 0 +/- . + = — all 30 template controls at template rects |
| **XP winmine.exe** | 170x1024 (board 170x230 area) | real board chrome: 3 LED counter boxes, sunken minefield, raised border |
| **XP winmine.exe** | 3x messageboxes 200x102 | Win95 dialog chrome: navy caption, X icon, OK button |
| **Everything 1.4.1** | 778x605 | main window at its real size (white = no search content — honest state) |
| **XP freecell.exe** | 640x480 | real green card-back blits (home cells + free cells) |
| **XP sol.exe** | 154x64 | status bar (deck draws land before the surface grow — follow-up) |
| **XP mshearts.exe** | 1280x32 | top strip (79712x0 create-decode artifact clamps — follow-up) |
| soffice.bin | rc=99 stub 8 | bootstrap frontier UNCHANGED (SW-1263 rtl_uString truncation) — regression-clean |

## Regression

- G1 12/12 (evidence/master/g1/20261022_215001, binary f26bf49a)
- census: 11 XP guests x2 + everything + soffice vs SWEEP-052 baseline:
  ZERO deltas (artifacts/sweep053/census/regression.tsv; soffice rc=99
  stub 8 = its SWEEP-052 value; DL-corpus programs not re-run this round —
  their corpus is a separate re-stage task, SW-1206 class)

## Open cars

- SW-1263 continues: soffice rtl_uString allocator/length-header truncation
  ('C:\artifacts\s' cut points) — the ODF display program's remaining gate
- SW-1267 (new): sol/mshearts surface sizing — deck/board draws land
  before MoveWindow grows the surface; needs the guest's sizing order
  decoded (MoveWindow fires, draws land pre-grow)
- SW-1268 (new): Everything content paint — the guest paints white because
  its list content is driven by its NTFS index (absent headless); a fake
  index volume would give the list real rows to paint
- SW-1206 (unchanged): DL-corpus (procexp64/putty/winmergeu/...) re-stage
