#!/usr/bin/env python3
"""SWEEP-032 splice: apply sweep032_impl.c sections into src/loader/loader.c.
Train 1 of 5 — every replacement must match EXACTLY ONCE; aborts otherwise
(train discipline: a wagon either lands whole or not at all)."""
import sys, re

LOADER = "src/loader/loader.c"
IMPL = "scripts/sweep032_impl.c"

with open(LOADER) as f:
    src = f.read()
with open(IMPL) as f:
    impl = f.read()

def section(name):
    pat = r"/\* ==== SECTION: %s ==== \*/\n(.*?)(?=/\* ==== SECTION:|/\* ==== SWEEP-032 IMPL END ==== \*/)" % re.escape(name)
    m = re.search(pat, impl, re.S)
    if not m:
        raise SystemExit(f"impl section '{name}' not found")
    return m.group(1).rstrip() + "\n"

fails = []
def replace(old, new, label):
    global src
    n = src.count(old)
    if n != 1:
        fails.append(f"FAILED [{label}]: {n} occurrences (need exactly 1)")
        return
    src = src.replace(old, new)
    print(f"  ok: {label}")

def replace_span(start, end, new, label):
    """Replace the inclusive span start..end; both markers must be unique."""
    global src
    ns, ne = src.count(start), src.count(end)
    if ns != 1 or ne != 1:
        fails.append(f"FAILED [{label}]: start={ns} end={ne} (need 1/1)")
        return
    i = src.index(start)
    j = src.index(end)
    if j < i:
        fails.append(f"FAILED [{label}]: end marker before start")
        return
    src = src[:i] + new + src[j + len(end):]
    print(f"  ok: {label} (span {j + len(end) - i} chars)")

print("== SWEEP-032 splice ==")

# ---- 1. engine fix: GDI+ Status NotImplemented is 6 (not 4/ObjectBusy) ----
replace("#define MW_GDIPLUS_NOTIMPL 4u",
        "#define MW_GDIPLUS_NOTIMPL 6u /* SWEEP-032: was 4 (=ObjectBusy) — Status::NotImplemented is 6 */",
        "NOTIMPL-6")

# ---- 2. SW-1152: RtlCaptureContext SSE/FPU capture (span replace) ----
sse_start = "__attribute__((ms_abi)) void mw_RtlCaptureContext(void* ctx) {"
sse_end = '    MW_TRACE("RtlCaptureContext() rip=0x%lx", rip);\n}'
replace_span(sse_start, sse_end, section("sse").rstrip("\n") + "\n", "SW-1152-SSE")

# ---- 3. gdobj state store (span replace) ----
gdo_start = "#define MW_GDOBJ_BASE 0x00B20000u"
gdo_end = """static int mw_gdobj_kind(void* h) {
    uintptr_t v = (uintptr_t)h;
    if ((v & 0xFFFF0000u) != MW_GDOBJ_BASE) return 0;
    int i = (int)(v & 0xFF) - 1;
    if (i < 0 || i >= 64) return 0;
    return g_gdobj[i].kind;
}"""
replace_span(gdo_start, gdo_end, section("gdobj").rstrip("\n"), "gdobj-state")

# ---- 4. GdipCreatePen1 real state ----
replace("""__attribute__((ms_abi)) uint32_t mw_GdipCreatePen1(uint32_t color, float width, uint32_t unit, void** pen) {
    (void)color; (void)width; (void)unit;
    if (!pen) return MW_GDIPLUS_INVALIDPARAM;
    void* p = mw_gdobj_new(2);
    if (!p) return MW_GDIPLUS_GENERICERR;
    *pen = p;
    return MW_GDIPLUS_OK;
}""", section("pen").rstrip("\n"), "GdipCreatePen1")

