#!/usr/bin/env python3
# SWEEP-027: splice the wagon batch into loader.c
# 1. impl block before g_import_table
# 2. ENTRY lines before table close
# 3. _wcmdln UTF-16 globals + data import + main fill
import re

LOADER = 'src/loader/loader.c'
IMPL = open('scripts/sweep027_impl.c', encoding='utf-8').read()

ENTRIES = '''    /* -- SWEEP-027 wagon batch: cross-program unresolved tails -- */
    ENTRY("USER32.DLL", "CreateAcceleratorTableW",              mw_CreateAcceleratorTableW),
    ENTRY("USER32.DLL", "CopyAcceleratorTableW",                mw_CopyAcceleratorTableW),
    ENTRY("USER32.DLL", "DestroyAcceleratorTableW",             mw_DestroyAcceleratorTableW),
    ENTRY("USER32.DLL", "TranslateAcceleratorA",                mw_TranslateAcceleratorA),
    ENTRY("USER32.DLL", "TranslateAcceleratorW",                mw_TranslateAcceleratorW),
    ENTRY("USER32.DLL", "SetCursorPos",                         mw_SetCursorPos),
    ENTRY("USER32.DLL", "GetMenuStringW",                       mw_GetMenuStringW),
    ENTRY("USER32.DLL", "GetMenuState",                         mw_GetMenuState),
    ENTRY("USER32.DLL", "GetMenuCheckMarkDimensions",           mw_GetMenuCheckMarkDimensions),
    ENTRY("USER32.DLL", "SetMenuItemInfoA",                     mw_SetMenuItemInfoA),
    ENTRY("USER32.DLL", "DrawTextExW",                          mw_DrawTextExW),
    ENTRY("USER32.DLL", "DefFrameProcW",                        mw_DefFrameProcW),
    ENTRY("USER32.DLL", "DefMDIChildProcW",                     mw_DefMDIChildProcW),
    ENTRY("USER32.DLL", "WaitMessage",                          mw_WaitMessage),
    ENTRY("USER32.DLL", "ToUnicodeEx",                          mw_ToUnicodeEx),
    ENTRY("USER32.DLL", "WinHelpW",                             mw_WinHelpW),
    ENTRY("USER32.DLL", "UnpackDDElParam",                      mw_UnpackDDElParam),
    ENTRY("USER32.DLL", "TranslateMDISysAccel",                 mw_TranslateMDISysAccel),
    ENTRY("USER32.DLL", "TabbedTextOutW",                       mw_TabbedTextOutW),
    ENTRY("USER32.DLL", "UnregisterHotKey",                     mw_UnregisterHotKey),
    ENTRY("USER32.DLL", "SetClipboardViewer",                   mw_SetClipboardViewer),
    ENTRY("USER32.DLL", "GetKeyNameTextW",                      mw_GetKeyNameTextW),
    ENTRY("USER32.DLL", "GetUserDefaultUILanguage",             mw_GetUserDefaultUILanguage),
    ENTRY("USER32.DLL", "CopyImage",                            mw_CopyImage),
    ENTRY("USER32.DLL", "SetWindowLongA",                       mw_SetWindowLongA),
    ENTRY("USER32.DLL", "GetWindowLongA",                       mw_GetWindowLongA),
    ENTRY("KERNEL32.DLL", "GetThreadSelectorEntry",             mw_GetThreadSelectorEntry),
    ENTRY("GDI32.DLL", "EnumFontsW",                            mw_EnumFontsW),
    ENTRY("COMCTL32.DLL", "ImageList_Copy",                     mw_ImageList_Copy),
    ENTRY("USER32.DLL", "UpdateLayeredWindow",                  mw_UpdateLayeredWindow),
    ENTRY("GDI32.DLL", "TransparentBlt",                        mw_TransparentBlt),
    ENTRY("msimg32.dll", "TransparentBlt",                      mw_TransparentBlt),
    ENTRY("USER32.DLL", "SubtractRect",                         mw_SubtractRect),
    ENTRY("KERNEL32.DLL", "GetPrivateProfileStringA",           mw_GetPrivateProfileStringA),
    ENTRY("KERNEL32.DLL", "GetPrivateProfileIntA",              mw_GetPrivateProfileIntA),
    ENTRY("KERNEL32.DLL", "WritePrivateProfileStringA",         mw_WritePrivateProfileStringA),
    ENTRY("KERNEL32.DLL", "WritePrivateProfileStringW",         mw_WritePrivateProfileStringW),
    ENTRY("ADVAPI32.DLL", "RegEnumKeyW",                        mw_RegEnumKeyW),
    ENTRY("ADVAPI32.DLL", "OpenServiceW",                       mw_OpenServiceW),
    ENTRY("ADVAPI32.DLL", "OpenServiceA",                       mw_OpenServiceA),
    ENTRY("ADVAPI32.DLL", "QueryServiceStatus",                 mw_QueryServiceStatus),
    ENTRY("ADVAPI32.DLL", "StartServiceW",                      mw_StartServiceW),
    ENTRY("ADVAPI32.DLL", "StartServiceA",                      mw_StartServiceA),
    ENTRY("ADVAPI32.DLL", "StartServiceCtrlDispatcherW",        mw_StartServiceCtrlDispatcherW),
    ENTRY("KERNEL32.DLL", "GetSystemWow64DirectoryW",           mw_GetSystemWow64DirectoryW),
    ENTRY("KERNEL32.DLL", "Wow64DisableWow64FsRedirection",     mw_Wow64DisableWow64FsRedirection),
    ENTRY("KERNEL32.DLL", "Wow64RevertWow64FsRedirection",      mw_Wow64RevertWow64FsRedirection),
    ENTRY("KERNEL32.DLL", "FindPackagesByPackageFamily",        mw_FindPackagesByPackageFamily),
    ENTRY("KERNEL32.DLL", "QueryDosDeviceW",                    mw_QueryDosDeviceW),
    ENTRY("psapi.dll", "K32GetMappedFileNameW",                 mw_K32GetMappedFileNameW),
    ENTRY("KERNEL32.DLL", "K32GetMappedFileNameW",              mw_K32GetMappedFileNameW),
    ENTRY("KERNEL32.DLL", "SearchPathW",                        mw_SearchPathW),
    ENTRY("KERNEL32.DLL", "GetTempFileNameA",                   mw_GetTempFileNameA),
    ENTRY("KERNEL32.DLL", "WinExec",                            mw_WinExec),
    ENTRY("KERNEL32.DLL", "GetTimeFormatA",                     mw_GetTimeFormatA),
    ENTRY("KERNEL32.DLL", "GetNumberFormatA",                   mw_GetNumberFormatA),
    ENTRY("KERNEL32.DLL", "GetNumberFormatEx",                  mw_GetNumberFormatEx),
    ENTRY("KERNEL32.DLL", "GlobalReAlloc",                      mw_GlobalReAlloc),
    ENTRY("KERNEL32.DLL", "CreateThreadpoolTimer",              mw_CreateThreadpoolTimer),
    ENTRY("KERNEL32.DLL", "SetThreadpoolTimer",                 mw_SetThreadpoolTimer),
    ENTRY("KERNEL32.DLL", "CloseThreadpoolTimer",               mw_CloseThreadpoolTimer),
    ENTRY("KERNEL32.DLL", "WaitForThreadpoolTimerCallbacks",    mw_WaitForThreadpoolTimerCallbacks),
    ENTRY("KERNEL32.DLL", "IsThreadpoolTimerSet",               mw_IsThreadpoolTimerSet),
    ENTRY("winhttp.dll", "WinHttpOpen",                         mw_WinHttpOpen),
    ENTRY("WINHTTP.dll", "WinHttpOpen",                         mw_WinHttpOpen),
    ENTRY("winhttp.dll", "WinHttpConnect",                      mw_WinHttpConnect),
    ENTRY("WINHTTP.dll", "WinHttpConnect",                      mw_WinHttpConnect),
    ENTRY("winhttp.dll", "WinHttpOpenRequest",                  mw_WinHttpOpenRequest),
    ENTRY("WINHTTP.dll", "WinHttpOpenRequest",                  mw_WinHttpOpenRequest),
    ENTRY("winhttp.dll", "WinHttpSendRequest",                  mw_WinHttpSendRequest),
    ENTRY("WINHTTP.dll", "WinHttpSendRequest",                  mw_WinHttpSendRequest),
    ENTRY("winhttp.dll", "WinHttpReceiveResponse",              mw_WinHttpReceiveResponse),
    ENTRY("WINHTTP.dll", "WinHttpReceiveResponse",              mw_WinHttpReceiveResponse),
    ENTRY("winhttp.dll", "WinHttpReadData",                     mw_WinHttpReadData),
    ENTRY("WINHTTP.dll", "WinHttpReadData",                     mw_WinHttpReadData),
    ENTRY("winhttp.dll", "WinHttpWriteData",                    mw_WinHttpWriteData),
    ENTRY("WINHTTP.dll", "WinHttpWriteData",                    mw_WinHttpWriteData),
    ENTRY("winhttp.dll", "WinHttpQueryHeaders",                 mw_WinHttpQueryHeaders),
    ENTRY("WINHTTP.dll", "WinHttpQueryHeaders",                 mw_WinHttpQueryHeaders),
    ENTRY("winhttp.dll", "WinHttpQueryDataAvailable",           mw_WinHttpQueryDataAvailable),
    ENTRY("WINHTTP.dll", "WinHttpQueryDataAvailable",           mw_WinHttpQueryDataAvailable),
    ENTRY("winhttp.dll", "WinHttpSetOption",                    mw_WinHttpSetOption),
    ENTRY("WINHTTP.dll", "WinHttpSetOption",                    mw_WinHttpSetOption),
    ENTRY("winhttp.dll", "WinHttpCloseHandle",                  mw_WinHttpCloseHandle),
    ENTRY("WINHTTP.dll", "WinHttpCloseHandle",                  mw_WinHttpCloseHandle),
    ENTRY("winhttp.dll", "WinHttpGetProxyForUrl",               mw_WinHttpGetProxyForUrl),
    ENTRY("WINHTTP.dll", "WinHttpGetProxyForUrl",               mw_WinHttpGetProxyForUrl),
    ENTRY("shlwapi.dll", "StrChrW",                             mw_StrChrW),
    ENTRY("shlwapi.dll", "StrStrIW",                            mw_StrStrIW),
    ENTRY("shlwapi.dll", "StrCmpIW",                            mw_StrCmpIW),
    ENTRY("shlwapi.dll", "StrCmpLogicalW",                      mw_StrCmpLogicalW),
    ENTRY("shlwapi.dll", "StrTrimW",                            mw_StrTrimW),
    ENTRY("shlwapi.dll", "StrFormatKBSizeW",                    mw_StrFormatKBSizeW),
    ENTRY("shlwapi.dll", "StrFormatByteSizeW",                  mw_StrFormatByteSizeW),
    ENTRY("shlwapi.dll", "UrlIsW",                              mw_UrlIsW),
    ENTRY("shlwapi.dll", "PathIsNetworkPathW",                  mw_PathIsNetworkPathW),
    ENTRY("msvcrt.dll", "_strcmpi",                             mw__strcmpi),
    ENTRY("msvcrt.dll", "_mbscmp",                              mw__mbscmp),
    ENTRY("msvcrt.dll", "_wtoi",                                mw__wtoi),
    ENTRY("msvcrt.dll", "_wtoi64",                              mw__wtoi64),
    ENTRY("msvcrt.dll", "_wcslwr",                              mw__wcslwr),
    ENTRY("msvcrt.dll", "_itow",                                mw__itow),
    ENTRY("msvcrt.dll", "_ui64tow",                             mw__ui64tow),
    ENTRY("msvcrt.dll", "_msize",                               mw__msize),
    ENTRY("msvcrt.dll", "wcsncat",                              mw_wcsncat),
    ENTRY("mpr.dll", "WNetGetUniversalNameW",                   mw_WNetGetUniversalNameW),
    ENTRY("shell32.dll", "SHFileOperationW",                    mw_SHFileOperationW),
    ENTRY("shell32.dll", "SHBindToParent",                      mw_SHBindToParent),
    ENTRY("ole32.dll", "StgCreateDocfileOnILockBytes",          mw_StgCreateDocfileOnILockBytes),
    ENTRY("ole32.dll", "StringFromCLSID",                       mw_StringFromCLSID),
    ENTRY("ole32.dll", "CoLockObjectExternal",                  mw_CoLockObjectExternal),
    ENTRY("ole32.dll", "CoDisconnectObject",                    mw_CoDisconnectObject),
    ENTRY("ole32.dll", "CoGetObject",                           mw_CoGetObject),
    ENTRY("ole32.dll", "CoCreateFreeThreadedMarshaler",         mw_CoCreateFreeThreadedMarshaler),
    ENTRY("ole32.dll", "CoRevokeClassObject",                   mw_CoRevokeClassObject),
    ENTRY("ADVAPI32.DLL", "CreateProcessAsUserW",               mw_CreateProcessAsUserW),
    ENTRY("winspool.drv", "DocumentPropertiesW",                mw_DocumentPropertiesW),
    ENTRY("gdiplus.dll", "GdipGetImageEncodersSize",            mw_GdipGetImageEncodersSize),
    ENTRY("gdiplus.dll", "GdipGetImageGraphicsContext",         mw_GdipGetImageGraphicsContext),
    ENTRY("gdiplus.dll", "GdipGetImagePalette",                 mw_GdipGetImagePalette),
    ENTRY("gdiplus.dll", "GdipGetImagePaletteSize",             mw_GdipGetImagePaletteSize),
    ENTRY("gdiplus.dll", "GdipCloneImage",                      mw_GdipCloneImage),
    ENTRY("gdiplus.dll", "GdipDrawImageI",                      mw_GdipDrawImageI),
    ENTRY("gdiplus.dll", "GdipDrawImageRectI",                  mw_GdipDrawImageRectI),
    ENTRY("gdiplus.dll", "GdipDrawImageRectRectI",              mw_GdipDrawImageRectRectI),
    ENTRY("gdiplus.dll", "GdipSaveImageToStream",               mw_GdipSaveImageToStream),
    ENTRY("snmpapi.dll", "SnmpUtilOidNCmp",                     mw_SnmpUtilOidNCmp),
    ENTRY("snmpapi.dll", "SnmpUtilOidCpy",                      mw_SnmpUtilOidCpy),
    ENTRY("snmpapi.dll", "SnmpSvcGetUptime",                    mw_SnmpSvcGetUptime),
    ENTRY("NETAPI32.dll", "NetApiBufferFree",                   mw_NetApiBufferFree),
    ENTRY("uxtheme.dll", "OpenThemeData",                       mw_OpenThemeData),
    ENTRY("uxtheme.dll", "OpenThemeDataEx",                     mw_OpenThemeDataEx),
    ENTRY("uxtheme.dll", "CloseThemeData",                      mw_CloseThemeData),
    ENTRY("uxtheme.dll", "IsAppThemed",                         mw_IsAppThemed),
    ENTRY("uxtheme.dll", "IsThemeActive",                       mw_IsThemeActive),
    ENTRY("uxtheme.dll", "GetCurrentThemeName",                 mw_GetCurrentThemeName),
    ENTRY("uxtheme.dll", "SetWindowTheme",                      mw_SetWindowTheme),
    ENTRY("uxtheme.dll", "GetWindowTheme",                      mw_GetWindowTheme),
    ENTRY("uxtheme.dll", "DrawThemeBackground",                 mw_DrawThemeBackground),
    ENTRY("uxtheme.dll", "DrawThemeText",                       mw_DrawThemeText),
    ENTRY("uxtheme.dll", "DrawThemeParentBackground",           mw_DrawThemeParentBackground),
    ENTRY("uxtheme.dll", "GetThemePartSize",                    mw_GetThemePartSize),
    ENTRY("uxtheme.dll", "GetThemeMargins",                     mw_GetThemeMargins),
    ENTRY("uxtheme.dll", "GetThemeInt",                         mw_GetThemeInt),
    ENTRY("uxtheme.dll", "GetThemeColor",                       mw_GetThemeColor),
    ENTRY("uxtheme.dll", "GetThemeFont",                        mw_GetThemeFont),
    ENTRY("uxtheme.dll", "GetThemeSysColor",                    mw_GetThemeSysColor),
    ENTRY("uxtheme.dll", "GetThemeTextExtent",                  mw_GetThemeTextExtent),
    ENTRY("uxtheme.dll", "IsThemeBackgroundPartiallyTransparent", mw_IsThemeBackgroundPartiallyTransparent),
'''

