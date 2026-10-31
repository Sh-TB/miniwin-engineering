#!/usr/bin/env python3
# SWEEP-066 (SW-1297): splice the Notepad++ 66-import wagon batch into loader.c
# 1. impl block immediately before the g_import_table declaration
# 2. ENTRY lines before the FIRST table close after that declaration
import sys, re

LOADER = 'src/loader/loader.c'
IMPL = open('scripts/sweep066_impl.c', encoding='utf-8').read()

ENTRIES = '''    /* -- SWEEP-066 wagon batch (SW-1297): Notepad++ 8.9.8.1 66-tail --
     * SHLWAPI path surgery REAL (wine semantics), USER32/GDI tails
     * typed-honest, CRYPT/WINTRUST honest-unverified, InternetCrackUrlW
     * REAL (wine layout), ImageNtHeader REAL, CopyFileExW REAL. -- */
    ENTRY("USER32.dll", "AddClipboardFormatListener",           mw_AddClipboardFormatListener),
    ENTRY("SHLWAPI.dll", "AssocQueryStringW",                   mw_AssocQueryStringW),
    ENTRY("UxTheme.dll", "BeginBufferedAnimation",              mw_BeginBufferedAnimation),
    ENTRY("UxTheme.dll", "BufferedPaintRenderAnimation",        mw_BufferedPaintRenderAnimation),
    ENTRY("UxTheme.dll", "BufferedPaintStopAllAnimations",      mw_BufferedPaintStopAllAnimations),
    ENTRY("CRYPT32.dll", "CertFindCertificateInStore",          mw_CertFindCertificateInStore),
    ENTRY("CRYPT32.dll", "CertGetCertificateContextProperty",   mw_CertGetCertificateContextProperty),
    ENTRY("CRYPT32.dll", "CertNameToStrW",                      mw_CertNameToStrW),
    ENTRY("SHLWAPI.dll", "ColorAdjustLuma",                     mw_ColorAdjustLuma),
    ENTRY("KERNEL32.dll", "CompareStringOrdinal",               mw_CompareStringOrdinal),
    ENTRY("KERNEL32.dll", "CopyFileExW",                        mw_CopyFileExW),
    ENTRY("CRYPT32.dll", "CryptMsgClose",                       mw_CryptMsgClose),
    ENTRY("CRYPT32.dll", "CryptMsgGetParam",                    mw_CryptMsgGetParam),
    ENTRY("CRYPT32.dll", "CryptQueryObject",                    mw_CryptQueryObject),
    ENTRY("SHELL32.dll", "DragQueryPoint",                      mw_DragQueryPoint),
    ENTRY("UxTheme.dll", "DrawThemeTextEx",                     mw_DrawThemeTextEx),
    ENTRY("dwmapi.dll", "DwmGetColorizationColor",              mw_DwmGetColorizationColor),
    ENTRY("UxTheme.dll", "EndBufferedAnimation",                mw_EndBufferedAnimation),
    ENTRY("USER32.dll", "EnumThreadWindows",                    mw_EnumThreadWindows),
    ENTRY("GDI32.dll", "ExtCreatePen",                          mw_ExtCreatePen),
    ENTRY("GDI32.dll", "GdiAlphaBlend",                         mw_AlphaBlend),
    ENTRY("KERNEL32.dll", "GetApplicationRestartSettings",      mw_GetApplicationRestartSettings),
    ENTRY("GDI32.dll", "GetClipRgn",                            mw_GetClipRgn),
    ENTRY("KERNEL32.dll", "GetDateFormatEx",                    mw_GetDateFormatEx),
    ENTRY("KERNEL32.dll", "GetProductInfo",                     mw_GetProductInfo),
    ENTRY("KERNEL32.dll", "GetStringTypeExA",                   mw_GetStringTypeExA),
    ENTRY("UxTheme.dll", "GetThemeBackgroundContentRect",       mw_GetThemeBackgroundContentRect),
    ENTRY("UxTheme.dll", "GetThemeTransitionDuration",          mw_GetThemeTransitionDuration),
    ENTRY("KERNEL32.dll", "GetTimeFormatEx",                    mw_GetTimeFormatEx),
    ENTRY("COMCTL32.dll", "ImageList_SetIconSize",              mw_ImageList_SetIconSize),
    ENTRY("dbghelp.dll", "ImageNtHeader",                       mw_ImageNtHeader),
    ENTRY("IMM32.dll", "ImmEscapeW",                            mw_ImmEscapeW),
    ENTRY("IMM32.dll", "ImmNotifyIME",                          mw_ImmNotifyIME),
    ENTRY("IMM32.dll", "ImmSetCandidateWindow",                 mw_ImmSetCandidateWindow),
    ENTRY("IMM32.dll", "ImmSetCompositionStringW",              mw_ImmSetCompositionStringW),
    ENTRY("WININET.dll", "InternetCrackUrlW",                   mw_InternetCrackUrlW),
    ENTRY("USER32.dll", "IsCharAlphaNumericW",                  mw_IsCharAlphaNumericW),
    ENTRY("USER32.dll", "IsCharAlphaW",                         mw_IsCharAlphaW),
    ENTRY("SensApi.dll", "IsDestinationReachableW",             mw_IsDestinationReachableW),
    ENTRY("SensApi.dll", "IsNetworkAlive",                      mw_IsNetworkAlive),
    ENTRY("ADVAPI32.dll", "IsTextUnicode",                      mw_IsTextUnicode),
    ENTRY("USER32.dll", "MonitorFromRect",                      mw_MonitorFromRect),
    ENTRY("SHLWAPI.dll", "PathAppendW",                         mw_PathAppendW),
    ENTRY("SHLWAPI.dll", "PathCanonicalizeW",                   mw_PathCanonicalizeW),
    ENTRY("SHLWAPI.dll", "PathCompactPathExW",                  mw_PathCompactPathExW),
    ENTRY("SHLWAPI.dll", "PathGetDriveNumberW",                 mw_PathGetDriveNumberW),
    ENTRY("SHLWAPI.dll", "PathIsRelativeW",                     mw_PathIsRelativeW),
    ENTRY("SHLWAPI.dll", "PathRemoveExtensionW",                mw_PathRemoveExtensionW),
    ENTRY("SHLWAPI.dll", "PathStripPathW",                      mw_PathStripPathW),
    ENTRY("KERNEL32.dll", "QueueUserAPC",                       mw_QueueUserAPC),
    ENTRY("KERNEL32.dll", "RegisterApplicationRestart",         mw_RegisterApplicationRestart),
    ENTRY("USER32.dll", "RemoveClipboardFormatListener",        mw_RemoveClipboardFormatListener),
    ENTRY("SHELL32.dll", "SHCreateItemFromParsingName",         mw_SHCreateItemFromParsingName),
    ENTRY("SHELL32.dll", "SHOpenFolderAndSelectItems",          mw_SHOpenFolderAndSelectItems),
    ENTRY("USER32.dll", "ToAscii",                              mw_ToAscii),
    ENTRY("KERNEL32.dll", "UnregisterApplicationRestart",       mw_UnregisterApplicationRestart),
    ENTRY("WINTRUST.dll", "WinVerifyTrust",                     mw_WinVerifyTrust),
    ENTRY("COMCTL32.dll", "_TrackMouseEvent",                   mw_TrackMouseEvent),
    ENTRY("KERNEL32.dll", "lstrcmpiA",                          mw_lstrcmpiA),
    ENTRY("USER32.dll", "mouse_event",                          mw_mouse_event),
'''

src = open(LOADER, encoding='utf-8').read()
if 'SWEEP-066 wagon batch (SW-1297)' in src:
    print('already applied'); sys.exit(0)

anchor = re.search(r'^(static [^\n]*g_import_table[^\n]*\{)', src, re.M)
if not anchor:
    print('cannot locate g_import_table'); sys.exit(1)

# table close = first "\n};" at-or-after the anchor (table is a flat ENTRY array)
close = src.find('\n};', anchor.start())
if close < 0:
    print('cannot locate table close'); sys.exit(1)

src = src[:anchor.start()] + IMPL + '\n' + src[anchor.start():close] + ENTRIES.rstrip('\n') + src[close:]

open(LOADER, 'w', encoding='utf-8').write(src)
print('spliced: impl block +', ENTRIES.count('ENTRY('), 'ENTRY lines')
