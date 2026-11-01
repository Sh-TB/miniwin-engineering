# W7 evidence — SW-1571 WinRAR toolbar: app-generated UI rendered from the app's own art

Binary under test: `minwin_loader` sha256 c2aab144 (G1 PIN 12/12 at
`evidence/master/g1/20261101_092446`).

## What these frames show

`frame_tbar_visible.bmp` (env gate `MINWIN_TBART_DEBUG=1`, dump taken at the
toolbar's own composite step): WinRAR 7.12's OWN toolbar, built end-to-end by
the guest — `CreateWindowExW(ToolbarWindow32 id=12)` by the guest (site
instrumentation `site=0x20f7aa5 parent=0x10001`), `TB_ADDBUTTONSW x10`
(guest-chosen cmd ids 0x190..0x1A5, bitmap indices 0..11), button labels from
the guest's `TB_GETSTRINGW/0x44D` pool, and each button face blitted from the
app's OWN `GdipCreateBitmapFromStream` PNG decodes (48x36 button art from
WinRAR's RT_RCDATA 'PNG' resources). ZERO app-specific hardcoding: the
renderer reads only the engine's TB message model (button array, string pool)
and the ordered registry of decoded GDI+ bitmaps.

`frame_final_settled.bmp` (default battery run, no env gate): the honest
settled state — the file-list pane covers the toolbar strip because the
GUEST's own layout pass computed listview y=24 from a rebar height of 4px
(Wine oracle: rebar 83px @L0-20261101-080928-WinRAR MoveWindow(0x1009c,0,0,952,83)).
Registered as SW-1572 (rebar/combo height model — the guest's height math
reads back the toolbar-area height the engine modeled at 2-4px).

## Toolbar protocol parity (this run vs the Wine 10.1 oracle relay)

| msg | meaning (guest usage) | MiniWin | Wine oracle |
|-----|----------------------|---------|-------------|
| 0x41E | TB_BUTTONSTRUCTSIZE(w=32) | 1 | 1 |
| 0x41F | TB_SETBUTTONSIZE | 1 | 1 |
| 0x420 | TB_SETBITMAPSIZE | 1 | 1 |
| 0x413 | TB_ADDBITMAP per button | 20 | 20 |
| 0x44D | string pool add (returns base idx) | 20 | 20 |
| 0x417 | TB_GETBUTTON readback | 11 | 11 |
| 0x418 | TB_BUTTONCOUNT | 5 | 5 |
| 0x444 | TB_ADDBUTTONSW | 10 + 1 | 10 + 1 |
| 0x41D | TB_ADDSTRINGW | 12 | 12 |

(full-count check: `rg -o 'TBMSG hwnd=0x3000[34] cls=TOOLBAR msg=0x[0-9a-f]+' | sort | uniq -c`)

## Trace

`api_trace.json` — key markers: `[GUI] child TOOLBAR id=12/13` (guest
CreateWindowExW, parent=0x10001), `TBMSG`/`TB_ADDBUTTONS[i] bmp=... cmd=...`
(guest protocol), `[GDI+] CreateBitmapFromStream -> GpBitmap ... (PNG
decoded)` x21, `[TBPAINT] hwnd=0x30003 rect=608x37@0,20 nbtn=10 bw=48 bh=36`.
