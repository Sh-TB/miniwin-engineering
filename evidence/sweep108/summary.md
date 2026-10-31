# SWEEP-108 — the three named programs pass their next frontier + the GUI content plane (user: «این 3 تا رو اجرا کامل بکنی و تصویر گرافیکی رو لد بکنی»)

Date: 2026-10-30 · Base: origin/main ac57a4f ·
Final binary sha256 2bb1586affa50b4703a2c32b747b217504fc1501d7fc9bacb31bbecce70a5a49
(G1 PIN 12/12, evidence/master/g1/20261030_113944; corpus 15 rows rc=0 in 9 crash 0 — zero regressions)

## Round goal (user instruction translated)
"Continue — you must be able to FULLY run these 3 [WinRAR / Notepad++ / IDM] and
load the graphical image." Method unchanged: Wine as the behavioral contract,
every fix measured on the real binary, before/after evidence.

## Battery (sweep108, serial)

| program | verdict | evidence |
|---|---|---|
| winrar (official SFX installer) | **INSTALL_OK rc=0** — 28 files / 9.2 MB into `sandbox/Program Files/WinRAR/`, byte-identical WinRAR.exe (sha 7415b18099, re-verified) | install_log 32 ops all rc=1 |
| winrarinstd (installed GUI) | **ALIVE_PUMP_AT_TIMEOUT rc=124 ×5 runs** — full control tree (rebar toolbar + combo + toolbar + listview 4 columns + statusbar), real menu band rendered, pump alive at the 90 s timeout | frame_001.png (menu: File Commands Tools Favorites Options Help) |
| npp64 (Notepad++ 8.9.8.1 x64) | **PASS_INPUT_STATE_CHANGE frames=35 uniq=16** — zero regressions from the path/cwd plane rebuild | plugins/status.tsv |
| idman (official 6.42 build 2) | PASS_LAUNCH_RENDER rc=0 frames=1 — real "IDM Setup" dialog rendered (temp-dir frontier SW-1401 unchanged) | messagebox_1.png |
| corpus gate | 15 rows, rc=0 in 9, crash total 0 (xp.* honestly MISSING — pre-existing, .cache wiped by the sandbox reset) | evidence/sweep108/corpus_parity.tsv |

## Engine changes (all generic, zero app special-casing) — src/loader/loader.c

