#!/usr/bin/env python3
# SWEEP-029: splice the wagon batch into loader.c
# 1. impl block before g_import_table
# 2. ENTRY lines before table close
# 3. SW-1103: replace the no-op RtlCaptureContext stub (moved to impl block)
# 4. winmergeu SHELL32/SHLWAPI raw ordinals -> ole_ord map
import sys

LOADER = 'src/loader/loader.c'
IMPL = open('scripts/sweep029_impl_a.c', encoding='utf-8').read()

ENTRIES = '''    /* -- SWEEP-029 wagon batch: SW-1103 capture fix + bginfo64 54-tail
     *    + winmergeu SHELL32/SHLWAPI raw-ordinal tail (wine spec names) -- */
    ENTRY("GDI32.DLL", "AlphaBlend",                            mw_AlphaBlend),
    ENTRY("MSIMG32.DLL", "AlphaBlend",                          mw_AlphaBlend),
    ENTRY("USER32.DLL", "CharUpperBuffW",                       mw_CharUpperBuffW),
    ENTRY("ADVAPI32.DLL", "CloseEventLog",                      mw_CloseEventLog),
    ENTRY("WINSPOOL.DRV", "ClosePrinter",                       mw_ClosePrinter),
    ENTRY("USER32.DLL", "CopyIcon",                             mw_CopyIcon),
    ENTRY("GDI32.DLL", "CreateDIBitmap",                        mw_CreateDIBitmap),
    ENTRY("KERNEL32.DLL", "CreateEnvironmentBlock",             mw_CreateEnvironmentBlock),
    ENTRY("GDI32.DLL", "CreateHatchBrush",                      mw_CreateHatchBrush),
    ENTRY("OLE32.dll", "CreateILockBytesOnHGlobal",             mw_CreateILockBytesOnHGlobal),
    ENTRY("GDI32.DLL", "CreatePolygonRgn",                      mw_CreatePolygonRgn),
    ENTRY("GDI32.DLL", "CreateRoundRectRgn",                    mw_CreateRoundRectRgn),
    ENTRY("ADVAPI32.DLL", "CreateServiceW",                     mw_CreateServiceW),
    ENTRY("USER32.DLL", "DestroyAcceleratorTable",              mw_DestroyAcceleratorTable),
    ENTRY("KERNEL32.DLL", "DestroyEnvironmentBlock",            mw_DestroyEnvironmentBlock),
    ENTRY("GDI32.DLL", "ExtFloodFill",                          mw_ExtFloodFill),
    ENTRY("GDI32.DLL", "ExtSelectClipRgn",                      mw_ExtSelectClipRgn),
    ENTRY("GDI32.DLL", "FillRgn",                               mw_FillRgn),
    ENTRY("GDI32.DLL", "FrameRgn",                              mw_FrameRgn),
    ENTRY("GDI32.DLL", "GetBoundsRect",                         mw_GetBoundsRect),
    ENTRY("GDI32.DLL", "GetNearestPaletteIndex",                mw_GetNearestPaletteIndex),
    ENTRY("USER32.DLL", "GetNextDlgGroupItem",                  mw_GetNextDlgGroupItem),
    ENTRY("GDI32.DLL", "GetObjectType",                         mw_GetObjectType),
    ENTRY("GDI32.DLL", "GetPaletteEntries",                     mw_GetPaletteEntries),
    ENTRY("USER32.DLL", "GetProcessWindowStation",              mw_GetProcessWindowStation),
    ENTRY("USER32.DLL", "GetWindowRgn",                         mw_GetWindowRgn),
    ENTRY("IMM32.dll", "ImmGetOpenStatus",                      mw_ImmGetOpenStatus),
    ENTRY("GDI32.DLL", "InvertRect",                            mw_InvertRect),
    ENTRY("USER32.DLL", "IsCharLowerW",                         mw_IsCharLowerW),
    ENTRY("KERNEL32.DLL", "K32EnumProcessModules",              mw_K32EnumProcessModules),
    ENTRY("KERNEL32.DLL", "K32EnumProcesses",                   mw_K32EnumProcesses),
    ENTRY("KERNEL32.DLL", "K32GetModuleBaseNameW",              mw_K32GetModuleBaseNameW),
    ENTRY("ADVAPI32.DLL", "LsaQueryInformationPolicy",          mw_LsaQueryInformationPolicy),
    ENTRY("USER32.DLL", "MapVirtualKeyExW",                     mw_MapVirtualKeyExW),
    ENTRY("USER32.DLL", "MapVirtualKeyW",                       mw_MapVirtualKeyW),
    ENTRY("NETAPI32.DLL", "NetServerGetInfo",                   mw_NetServerGetInfo),
    ENTRY("NETAPI32.DLL", "NetWkstaGetInfo",                    mw_NetWkstaGetInfo),
    ENTRY("NETAPI32.DLL", "NetWkstaUserGetInfo",                mw_NetWkstaUserGetInfo),
    ENTRY("USER32.DLL", "NotifyWinEvent",                       mw_NotifyWinEvent),
    ENTRY("GDI32.DLL", "OffsetWindowOrgEx",                     mw_OffsetWindowOrgEx),
    ENTRY("OLE32.dll", "OleLockRunning",                        mw_OleLockRunning),
    ENTRY("ADVAPI32.DLL", "OpenEventLogW",                      mw_OpenEventLogW),
    ENTRY("WINSPOOL.DRV", "OpenPrinterW",                       mw_OpenPrinterW),
    ENTRY("WINMM.DLL", "PlaySoundW",                            mw_PlaySoundW),
    ENTRY("ADVAPI32.DLL", "ReadEventLogW",                      mw_ReadEventLogW),
    ENTRY("ADVAPI32.DLL", "RegisterServiceCtrlHandlerW",        mw_RegisterServiceCtrlHandlerW),
    ENTRY("SHELL32.DLL", "SHAppBarMessage",                     mw_SHAppBarMessage),
    ENTRY("ADVAPI32.DLL", "SaferCloseLevel",                    mw_SaferCloseLevel),
    ENTRY("ADVAPI32.DLL", "SaferComputeTokenFromLevel",         mw_SaferComputeTokenFromLevel),
    ENTRY("ADVAPI32.DLL", "SaferIdentifyLevel",                 mw_SaferIdentifyLevel),
    ENTRY("GDI32.DLL", "SetGraphicsMode",                       mw_SetGraphicsMode),
    ENTRY("GDI32.DLL", "SetPixelV",                             mw_SetPixelV),
    ENTRY("ADVAPI32.DLL", "SetServiceStatus",                   mw_SetServiceStatus),
    ENTRY("USER32.DLL", "SetSysColors",                         mw_SetSysColors),
    ENTRY("GDI32.DLL", "SetWorldTransform",                     mw_SetWorldTransform),
    ENTRY("SHELL32.DLL", "SHCreateDirectory",                   mw_SHCreateDirectory),
    ENTRY("SHELL32.DLL", "CDefFolderMenu_Create2",              mw_CDefFolderMenu_Create2),
    ENTRY("SHLWAPI.DLL", "ParseURLW",                           mw_ParseURLW),
    ENTRY("SHLWAPI.DLL", "SHCreateMemStream",                   mw_SHCreateMemStream),
'''

