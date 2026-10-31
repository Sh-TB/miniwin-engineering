# SWEEP-110 — Icon Parity: Wine vs MiniWin (WinRAR 7.12 GUI, fine-grained)

Method: the level-0 Wine relay profile (`artifacts/sweep033/winrar-gui.wine-dynamic-apis.json`,
873k calls captured under `WINEDEBUG=+relay`, wine-10.0) mined for every icon-family
API, diffed against the SWEEP-110 MiniWin api_trace of the installed WinRAR battery
(`winrarinstd`, ALIVE_PUMP run). This answers "exactly what does Wine load" on the
icon dimension, and where MiniWin stands call-by-call.

## Wine (winrar-gui init window, 602 distinct APIs total)

| Wine call | count |
|---|---|
| `user32.LoadImageW` | 6 |
| `user32.LoadIconW` | 3 |
| **icon-family total** | **9** |

(Headless wine covers process init + window creation — the `nodrv` path. Toolbar
imagelist calls would appear after the first paint cycle in a full session.)

## MiniWin (SWEEP-110, icon plane landed — commit 517416d)

| MiniWin call | count | outcome |
|---|---|---|
| `LoadIconW(id=101)` | 1 | **REAL DECODE**: group 101 walk → RT_ICON id 6 (32x32, 8bpp palette) → HICON 0x62001, 32bpp BGRA in the icon store |
| `LoadIconW` remaining (2 of Wine's 3) | 0 | not reached — guest parks first |
| `LoadImageW` (Wine: 6) | 0 | not reached — guest parks first |

Decode chain (from the api_trace):

```
[RES] find hmod=0x140000000 a=?/- w=0x6/0x3          <- RT_GROUP_ICON(14) id=101
[RES] walk depth=0 level=0x1402a8000 named=1 ided=10
[RES] walk depth=1 level=0x1402a8410 named=0 ided=46
[RES] walk depth=2 level=0x1402a9730 named=0 ided=1
[ICON] RT_ICON decode 32x32 bpp=8 -> HICON 0x62001
[ICON] group id=101 picked RT_ICON 6 (32x32 want 32x32) -> 0x62001
LoadIconW(id=101) -> HICON 0x62001 (real pixels)
```

## Where the missing 8 calls wait

The installed WinRAR parks in a WM_TIMER/registry loop reading
`HKCU\Software\WinRAR\General\Toolbar\Buttons` (ERROR_PATH_NOT_FOUND, repeated every
tick) BEFORE building its toolbar — so the remaining LoadImageW/LoadIconW calls
(toolbar glyph strip, tree icons) never execute. Tracked in issue #26 with the
proposed registry-seed probe. The icon plane (RT_GROUP_ICON walk + RT_ICON DIB
decode + DrawIconEx alpha-blit + ImageList pixels) renders them the moment the
guest reaches that code — no further engine work needed for the decode itself.

## Resource inventory of the guest (winrar 7.12 x64 official)

- RT_GROUP_ICON groups: 6 (ids 101..106; group 101 = 11 sizes 16..48, 8bpp palette + 32bpp)
- RT_ICON icons: 46, ALL DIB format (magic `28 00 00 00` = BITMAPINFOHEADER), zero PNG
- bpp mix: 8 (palette), 32 (alpha); sizes 16/20/24/32/48

## Conclusion

1/9 Wine icon calls reached, and that one now decodes REAL pixels end-to-end
(previously every icon API returned a handle-only cookie). The gap to the remaining
8 is the toolbar construction loop (#26), not the icon machinery.
