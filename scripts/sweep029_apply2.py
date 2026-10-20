#!/usr/bin/env python3
# SWEEP-029b: splice the ODBC/OLEAUT32 ordinal tail into loader.c
import sys

LOADER = 'src/loader/loader.c'
IMPL = open('scripts/sweep029_impl_b.c', encoding='utf-8').read()

ENTRIES = '''    /* -- SWEEP-029b: bginfo64 ODBC32 ordinals + winmergeu OLEAUT32 ordinals -- */
    ENTRY("ODBC32.DLL", "SQLDescribeCol",                       mw_SQLDescribeCol),
    ENTRY("ODBC32.DLL", "SQLAllocHandle",                       mw_SQLAllocHandle),
    ENTRY("ODBC32.DLL", "SQLFreeHandle",                        mw_SQLFreeHandle),
    ENTRY("ODBC32.DLL", "SQLSetEnvAttr",                        mw_SQLSetEnvAttr),
    ENTRY("ODBC32.DLL", "SQLGetDiagRecW",                       mw_SQLGetDiagRecW),
    ENTRY("ODBC32.DLL", "SQLSetConnectAttrW",                   mw_SQLSetConnectAttrW),
    ENTRY("OLEAUT32.DLL", "SafeArrayPutElement",                mw_SafeArrayPutElement),
    ENTRY("OLEAUT32.DLL", "VarBstrFromDate",                    mw_VarBstrFromDate),
    ENTRY("OLEAUT32.DLL", "DllUnregisterServer",                mw_DllUnregisterServer),
'''

txt = open(LOADER, encoding='utf-8').read()
if 'SWEEP-029b' in txt:
    print('already applied'); sys.exit(0)

# 1. impl block right after the SWEEP-029 impl end marker
anchor = '/* ==== SWEEP-029 IMPL END ==== */'
assert anchor in txt, 'SWEEP-029 impl end marker missing'
txt = txt.replace(anchor, anchor + '\n\n' + IMPL, 1)

# 2. ENTRY lines before the table close
k = txt.index('static ImportEntry g_import_table[] = {')
end = txt.index('};', k)
txt = txt[:end] + ENTRIES + txt[end:]

# 3. ole_ord map: ODBC32 + OLEAUT32 (114/25/418)
_ord_old = '                    { "SHLWAPI", 12,  "SHCreateMemStream" },'
_ord_new = (_ord_old + '\n'
    '                    /* SWEEP-029b: bginfo64 ODBC32 + winmergeu OLEAUT32 raw ordinals */\n'
    '                    { "ODBC32", 9,   "SQLDescribeCol" },\n'
    '                    { "ODBC32", 24,  "SQLAllocHandle" },\n'
    '                    { "ODBC32", 31,  "SQLFreeHandle" },\n'
    '                    { "ODBC32", 75,  "SQLSetEnvAttr" },\n'
    '                    { "ODBC32", 136, "SQLGetDiagRecW" },\n'
    '                    { "ODBC32", 141, "SQLSetConnectAttrW" },\n'
    '                    { "OLEAUT32", 25,  "SafeArrayPutElement" },\n'
    '                    { "OLEAUT32", 114, "VarBstrFromDate" },\n'
    '                    { "OLEAUT32", 418, "DllUnregisterServer" },')
assert _ord_old in txt, 'SWEEP-029 ordinal anchor missing'
txt = txt.replace(_ord_old, _ord_new, 1)

open(LOADER, 'w', encoding='utf-8').write(txt)
n = ENTRIES.count('ENTRY(')
print(f'applied: impl {len(IMPL)} bytes, {n} ENTRY lines, 9 ordinals mapped')