txt = open(LOADER, encoding='utf-8').read()
if 'SWEEP-029 wagon batch' in txt:
    print('already applied'); sys.exit(0)

# 3. SW-1103: retire the no-op RtlCaptureContext stub (definition now in the
#    impl block with a REAL capture). Exact-string replace, asserted.
_old_ctx = ('/* SEH: RtlCaptureContext stub */\n'
            '__attribute__((ms_abi)) void mw_RtlCaptureContext(void* ctx) {\n'
            '    MW_TRACE("RtlCaptureContext()");\n'
            '}')
_new_ctx = ('/* SWEEP-029 (SW-1103): RtlCaptureContext moved to the impl block with a\n'
            ' * REAL machine capture. The old no-op left ctx.Rip=0; every guest\n'
            ' * consumer (mingw-w64 __report_gsfailure / gs_report) then ran\n'
            ' * RtlLookupFunctionEntry(pc=0)->NULL -> UnhandledExceptionFilter ->\n'
            ' * TerminateProcess(0xC0000409) — the winmergeu exit-9 tail. */')
assert _old_ctx in txt, 'RtlCaptureContext stub anchor missing'
txt = txt.replace(_old_ctx, _new_ctx, 1)

# 1. impl block before the import table definition
marker = 'static ImportEntry g_import_table[] = {'
i = txt.index(marker)
j = txt.rfind('\n', 0, i)
txt = txt[:j] + '\n' + IMPL + '\n' + txt[j:]

# 2. ENTRY lines before the table close (first "};\n" after marker)
k = txt.index(marker)
end = txt.index('};', k)
txt = txt[:end] + ENTRIES + txt[end:]

# 4. SWEEP-029: winmergeu binds SHELL32/SHLWAPI BY ORDINAL (pefile dump:
#    SHELL32 155/701, SHLWAPI 2/12). Names cross-checked against
#    wine-mirror spec files (shlwapi.spec, shell32.spec) 2026-10-20:
#    SHELL32.155 = SHCreateDirectory, SHELL32.701 = CDefFolderMenu_Create2,
#    SHLWAPI.2 = ParseURLW, SHLWAPI.12 = SHCreateMemStream.
_ord_old = '                    { "OLEAUT32", 202, "CreateErrorInfo" },'
_ord_new = (_ord_old + '\n'
    '                    /* SWEEP-029: winmergeu SHELL32/SHLWAPI raw ordinals */\n'
    '                    { "SHELL32", 155, "SHCreateDirectory" },\n'
    '                    { "SHELL32", 701, "CDefFolderMenu_Create2" },\n'
    '                    { "SHLWAPI", 2,   "ParseURLW" },\n'
    '                    { "SHLWAPI", 12,  "SHCreateMemStream" },')
assert _ord_old in txt, 'ordinal anchor missing in loader'
txt = txt.replace(_ord_old, _ord_new, 1)

open(LOADER, 'w', encoding='utf-8').write(txt)
n = ENTRIES.count('ENTRY(')
print(f'applied: impl {len(IMPL)} bytes, {n} ENTRY lines, ctx stub retired, 4 ordinals mapped')
