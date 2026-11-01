# W8 — wine-l0 «تولتیپ، اسکرو، آیکون، کادر» — Wine-contract completion wave

Session: 2026-11-02 · branch `wine-l0` · loader HEAD after this wave (see git log)
Pinned contract source: wine-9.0 (`wine-mirror/wine` tag `wine-9.0`), files fetched
to `workspace/wine-ref/`: `dlls/comctl32/tooltips.c`, `dlls/comctl32/toolbar.c`,
`dlls/user32/scroll.c`, `include/commctrl.h`.

## User report (Persian, verbatim mapping)
1. «نمیتونی تولتیپ ها رو لود بکنی تو منو ها» → tooltips never load anywhere
   (menus/toolbars/windows).
2. «بررسی بکن ببین ایکون ها رو و کادر ها دکمه ها رو اسکرو ها رو چطوری کامل لود
   میکنه منبع اصلی» → extract from the Wine SOURCE how icons, boxes/frames,
   buttons, scrollbars FULLY load.
3. «مشکل کامل رو کامل کامل حل بکن تا سایر برنامه های ویندوز هم اجرا بشه» → fix
   generically, not per-app.
4. «10 تا 15 برنامه ساده ویندوزی جدیدی رو تست بکن» → 15 NEW simple Windows
   programs built and tested.
5. «الگوشون رو از وینه استخراج بکن» → each behavior traced to Wine source lines.
6. «پلاگین سطح 0 داریم ادامه بده» → L0 engine used for capture + determinism.

## Root causes found (differential, Wine-contract vs engine)
| # | Root cause | Wine contract | Fix |
|---|---|---|---|
| 1 | `MwChild.cls[16]` snprintf-truncated every class name ≥16 chars: `"tooltips_class32"` (16), `"msctls_progress32"`/`"msctls_trackbar32"` (17) stored as 15+NUL → EVERY `cls_is`/`strcmp` compare failed silently | n/a (engine-internal) | `cls[24]` |
| 2 | ZERO TTM_\*/TTN_\* routing — TTM_ADDTOOL returned 0, no tool ever loaded, no TTN_NEEDTEXT/SHOW/POP ever fired | tooltips.c L360-470 (GetDispInfo), L836 (Show), L852 (Hide), L1486-1545 (Relay) | full tooltip engine (see below) |
| 3 | `SetScrollInfo` read nPos from offset 16 = **nPage** (wrote PAGE into POS); `GetScrollInfo` always answered an empty zeroed range; `Set/GetScrollRange` + `GetScrollPos` used a parallel store the Info family never saw | winuser.h SCROLLINFO x64: cbSize@0 fMask@4 nMin@8 nMax@12 nPage@16 nPos@20 nTrackPos@24 | single `g_scroll` store, correct offsets, page support, trackpos mirror |
| 4 | `TTM_RELAYEVENT` MSG parse: POINT read at +32 = `time` field; POINT is 4-aligned → actually at +36 | winuser.h tagMSG x64: hwnd@0 message@8 wParam@16 lParam@24 time@32 pt@36 | MW_MSG_PT_OFF 36 |
| 5 | A-path `WM_SETTEXT` widen loop wrote `tmp[i]` while reading `((char*)tmp)[i]` — self-clobbering ("line one…" stored as "l") | n/a | separate byte buffer |
| 6 | A-path sysclass table had NO `msctls_progress32` (W table only, SW-1360b) — every A-created progress fell to the unmatched-child stub | n/a | entry added |
| 7 | LB_ADDSTRING read the string from wParam; winuser.h says lParam | winuser.h LB_ADDSTRING | lp |
| 8 | BM_SETCHECK (0xF1) unhandled → checkbox/radio state never moved | wine button.c BM_SETCHECK | stored, returns 0 |
| 9 | CB_SETCURSEL/GETCURSEL hardcoded 0 | winuser.h | real `cb_cursel` store (plain COMBOBOX + ComboBoxEx32) |
| 10 | SS_ICON/SS_BITMAP/SS_\*FRAME/SS_\*RECT/BS_GROUPBOX/BS_ICON styles never decoded by the compositor (v1 drew text for every static; default 3D box for every button) | winuser.h SS_\*=0x3..0x12, BS_GROUPBOX=7 | full style decode + icon blit via `mw_icon_lookup` (the same plane WinRAR uses) + real groupbox/checkbox/radio painters |
| 11 | SCROLLBAR control children painted nothing (default box); no WM_VSCROLL/HSCROLL contract | scroll.c SCROLL_DrawArrows/DrawInterior | arrows/track/thumb painter from g_scroll state + SB_\* code adjustments + parent re-notify (EDIT scrolls silently) |