# ---- 5. GdipCreateBitmapFromScan0 real pixel store ----
replace("""__attribute__((ms_abi)) uint32_t mw_GdipCreateBitmapFromScan0(int w, int h, int stride, uint32_t fmt, void* bits, void** bmp) {
    if (w <= 0 || h <= 0 || !bmp) return MW_GDIPLUS_INVALIDPARAM;
    void* b = mw_gdobj_new(5);
    if (!b) return MW_GDIPLUS_GENERICERR;
    *bmp = b;
    (void)stride; (void)fmt; (void)bits;
    return MW_GDIPLUS_OK;
}""", section("bmpscan0").rstrip("\n"), "GdipCreateBitmapFromScan0")

# ---- 6. gdiplus wagons after GdipDrawLinesI ----
gdl = """__attribute__((ms_abi)) uint32_t mw_GdipDrawLinesI(void* g, void* pen, const void* pts, int n) {
    (void)g; (void)pen; (void)pts; (void)n; return MW_GDIPLUS_OK;
}"""
replace(gdl, gdl + "\n" + section("gdiplus_wagons").rstrip("\n"), "gdiplus-wagons")

# ---- 7. user32 wagons after PostMessageW ----
pmw = """__attribute__((ms_abi)) int mw_PostMessageW(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp) {
    (void)hwnd; (void)msg; (void)wp; (void)lp;
    MW_TRACE("PostMessageW(msg=%#x) dropped (headless) -> TRUE", msg);
    return 1;
}"""
replace(pmw, pmw + "\n" + section("user32_wagons").rstrip("\n"), "user32-wagons")

# ---- 8. kernel32/winmm/threadpool wagons after OpenEventW ----
oew = """__attribute__((ms_abi)) void* mw_OpenEventW(uint32_t access, int inherit, const uint16_t* name) {
    (void)access; (void)inherit;
    char n8[256];
    if (name) w3_w16_to_utf8z(name, n8, sizeof(n8)); else n8[0] = 0;
    MW_TRACE("OpenEventW(%s) -> NULL", n8);
    mw_SetLastError(2); /* named events unsupported */
    return NULL;
}"""
replace(oew, oew + "\n" + section("kernel32_wagons").rstrip("\n"), "kernel32-wagons")

# ---- 9. reg wagons before RegSetValueExW ----
rsv = "__attribute__((ms_abi)) int32_t mw_RegSetValueExW(void* key, const wchar_t* name, uint32_t res, uint32_t type, const void* data, uint32_t size) {"
replace(rsv, section("reg_wagons").rstrip("\n") + "\n" + rsv, "reg-wagons")

# ---- 10. COM/WinRT/MF/shell + subclass wagons after CoFreeUnusedLibraries ----
cful = """__attribute__((ms_abi)) void mw_CoFreeUnusedLibraries(void) {
    MW_TRACE("CoFreeUnusedLibraries() (vacuous: no delayed unloads)");
}"""
replace(cful, cful + "\n" + section("com_wagons").rstrip("\n") + "\n" + section("subclass_wagons").rstrip("\n"), "com+subclass-wagons")

# ---- 11. clipboard wiring (globals early + owner/seq updates) ----
replace("""static int      g_clip_open = 0;
static uint32_t g_clip_fmt = 0;
static void*    g_clip_data = 0;
__attribute__((ms_abi)) int mw_OpenClipboard(void* owner) {
    (void)owner;
    g_clip_open = 1;
    MW_TRACE("OpenClipboard() -> TRUE (single headless clipboard)");
    return 1;
}""", """static int      g_clip_open = 0;
static uint32_t g_clip_fmt = 0;
static void*    g_clip_data = 0;
static void*    g_clip_owner = 0;  /* SWEEP-032: GetOpenClipboardWindow */
static uint32_t g_clip_seq = 0;    /* SWEEP-032: GetClipboardSequenceNumber */
__attribute__((ms_abi)) int mw_OpenClipboard(void* owner) {
    g_clip_owner = owner;
    g_clip_open = 1;
    MW_TRACE("OpenClipboard(owner=%p) -> TRUE (single headless clipboard)", owner);
    return 1;
}""", "clipboard-open")
replace("""__attribute__((ms_abi)) int mw_EmptyClipboard(void) {
    g_clip_fmt = 0; g_clip_data = 0;
    MW_TRACE("EmptyClipboard() -> TRUE");
    return 1;
}""", """__attribute__((ms_abi)) int mw_EmptyClipboard(void) {
    g_clip_fmt = 0; g_clip_data = 0;
    g_clip_seq++; /* real: clipboard change bumps the sequence number */
    MW_TRACE("EmptyClipboard() -> TRUE");
    return 1;
}""", "clipboard-empty")
replace("""__attribute__((ms_abi)) void* mw_SetClipboardData(uint32_t fmt, void* mem) {
    if (!g_clip_open) return NULL;
    g_clip_fmt = fmt; g_clip_data = mem;
    MW_TRACE("SetClipboardData(fmt=%#x, %p) -> stored", fmt, mem);
    return mem;
}""", """__attribute__((ms_abi)) void* mw_SetClipboardData(uint32_t fmt, void* mem) {
    if (!g_clip_open) return NULL;
    g_clip_fmt = fmt; g_clip_data = mem;
    g_clip_seq++; /* real: clipboard change bumps the sequence number */
    MW_TRACE("SetClipboardData(fmt=%#x, %p) -> stored", fmt, mem);
    return mem;
}""", "clipboard-set")

