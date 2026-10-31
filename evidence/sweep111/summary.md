# SWEEP-111 — Toolbar registry-state seed + real shell file-info (the "how does Wine get past this" round)

Binary sha256 `41e7036be939ae6e2f63fa9947cbba6ea5362db23632f21da7a0b61be3e57fac`.
G1 PIN 12/12 (evidence/master/g1/20261030_175415). corpus_gate sweep111: 15 rows,
rc=0 in 9, crash total 0 — ZERO regressions. All four named-program batteries
reproduce their pinned states (winrar EXIT rc=0 x3, winrarinstd ALIVE_PUMP rc=124
x3 with the seed consumed, npp64 PASS_INPUT_STATE_CHANGE x3 frames=28 uniq=13,
idman rc=9 x3 deterministic).

## 1. SW-1546 — the toolbar registry-state seed (issue #26 discriminating experiment, CONFIRMED)

Round-1/2 open question (issue #26): the installed WinRAR decodes all 21 toolbar PNG
glyphs but never sends TB_ADDBUTTONSW; the guest aborted the toolbar-init after
`HKCU\Software\WinRAR\General\Toolbar\{Layout,Buttons}` both missed, then parked in
the WM_TIMER loop. On a real box those keys exist because WinRAR's own first GUI run
writes them — a Wine prefix after one GUI session has them too.

**Decode (guest strings + disasm, measured):** the binary ships the exact registry
model — button ids `add exto test view del find prn wiz cvt info exit rep extr vir
cmt prot lock SFX rpt bnc`, value templates `f.<id>` / `a.<id>` / `f.<id>.sep` on
`General\Toolbar\Buttons`, `Band%d_%d` rebar bands + `ViewSmall`/`ViewMain` size
flags on `Layout`/`Toolbar`. Xrefs to `General\Toolbar\Buttons` cluster at
0x140148a10..0x140149678; the toolbar-setup function (0x140147d1c) sends
BUTTONSTRUCTSIZE -> SETBUTTONSIZE -> SETBITMAPSIZE -> (button add), and its dispatch
caller (0x1401458f2 loop) selects per-band handlers.

**Experiment (SW-1546):** seed `Buttons` (40 values: f.* + a.* per id, REG_DWORD=1)
+ `Layout` (f.* set) into the MWREG001 snapshot via `scripts/sweep111_seed.py`
(reuses the regseed_extend MWREG001 serializer). Result, measured on the seeded run
(evidence/sweep111/seedexp + evidence/111/winrarinstd):

- the guest **opens the Buttons key successfully** and issues **80 value reads**
  (every `f.<id>` and `a.<id>` once) — the abort/re-probe loop is GONE
- separator probes (`a.bnc.sep`) miss benignly, exactly like real Windows
- flow proceeds: 21 PNG glyph decodes -> HBITMAPs -> child TOOLBAR id=12/13/15
  created -> TB_ENABLEBUTTON(0x401) x23 pass runs
- headless variant with events disabled reaches a clean `ExitProcess(0)` (was:
  park-until-timeout) — init now COMPLETES
- remaining frontier (named): the guest's DEFAULT band-layout path reads
  `Band76_0` + `ViewSmall` and still never issues ImageList_Create /
  TB_ADDBUTTONSW headless — the button-add is gated behind that decode
  (variant B with a seeded `Band76_0=1` changes the flow but not to the add,
  so the Layout value semantics need their own round)

**Integration:** `reg_seed_extend` manifest key in `scripts/plugin_run.sh` (SW-1546b)
+ `plugins/winrarinstd/plugin.tsv` now carries `reg_seed_extend scripts/sweep111_seed.py`
— the battery itself stages the "prefix with one GUI run behind it" state.

## 2. SW-1547 — SHGetFileInfoW: a REAL shell icon DB (issue #10 FILE-2)

Before: every call returned an honest-0 "no shell icon DB" — WinRAR's file panel had
NO per-file icons and no type names. Now (wine parallel: shell32/sic.c
SIC_Initialize/SIC_GetIconIndex):

1. `HKCR\<ext>\DefaultIcon` (Default) probe via the live hive (honored when
   provisioning writes it; probe wired + traced)
2. otherwise a per-class 32bpp glyph synthesized with the SW-1544 raster helpers:
   folder / drive / text / archive ("book") / app / image / generic file, cached
   per class
3. SHFILEINFOW (x64 layout: hIcon@0, iIcon@8, dwAttributes@12, szDisplayName@16,
   szTypeName@536) filled per SHGFI_* flags: ICON(0x100), DISPLAYNAME(0x200),
   TYPENAME(0x400), ATTRIBUTES(0x800), ICONLOCATION(0x1000), SYSICONINDEX(0x4000);
   SHGFI_USEFILEATTRIBUTES(0x10) honored (no stat, attrs trusted)
4. SHGetFileInfoA same contract (A path upcast); extension table covers
   rar/r00/rev/zip/7z/tar/... (archive), txt/ini/log/xml/... (text),
   bmp/png/jpg/... (image), exe/dll (app)

Measured (evidence/111/winrarinstd trace): WinRAR's four drive-root queries now
resolve `[SHELL-INFO] class=Local Disk` with real HICONs (was `-> 0 (no shell icon
DB, honest failure)` x4).

## 3. SW-1548 — SHGetFolderPathW/A: CSIDL_DESKTOPDIRECTORY(0x10) + CSIDL_DESKTOP(0)

WinRAR 7.12's first-run scan probes csidl=0x10 and previously got `E_FAIL
(unmapped)`; both W and A tables now answer `C:\Users\MiniWin\Desktop` (S_OK) —
verified in the battery trace.

## Honesty notes

- The toolbar BUTTON-ADD (TB_ADDBUTTONSW) is still not reached headless: the default
  band-layout decode (`Band76_0` semantics, ViewSmall/ViewMain flags) is the named
  next frontier. What landed is the unblock of the whole toolbar-init + registry
  state consumption + the full PNG glyph chain, measured end-to-end.
- The synthesized file icons are per-CLASS glyphs, not per-file extracted icons;
  the HKCR DefaultIcon probe is wired so real provisioning fills them later.
- xp.* SEED corpus rows remain MISSING (pre-existing, owed).
- P01-P20 puzzle battery guests are not fetched in this environment
  (artifacts/sweep074/dl absent) — environment gap, not a regression; not part of
  the SWEEP-110 gate set.

Evidence: evidence/sweep111/{API_PARITY.md,seedexp/}, evidence/111/{winrar,
winrarinstd,idman,npp64}/, evidence/master/g1/20261030_175415.