txt = open(LOADER, encoding='utf-8').read()
orig = txt

# 1. impl block before the import table
anchor = 'static ImportEntry g_import_table[] = {'
assert txt.count(anchor) == 1
txt = txt.replace(anchor, IMPL + '\n' + anchor)

# 2. ENTRY lines before the table close (after the GetCharacterPlacementW pair)
tail_anchor = ('ENTRY("gdi32.dll", "GetCharacterPlacementW",                mw_GetCharacterPlacementW),\n'
               '    ENTRY("GDI32.DLL", "GetCharacterPlacementW",                mw_GetCharacterPlacementW),\n};')
assert txt.count(tail_anchor) == 1
txt = txt.replace(tail_anchor,
    'ENTRY("gdi32.dll", "GetCharacterPlacementW",                mw_GetCharacterPlacementW),\n'
    'ENTRY("GDI32.DLL", "GetCharacterPlacementW",                mw_GetCharacterPlacementW),\n'
    + ENTRIES + '};')

# 3a. _wcmdln globals next to g_acmdln_ptr
g_anchor = 'static char* g_acmdln_ptr = NULL;     /* pointer to ANSI command line */'
assert txt.count(g_anchor) == 1
txt = txt.replace(g_anchor, g_anchor +
    '\nstatic uint16_t g_cmdline_utf16[2048]; /* SWEEP-027: real UTF-16 command line for _wcmdln */'
    '\nstatic uint16_t* g_wcmdln_ptr = g_cmdline_utf16;')