# ---- 12. ole_ord table: COMCTL32 410/412/413 + SHLWAPI 219 (wine spec) ----
replace("""                    { "OLEAUT32", 8, "VariantInit" },
                };""", """                    { "OLEAUT32", 8, "VariantInit" },
                    /* SWEEP-032 (SW-1106): zoomit64 raw ordinals — names via
                     * wine-mirror comctl32.spec/shlwapi.spec (2026-10-21) */
                    { "COMCTL32", 410, "SetWindowSubclass" },
                    { "COMCTL32", 412, "RemoveWindowSubclass" },
                    { "COMCTL32", 413, "DefSubclassProc" },
                    { "SHLWAPI", 219, "QISearch" },
                };""", "ole_ord-4")

# ---- 13. ENTRY table insertions ----
replace('    ENTRY("gdiplus.dll", "GdipDrawLinesI",                      mw_GdipDrawLinesI),',
"""    ENTRY("gdiplus.dll", "GdipDrawLinesI",                      mw_GdipDrawLinesI),
    /* SWEEP-032 (SW-1106 zoomit64 GDI+ tail) */
    ENTRY("gdiplus.dll", "GdipSetPenLineJoin",                  mw_GdipSetPenLineJoin),
    ENTRY("gdiplus.dll", "GdipSetPenLineCap197819",             mw_GdipSetPenLineCap197819),
    ENTRY("gdiplus.dll", "GdipSetPenStartCap",                  mw_GdipSetPenStartCap),
    ENTRY("gdiplus.dll", "GdipSetPenEndCap",                    mw_GdipSetPenEndCap),
    ENTRY("gdiplus.dll", "GdipDrawRectangleI",                  mw_GdipDrawRectangleI),
    ENTRY("gdiplus.dll", "GdipDrawEllipse",                     mw_GdipDrawEllipse),
    ENTRY("gdiplus.dll", "GdipDrawEllipseI",                    mw_GdipDrawEllipseI),
    ENTRY("gdiplus.dll", "GdipDrawArc",                         mw_GdipDrawArc),
    ENTRY("gdiplus.dll", "GdipFillRectangle",                   mw_GdipFillRectangle),
    ENTRY("gdiplus.dll", "GdipFillEllipse",                     mw_GdipFillEllipse),
    ENTRY("gdiplus.dll", "GdipFillEllipseI",                    mw_GdipFillEllipseI),
    ENTRY("gdiplus.dll", "GdipFillPolygon",                     mw_GdipFillPolygon),
    ENTRY("gdiplus.dll", "GdipBitmapGetPixel",                  mw_GdipBitmapGetPixel),
    ENTRY("gdiplus.dll", "GdipCreateEffect",                    mw_GdipCreateEffect),
    ENTRY("gdiplus.dll", "GdipDeleteEffect",                    mw_GdipDeleteEffect),
    ENTRY("gdiplus.dll", "GdipSetEffectParameters",             mw_GdipSetEffectParameters),
    ENTRY("gdiplus.dll", "GdipBitmapApplyEffect",               mw_GdipBitmapApplyEffect),
    ENTRY("gdiplus.dll", "GdipSaveImageToFile",                 mw_GdipSaveImageToFile),""", "ENTRY-gdiplus")