- **SW-1515** `w3_normalize_path`: restored the **SW-1234 drive contract** (root-anchored
  `C:\a\b` → `<drive_cwd>/a/b`) that SW-1509 (SWEEP-033) had silently broken by
  making every root-anchored drive path host-ABSOLUTE. That regression killed the
  WinRAR SFX self-reopen (`CreateFileW('C:\Installers\winrar712.exe')` →
  `open("/Installers/...")` ENOENT → "Errors encountered" rc=6) — the installer
  train could not even re-run. The SW-1509 case survives as a measured stat()
  fallback (cwd-relative absent AND host-absolute present → host form; coreinfo64
  unchanged). Plus: `\\?\X:\...` verbatim prefix now re-maps to the same file as
  `X:\...` (WinRAR's long-path fallback; contract wine ntdll/unix/file.c).
- **SW-1516** `GetCurrentDirectoryW/A` unified on the drive-namespace plane: real
  Windows answers `C:\...` (wine kernelbase/path.c); the engine leaked the RAW
  HOST path (`/home/z/.../sandbox`) and the SFX used it as its install
  destination, walking /home creating host dirs. The A variant no longer answers
  the phantom virtual `C:\miniwin` that contradicted the W variant's real chdir.
- **SW-1517** the `CreateFileW u16len` diagnostic probe indexed `wchar_t*`
  (Linux 4-byte!) over a UTF-16 buffer — every logged u16len was fiction
  (u16len=15 for a 27-char path) and misled the decode for an hour. Now
  indexes `uint16_t*`.
- **SW-1518** `FreeLibraryAndExitThread` = `FreeLibrary(mod); ExitThread(code)`
  (wine kernelbase/thread.c) — the old mapping called **mw_ExitProcess**: any
  worker DLL finishing tore down the whole guest. WinRAR GUI's temp-cleanup
  thread (`FindFirstFile('Windows\Temp\Rar$*')` → PATH_NOT_FOUND →
  FreeLibraryAndExitThread(0)) killed the just-painted main window ~1 s after
  launch (rc=0, frames frozen). After the fix: the GUI survives in its pump.
  **SW-1518b** the SW-1240 Windows-skeleton seeding now also runs on the
  enumeration path — `C:\Windows\Temp` exists on every real Windows/Wine prefix;
  the find path returned ERROR_PATH_NOT_FOUND(3) where real Windows answers
  "no matches"(2).
- **SW-1520** the GUI content plane: (a) STATUSBAR content+metrics
  (SB_SETPARTS/GETPARTS/SETTEXT A+W/GETTEXT/GETRECT with the 23 px height
  contract), TOOLBAR content+metrics (TB_ADDSTRING A+W pool, TB_ADDBUTTONS A/W
  + INSERTBUTTON capture, TB_GETBUTTON readback, TB_BUTTONCOUNT,
  TB_SETBUTTONSIZE/GETBUTTONSIZE, TB_GETITEMRECT cumulative real layout so the
  GUEST's own WM_SIZE code positions the row, TB_SETBITMAPSIZE/ADDBITMAP),
  COMBOBOX items (CB_ADDSTRING/GETCOUNT/GETLBTEXT/GETITEMHEIGHT),
  LVM_INSERTCOLUMN header capture; (b) the menu band: the main menu renders as
  a 20 px non-client band and the client plane honors it (GetClientRect,
  WM_SIZE lParam, child rects shift down — wine user32/menu.c contract);
  constants verified against wine include/commctrl.h:1200-1247.
- **SW-1521** `ReBarWindow32`/`msctls_rebar32` → TOOLBAR identity: WinRAR's
  first toolbar container is a rebar; the old unknown-WS_CHILD fallback minted
  it STATUSBAR and its TB_ traffic fell into the SB model. Unmatched child
  classes now log their RAW name (SW-1521 diagnostics).
- **SW-1522** hierarchy-aware registry miss codes: parent missing →
  **ERROR_PATH_NOT_FOUND(3)**, leaf missing → ERROR_FILE_NOT_FOUND(2) — real
  Windows / wine advapi32 semantics. The flat 2 made WinRAR's per-button scan
  (`General\Toolbar\Buttons\*`, 160 opens) read every miss as "this button was
  deleted" instead of "no custom toolbar at all".

## Measured before → after (the same binaries)

| surface | before (SWEEP-107/033-era) | after (SWEEP-108) |
|---|---|---|
| WinRAR SFX install chain | rc=6 "Errors encountered", 0 files (self-reopen ENOENT) | **rc=0, 28 files installed, sha-identical payload** |
| installed WinRAR GUI | SPIN_AT_TIMEOUT frames=0 (SW-1506) → then self-exit rc=0 in ~1 s (SW-1518) | **ALIVE_PUMP rc=124 ×5, deterministic** — menu band + control tree painted |
| WinRAR GUI render content | flat gray face, no menu, no chrome | **real menu texts (File/Commands/Tools/Favorites/Options/Help) from RT_MENU**, sunken listview pane, control chrome |
| Notepad++ x64 | PASS 35/16 | **PASS 35/16 (unchanged, zero regressions)** |
| IDM installer | PASS_LAUNCH_RENDER 1 frame | unchanged + real Setup dialog content |
| G1 / corpus | 12/12, 15 rows | **12/12, 15 rows (identical)** |

## New registered frontiers (honest, not hidden)

- **SW-1523**: WinRAR toolbar buttons still absent — the button-add flow reads
  `General\Toolbar\Layout`/`Toolbar` (miss, honest 3/2) and then scans
  `General\Toolbar\Buttons\*` 160× on its config thread; the main thread's
  TB_ADDBUTTONSW never fires. Needs the guest's toolbar-init decode
  (MW32_STEP_TRACE around the 0x1400-0x1410 region) — the SW-1503 backtrace
  tool is the instrument.
- **SW-1506** (unresolved from SWEEP-107) is OVERTAKEN by SW-1515/1518: the
  CustomExt spin no longer reproduces — the GUI now reaches the pump.

## Gates

- G1 PIN 12/12 (2bb1586a, evidence/master/g1/20261030_113944)
- corpus_gate sweep108: 15 rows, rc=0 in 9, crash total 0 — identical
- no force-push