## Tooltip engine (SW-1591) — Wine lines implemented
- TOOLINFOA/W x64 (commctrl.h L1810-1836): cbSize@0 uFlags@4 hwnd@8 uId@16
  rect@24 hinst@40 lpszText@48 lParam@56.
- TTM_ADDTOOL A(0x404)/W(0x432) with replace-by-(hwnd,uId), direct text,
  IS_INTRESOURCE→LoadStringW, LPSTR_TEXTCALLBACK deferred.
- TTM_NEWTOOLRECT, DELTOOL, SET/GETTOOLINFO, UPDATETIPTEXT, GETTEXT,
  ENUMTOOLS, GETCURRENTTOOL, HITTEST (TTHITTESTINFO x64), GETTOOLCOUNT,
  ADJUSTRECT, GETBUBBLESIZE, WINDOWFROMPOINT, SET/GETMARGIN, SET/GETTIPCOLOR,
  SETDELAYTIME/GETDELAYTIME (500ms default), SETTITLE/GETTITLE, MAXTIPWIDTH,
  ACTIVATE, POP, POPUP, UPDATE, TRACKPOSITION.
- TTM_RELAYEVENT: WM_MOUSEMOVE→tool-from-point→show (deterministic immediate
  ID_TIMERSHOW, traced); button-down relays hide (wine L1497-1502).
- TTN_SHOW (-521)/TTN_POP (-522) delivered to the TOOL hwnd via WM_NOTIFY
  (tooltips.c L814/L850); toolbar-owned tools consume them internally exactly
  as wine toolbar does (toolbar.c L6355 TOOLBAR_TTGetDispInfo answers
  TTN_GETDISPINFO from the button text pool).
- TTN_GETDISPINFO A(-520)/W(-530) with NMTTDISPINFO x64 {hdr 24B,
  lpszText→szText@24, szText[80]@32, hinst@112, uFlags@120, lParam@128};
  reply forms honored: szText fill / direct pointer / INTRESOURCE
  (LoadStringW) / LPSTR_TEXTCALLBACK = recursive→empty (wine L409) / empty →
  retry on GetParent(tool hwnd) (wine L418-421).
- Popup compositor: text-sized white panel, 1px 0x454545 border, soft shadow,
  below-right of the relay point with flip-up at the bottom edge, TTM_SETTITLE
  header, topmost dump `frame "tooltip"`.

## The 15 new programs (tests/controls/, built by scripts/build_ctl_battery.sh)
One source `ctl_battery.c`, 15 binaries ctl01..ctl15.exe (one per -DCTL_MODE):
tooltip_direct / tooltip_callback / tooltip_toolbar / tooltip_menu /
tooltip_track / scrollbar_h / scrollbar_v / buttons / static_frames /
static_icon / button_icon / listbox / combobox / edit_scroll / progress_track.

## Results (artifacts/wine-l0-w8/ctl_l0_summary.tsv)
- 15/15 programs, **137 checks passed, 0 failed** (guest-reported, deterministic).
- Event-stream determinism: 3 runs x 15 programs, sha256 of the normalized
  [TIP]/[SCRL]/[LB]/[PBM]/[TBM]/[UDM]/[SSICON] engine-event stream — **3x/3x
  identical** per program.
- G1 regression gate: 12/12 PASS on the same engine build.
- Smoke: 7za 26.04 exit 0 with correct version output on the new engine.
- Visual evidence (rendered frames, cropped): ctl01 tooltip popup "Save
  changes"; ctl06 hscrollbar arrows/track/thumb; ctl15 segmented progress 42% +
  trackbar thumb 25/50 + updown arrows; ctl10 SS_ICON stock icons blitted.

## Honest limitations
- Wine-live capture (wine-9.0 relay via l0_tracer.py) remains BLOCKED in this
  sandbox: no wine binary. The L0 engine-event capture above uses the MiniWin
  backend (`api_trace.json`), the same evidence channel as SW-TRACK2.
- SS_BITMAP renders only when the HBITMAP decode lands in the icon plane;
  resource DIB bitmaps blit, arbitrary file bitmaps honestly do not yet.
- TTN_NEEDTEXT fallback-to-parent is implemented; LoadString-from-reply uses
  the engine string table (RT_STRING blocks).