replace('    ENTRY("KERNEL32.DLL", "CreateSemaphoreW",                   mw_CreateSemaphoreW),',
"""    ENTRY("KERNEL32.DLL", "CreateSemaphoreW",                   mw_CreateSemaphoreW),
    /* SWEEP-032 (SW-1106 zoomit64 KERNEL32/WINMM/threadpool tail) */
    ENTRY("KERNEL32.dll", "CreateSemaphoreExW",                 mw_CreateSemaphoreExW),
    ENTRY("KERNEL32.dll", "OpenSemaphoreW",                     mw_OpenSemaphoreW),
    ENTRY("KERNEL32.dll", "CompareStringOrdinal",               mw_CompareStringOrdinal),
    ENTRY("KERNEL32.dll", "LCIDToLocaleName",                   mw_LCIDToLocaleName),
    ENTRY("KERNEL32.dll", "CreateThreadpoolWait",               mw_CreateThreadpoolWait),
    ENTRY("KERNEL32.dll", "SetThreadpoolWait",                  mw_SetThreadpoolWait),
    ENTRY("KERNEL32.dll", "CloseThreadpoolWait",                mw_CloseThreadpoolWait),
    ENTRY("KERNEL32.dll", "SetThreadpoolWaitEx",                mw_SetThreadpoolWaitEx),
    ENTRY("KERNEL32.dll", "SetThreadpoolTimerEx",               mw_SetThreadpoolTimerEx),
    ENTRY("WINMM.dll", "timeSetEvent",                          mw_timeSetEvent),
    ENTRY("WINMM.dll", "timeKillEvent",                         mw_timeKillEvent),""", "ENTRY-kernel32")

replace('    ENTRY("COMCTL32.DLL", "InitCommonControls",                 mw_InitCommonControls),',
"""    ENTRY("COMCTL32.DLL", "InitCommonControls",                 mw_InitCommonControls),
    /* SWEEP-032 (SW-1106): COMCTL32 raw ordinals 410/412/413 (wine spec) */
    ENTRY("COMCTL32.dll", "SetWindowSubclass",                  mw_SetWindowSubclass),
    ENTRY("COMCTL32.dll", "RemoveWindowSubclass",               mw_RemoveWindowSubclass),
    ENTRY("COMCTL32.dll", "DefSubclassProc",                    mw_DefSubclassProc),""", "ENTRY-comctl")

replace('    ENTRY("USER32.DLL", "PostMessageW",                         mw_PostMessageW),',
"""    ENTRY("USER32.DLL", "PostMessageW",                         mw_PostMessageW),
    /* SWEEP-032 (SW-1106 zoomit64 USER32 tail) */
    ENTRY("USER32.dll", "SetMessageExtraInfo",                  mw_SetMessageExtraInfo),
    ENTRY("USER32.dll", "GetMessageExtraInfo",                  mw_GetMessageExtraInfo),
    ENTRY("USER32.dll", "GetLayeredWindowAttributes",           mw_GetLayeredWindowAttributes),
    ENTRY("USER32.dll", "VkKeyScanW",                           mw_VkKeyScanW),
    ENTRY("USER32.dll", "ClipCursor",                           mw_ClipCursor),
    ENTRY("USER32.dll", "GetClipCursor",                        mw_GetClipCursor),
    ENTRY("USER32.dll", "SendNotifyMessageW",                   mw_SendNotifyMessageW),
    ENTRY("USER32.dll", "MonitorFromRect",                      mw_MonitorFromRect),
    ENTRY("USER32.dll", "SetWindowDisplayAffinity",             mw_SetWindowDisplayAffinity),
    ENTRY("USER32.dll", "ChangeDisplaySettingsExW",             mw_ChangeDisplaySettingsExW),
    ENTRY("USER32.dll", "SendInput",                            mw_SendInput),
    ENTRY("WTSAPI32.dll", "WTSRegisterSessionNotification",     mw_WTSRegisterSessionNotification),
    ENTRY("WTSAPI32.dll", "WTSUnRegisterSessionNotification",   mw_WTSUnRegisterSessionNotification),""", "ENTRY-user32")

