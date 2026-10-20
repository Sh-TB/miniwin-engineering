# SWEEP-029 «قانون کلی» — cheetah/owl/elephant/glowworm/train (2026-10-20)

The standing law, honored: inspect fast, see the tiny flaw, carry many,
light bugs in the dark, pull dozens of wagons in one run.

## Engine root-cause fix (SW-1103) — the owl-eye catch
- `mw_RtlCaptureContext` was a NO-OP stub: guest consumers read a zeroed
  CONTEXT (ctx.Rip=0). The winmergeu exit-9 fail-fast chain ended exactly
  there: gs_report -> RtlLookupFunctionEntry(pc=0)->NULL ->
  UnhandledExceptionFilter -> TerminateProcess(0xC0000409).
- NOW: real capture (integer regs, segments, EFlags, MxCsr, Rip=caller RA,
  Rsp=entry+8 documented approximation; SSE/debug regs zeroed v1).
- Post-fix winmergeu: gs_report walks RtlVirtualUnwind 2 frames with REAL
  rip=0x140318939 before the genuine /GS cookie failure terminates.
  The remaining exit 9 is a real guest-side frontier (SW-1151), not ours.

## Wagon batch (67 ENTRY lines, 2 splices)
1. **bginfo64 54-tail -> 0** (SWEEP-028 "next wagon"):
   GDI raster REAL: InvertRect (pixel XOR), FillRgn/FrameRgn (region fill
   through brush tables), SetPixelV, CreateDIBitmap (blank surface v1),
   CreatePolygonRgn/CreateRoundRectRgn (bbox v1), CreateHatchBrush (solid v1),
   GetObjectType (handle-range map), AlphaBlend->StretchBlt SRCCOPY,
   GetBoundsRect DCB_RESET, ExtSelectClipRgn SIMPLEREGION.
   USER32: MapVirtualKeyW/ExW, CharUpperBuffW REAL, IsCharLowerW REAL,
   GetWindowRgn NULLREGION, NotifyWinEvent, SetSysColors, DestroyAcceleratorTable,
   GetProcessWindowStation, GetNextDlgGroupItem.
   K32 REAL /proc enumeration: K32EnumProcesses (opendir /proc),
   K32EnumProcessModules (image base), K32GetModuleBaseNameW (exe basename).
   Net* models: NetWkstaUserGetInfo 0/1, NetWkstaGetInfo 100/101,
   NetServerGetInfo 101 (PLATFORM_ID_NT 500, 10.0, MINIWIN/WORKGROUP).
   Cookies/typed: printer pair, event-log trio (ReadEventLogW EOF),
   services (CreateServiceW ACCESS_DENIED, CtrlHandler store, SetServiceStatus),
   Safer identity pipeline, LSA NOT_IMPLEMENTED, env-block pair,
   Imm/SHAppBar/OleLockRunning/CreateILockBytesOnHGlobal/PlaySoundW.
2. **winmergeu 4 nameless ordinals**: SHELL32.155 SHCreateDirectory (real
   mkdir), SHELL32.701 CDefFolderMenu_Create2 (E_FAIL), SHLWAPI.2 ParseURLW
   (real scheme strip), SHLWAPI.12 SHCreateMemStream (typed NULL).
3. **bginfo64 ODBC32 ordinals 9/24/31/75/136/141** = SQLDescribeCol/
   SQLAllocHandle/SQLFreeHandle/SQLSetEnvAttr/SQLGetDiagRecW(NO_DATA)/
   SQLSetConnectAttrW — typed-honest, no SQL engine.
4. **winmergeu OLEAUT32 ordinals 25/114/418** = SafeArrayPutElement(S_OK)/
   VarBstrFromDate(real BSTR)/DllUnregisterServer(S_OK).
- All ordinal names from wine-mirror spec files (shlwapi/shell32/oleaut32/
  odbc32), fetched 2026-10-20 — evidence-first discipline.

## Census (pump-off, this binary, x3)
| program | exit (x3) | unresolved | note |
|---|---|---|---|
| bginfo64 | 0,0,0 | **0,0,0** (was 54) | stderr byte-identical x3 |
| winmergeu | 9,9,9 | **0,0,0** (was 4) | gs fail-fast frontier SW-1151 |
| autoruns64 | 0,0,0 | 0,0,0 | |
| coreinfoex64 | 0,0,0 | 0,0,0 | |
| cports | 0,0,0 | 0,0,0 | |
| diskmon64 | 0,0,0 | 0,0,0 | |
| produkey | 0,0,0 | 0,0,0 | |
| putty | 0,0,0 | 0,0,0 | |
| puttygen | 0,0,0 | 0,0,0 | |
| procmon64 | 0,0,0 | 0,0,0 | |
| rammap64 | 0,0,0 | 0,0,0 | |
| searchmyfiles | 0,0,0 | 0,0,0 | |
| pageant | 124,124,124 | 0,0,0 | daemon mode (documented) |
| procexp64 | 139,139,139 | 0,0,0 | BUG-078 frontier; stderr ASLR addr diff pre-existing (SWEEP-028 same) |
| zoomit64 | 1,1,1 | 0,0,0 | SW-1106 trace-decode queued |
| adexplorer64 | - | - | retired upstream (404), no exe |

**ZERO unresolved across the entire corpus** — first time in project history.

## Gates
- G1 PIN OK sha 1c8e23f7 regression 12/12 (evidence/master/g1/20261020_113847)
- corpus x3 15/15 deterministic exits, ZERO regressions
- Loader 2,497,432 -> 2,698,328 bytes

## Next wagons
SW-1151 winmergeu /GS cookie decode; SW-1102 procexp64; SW-1106 zoomit64
exit1; SW-1153 ODBC mini-driver (sqlite host).