# 3b. fill UTF-16 cmdline in main after the mbstowcs line
main_anchor = ('mbstowcs(g_cmdline_wide, g_cmdline_ansi, sizeof(g_cmdline_wide)/sizeof(wchar_t));')
assert txt.count(main_anchor) == 1
fill = main_anchor + '''
    { /* SWEEP-027: widen to real UTF-16 for msvcrt _wcmdln (wchar_t is 4B on host) */
        size_t i = 0;
        for (; g_cmdline_ansi[i] && i + 1 < sizeof(g_cmdline_utf16) / sizeof(g_cmdline_utf16[0]); i++)
            g_cmdline_utf16[i] = (uint16_t)(unsigned char)g_cmdline_ansi[i];
        g_cmdline_utf16[i] = 0;
    }'''
txt = txt.replace(main_anchor, fill)

# 3c. data import entry after _acmdln
d_anchor = '{ "msvcrt.dll", "_acmdln",    &g_acmdln_ptr },'
assert txt.count(d_anchor) == 1
txt = txt.replace(d_anchor, d_anchor +
    '\n    { "msvcrt.dll", "_wcmdln",     &g_wcmdln_ptr }, /* SWEEP-027 */')

open(LOADER, 'w', encoding='utf-8').write(txt)
print(f"spliced: {len(txt)-len(orig):+d} bytes; "
      f"entries={ENTRIES.count('ENTRY(')}")