replace('    ENTRY("advapi32.dll", "GetUserNameExW",                     mw_GetUserNameExW),',
"""    ENTRY("advapi32.dll", "GetUserNameExW",                     mw_GetUserNameExW),
    /* SWEEP-032 (SW-1106 zoomit64 registry tail) */
    ENTRY("advapi32.dll", "RegSetKeyValueW",                    mw_RegSetKeyValueW),
    ENTRY("advapi32.dll", "RegDeleteKeyValueW",                 mw_RegDeleteKeyValueW),""", "ENTRY-advapi")

replace('    ENTRY("OLE32.dll", "OleLockRunning",                        mw_OleLockRunning),',
"""    ENTRY("OLE32.dll", "OleLockRunning",                        mw_OleLockRunning),
    /* SWEEP-032 (SW-1106 zoomit64 COM/WinRT/MF tail) */
    ENTRY("OLE32.dll", "RoGetAgileReference",                   mw_RoGetAgileReference),
    ENTRY("OLE32.dll", "CoGetApartmentType",                    mw_CoGetApartmentType),
    ENTRY("OLE32.dll", "CoReleaseMarshalData",                  mw_CoReleaseMarshalData),
    ENTRY("api-ms-win-shcore-stream-winrt-l1-1-0.dll", "CreateStreamOverRandomAccessStream", mw_CreateStreamOverRandomAccessStream),
    ENTRY("api-ms-win-core-winrt-error-l1-1-0.dll", "RoFailFastWithErrorContext", mw_RoFailFastWithErrorContext),
    ENTRY("api-ms-win-core-winrt-error-l1-1-0.dll", "GetRestrictedErrorInfo", mw_GetRestrictedErrorInfo),
    ENTRY("api-ms-win-core-winrt-error-l1-1-0.dll", "RoTransformError", mw_RoTransformError),
    ENTRY("api-ms-win-core-winrt-error-l1-1-1.dll", "RoOriginateLanguageException", mw_RoOriginateLanguageException),
    ENTRY("api-ms-win-core-winrt-error-l1-1-1.dll", "SetRestrictedErrorInfo", mw_SetRestrictedErrorInfo),
    ENTRY("api-ms-win-core-winrt-l1-1-0.dll", "RoGetActivationFactory", mw_RoGetActivationFactory),
    ENTRY("MFPlat.DLL", "MFStartup",                            mw_MFStartup),
    ENTRY("MFPlat.DLL", "MFShutdown",                           mw_MFShutdown),
    ENTRY("MFPlat.DLL", "MFCopyImage",                          mw_MFCopyImage),
    ENTRY("MFPlat.DLL", "MFCreateMediaType",                    mw_MFCreateMediaType),
    ENTRY("MFPlat.DLL", "MFCreateAttributes",                   mw_MFCreateAttributes),
    ENTRY("MFReadWrite.dll", "MFCreateSourceReaderFromMediaSource", mw_MFCreateSourceReaderFromMediaSource),
    ENTRY("MF.dll", "MFEnumDeviceSources",                      mw_MFEnumDeviceSources),
    ENTRY("SHLWAPI.dll", "SHCreateStreamOnFileEx",              mw_SHCreateStreamOnFileEx),
    ENTRY("SHLWAPI.dll", "QISearch",                            mw_QISearch),""", "ENTRY-ole32")

if fails:
    print("\n".join(fails))
    raise SystemExit(1)

with open(LOADER, "w") as f:
    f.write(src)
print("SWEEP-032 splice complete.")
