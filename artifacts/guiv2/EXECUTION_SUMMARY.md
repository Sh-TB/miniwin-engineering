# SW-GUI7/8/9 — Universal icon engine + modern chrome + template dialogs
Date: 2026-11-01 · Base HEAD: 242af84 (SWEEP-030) · Guest: `minwin_loader` (gcc, -Ttext-segment=0x2000000)

## User findings addressed (Persian originals)
1. «ایکن ها فقط تو winrar لود شده و نه کامل» → icons were stub handles; now a REAL
   decode+blit pipeline (see below), proven on ProduKey/CurrPorts/7z-SFX frames.
2. «انگار بردی تو ویندوز 98» → Win95 gray palette + 8x8 font replaced by the
   modern syspalette (#F0F0F0/#ADADAD/#0078D7) + AA DejaVu faces + Win10-class
   window chrome (title bar w/ app icon, caption glyphs, menu bar).
3. «idm رو هم اجرا بکن» → idman642build2.exe downloaded (12,698,904 B); loader
   rejects PE32 (magic 0x10b) — **BLOCKED: engine is x64-only** (honest).
4. «برنامه پورتابل رایگان داشت» (portable suites) → PortableApps-style suites
   ship NSIS x86 installers → same x86 BLOCKED. x64 icon-rich singles were
   tested instead (ProduKey, CurrPorts, 7-Zip SFX, RAMMap, DiskMon, PuTTY).

## Landed (all guest-visible contracts)
- SW-GUI7: AA proportional font (3 faces, 4bpp, row-packed) — TextOut A/W,
  DrawText A/W, GetTextExtentPoint32 A/W now real metrics; HFONT weight→bold.
- SW-GUI8: ARGB icon registry; decoders: DIB icons (1/4/8/24/32bpp + AND-mask
  + 32bpp alpha) AND PNG icons (own RFC1951 inflate + unfilter, no deps);
  LoadIcon A/W (RT_GROUP_ICON best-size + drawn modern IDI_* set),
  LoadImage A/W (resource icons + .ico files), CreateIconFromResourceEx,
  ExtractIconExW (own-module enumeration; synth shell set for external Dlls),
  CopyImage rescale, DrawIcon/DrawIconEx/ImageList_Draw(Ex) alpha blits.
- ImageList: real per-pixel alpha plane; Add/AddMasked/ReplaceIcon/SetImageCount/
  GetIcon/GetIconSize real.
- SW-GUI9 compositor (dump-time only, guest pixels untouched): title bar
  (WM_SETICON/class icon/exe name), menu bar from RT_MENU parse (LoadMenu A/W
  now real), children: TOOLBAR (buttons captured via CreateToolbarEx +
  TB_ADDBUTTONS/INSERT/SETSTATE + TB_SETIMAGELIST/TB_ADDBITMAP + generic
  strip autobind w/ modal-color mask), STATUSBAR (SB_SETTEXT/SETPARTS/SIMPLE),
  SysListView32 (header labels+widths from LVM_INSERTCOLUMN capture, rows from
  the SWEEP-019 item model), BUTTON/STATIC/EDIT/LISTBOX/COMBOBOX/SCROLLBAR.
- Blocking pump (MW_GUI_PUMP_BLOCK, MW_GUI_MAX_FRAMES) — iterative, spin-guarded;
  queued WM_SIZE before first WM_PAINT (synchronous variant crashed putty —
  kept as a documented negative result).
- DLGTEMPLATE **and DLGTEMPLATEEX** walker (bounds-checked; EX signature
  0xFFFF/0x0001) — 7z SFX wizard renders: label+edit+browse+Install/Cancel.
- LoadString A/W real (RT_STRING), lstrcatW, w3_res_cmp_name UTF-16 numeric
  hotfix (guest wchar is 2 bytes; old 4-byte walk marched off the stack).

## Evidence
- g1: evidence/master/g1/20261101_155044 (PIN OK, regression 12/12, sha256 6c819c1b…)
- console corpus x2 deterministic: artifacts/guiv2/sweep/sweep_guiv2.tsv
  (17 programs: 11×exit0, pageant 124 daemon, procexp 139/ winmerge 133 known
  frontiers, zoomit64/7z/idm honest nonzero — unchanged vs SWEEP-030 profile)
- GUI frames (MW_GUI_PUMP_BLOCK=1, MAX_FRAMES=10): miniwin-results/<exe>/screenshots
  ProduKey (toolbar 7 icons + menu bar + listview header + statusbar),
  cports (full-color toolbar + menus + 2-part statusbar), 7z SFX wizard,
  RAMMap EULA, DiskMon, PuTTY config (self-painted; engine children excluded).

## Known limits / honesty
- Menu bar renders top-level labels; popup drop-downs are not interactive.
- ZoomIt64 exits 1 before window creation (needs diagnostics — queued).
- procexp64/were + winmergeu 133 remain the recorded SWEEP-030 frontiers.
- IDM/portable-suite x86 guests: BLOCKED (no PE32 support in the engine yet).
