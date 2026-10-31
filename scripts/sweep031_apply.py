#!/usr/bin/env python3
"""SWEEP-031 splice: apply sweep031_impl.c sections into src/loader/loader.c.
Each replacement must match EXACTLY ONCE; aborts otherwise (train discipline:
a wagon either lands whole or not at all)."""
import sys, re

LOADER = "src/loader/loader.c"
IMPL = "scripts/sweep031_impl.c"

with open(LOADER) as f:
    src = f.read()
with open(IMPL) as f:
    impl = f.read()

def section(name):
    pat = r"/\* ==== SECTION: %s ==== \*/\n(.*?)(?=/\* ==== SECTION:|/\* ==== SWEEP-031 IMPL END ==== \*/)" % re.escape(name)
    m = re.search(pat, impl, re.S)
    if not m:
        raise SystemExit(f"impl section '{name}' not found")
    return m.group(1).rstrip() + "\n"

def replace(old, new, label):
    global src
    n = src.count(old)
    if n != 1:
        raise SystemExit(f"FAILED [{label}]: {n} occurrences (need exactly 1)")
    src = src.replace(old, new)
    print(f"  ok: {label}")

# ---- SW-1201: hooks ----
hook_old = '''/* user32!SetWindowsHookExW/A — no hook machinery: honest NULL = "hook not
 * installed" (callers proceed unhooked, same as a failed install on Win). */
__attribute__((ms_abi)) void* mw_SetWindowsHookExW(int idhook, void* proc, void* hmod, uint32_t tid) {
    (void)idhook; (void)proc; (void)hmod; (void)tid;
    MW_TRACE("SetWindowsHookExW(%d) -> NULL (no hook machinery)", idhook);
    return NULL;
}
__attribute__((ms_abi)) void* mw_SetWindowsHookExA(int idhook, void* proc, void* hmod, uint32_t tid) {
    (void)idhook; (void)proc; (void)hmod; (void)tid;
    MW_TRACE("SetWindowsHookExA(%d) -> NULL (no hook machinery)", idhook);
    return NULL;
}
__attribute__((ms_abi)) int mw_UnhookWindowsHookEx(void* hhk) {
    (void)hhk; return 1; /* idempotent success */
}'''
replace(hook_old, section("hook"), "SW-1201 hooks")

# ---- SW-1154: SafeArray store (core: table/create/bounds/access) ----
sa_old = '''__attribute__((ms_abi)) void* mw_SafeArrayCreate(uint32_t vt, uint32_t dims, void* bounds) {
    (void)vt; (void)dims; (void)bounds;
    return NULL; /* honest: no SAFEARRAY store v1 */
}
__attribute__((ms_abi)) uint32_t mw_SafeArrayUnaccessData(void* sa) { (void)sa; return 0; /* S_OK */ }
__attribute__((ms_abi)) uint32_t mw_SafeArrayAccessData(void* sa, void** pv) {
    (void)sa;
    if (pv) *pv = NULL;
    return 0x80004001u;
}
__attribute__((ms_abi)) uint32_t mw_SafeArrayGetLBound(void* sa, uint32_t dim, int32_t* v) {
    (void)sa; (void)dim;
    if (v) *v = 0;
    return 0; /* S_OK */
}
__attribute__((ms_abi)) uint32_t mw_SafeArrayGetUBound(void* sa, uint32_t dim, int32_t* v) {
    (void)sa; (void)dim;
    if (v) *v = -1;
    return 0; /* S_OK: empty dimension */
}'''
replace(sa_old, section("safearray"), "SW-1154 SafeArray core")

# ---- SW-1154: PutElement replacement + new GetElement/Destroy ----
pe_old = '''__attribute__((ms_abi)) uint32_t mw_SafeArrayPutElement(void* sa, int32_t* idx, void* data) {
    (void)sa; (void)idx; (void)data;
    return 0; /* S_OK — v1: element writes accepted, no store */
}'''
replace(pe_old, section("safearray2"), "SW-1154 Put + new Get/Destroy")

# ---- SW-1155: polygon scanline ----
poly_old = '''__attribute__((ms_abi)) void* mw_CreatePolygonRgn(const void* pts, int n, int fill) {
    (void)fill;
    if (!pts || n < 1) return NULL;
    const int32_t* p = (const int32_t*)pts;
    int32_t l = p[0], t = p[1], r = p[0], b = p[1];
    for (int i = 1; i < n; i++) {
        int32_t x = p[2 * i], y = p[2 * i + 1];
        if (x < l) l = x; if (x > r) r = x;
        if (y < t) t = y; if (y > b) b = y;
    }
    MW_TRACE("[GDI] CreatePolygonRgn(n=%d) -> bbox v1", n);
    return mw_CreateRectRgn(l, t, r, b);
}'''
replace(poly_old, section("polygon"), "SW-1155 polygon scanline")

# ---- SW-1158: CreateDIBitmap CBM_INIT ----
dib_old = '''__attribute__((ms_abi)) void* mw_CreateDIBitmap(void* hdc, const void* bmih, uint32_t init,
        const void* bits, const void* pbmi, uint32_t usage) {
    (void)init; (void)bits; (void)usage;
    const int32_t* hd = (const int32_t*)(bmih ? bmih : pbmi);
    if (!hd) return NULL;
    int w = hd[1], hh = hd[3]; /* BITMAPINFOHEADER: +4 biWidth, +12 biHeight */
    if (hh < 0) hh = -hh;
    void* hb = mw_CreateCompatibleBitmap(hdc, w, hh);
    MW_TRACE("[GDI] CreateDIBitmap(%dx%d) -> %p (blank surface v1)", w, hh, hb);
    return hb;
}'''
replace(dib_old, section("dibitmap"), "SW-1158 CreateDIBitmap")

# ---- SW-1159: AlphaBlend real blend ----
ab_old = '''__attribute__((ms_abi)) int mw_AlphaBlend(void* hdcDst, int dx, int dy, int dw, int dh,
        void* hdcSrc, int sx, int sy, int sw, int sh, uint32_t blendfn) {
    (void)blendfn; /* v1: source copy, no alpha blend; rop=SRCCOPY */
    return mw_StretchBlt(hdcDst, dx, dy, dw, dh, hdcSrc, sx, sy, sw, sh, 0x00CC0020u);
}'''
replace(ab_old, section("alphablend"), "SW-1159 AlphaBlend")

# ---- SW-1160: K32 module list ----
k32_old = '''__attribute__((ms_abi)) int mw_K32EnumProcessModules(void* proc, void** mods, uint32_t cb, uint32_t* needed) {
    (void)proc;
    if (needed) *needed = 8; /* one module: the image */
    if (!mods || cb < 8) return 0; /* Windows returns FALSE when buffer too small */
    mods[0] = g_image_base;
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_K32GetModuleBaseNameW(void* proc, void* mod, uint16_t* out, uint32_t size) {
    (void)proc; (void)mod;
    if (!out || !size) return 0;
    const char* p = get_exe_path();
    const char* base = p;
    for (const char* q = p; q && *q; q++) if (*q == '/' || *q == '\\\\') base = q + 1;
    uint32_t n = 0;
    while (base[n] && n + 1 < size) { out[n] = (uint16_t)(uint8_t)base[n]; n++; }
    out[n] = 0;
    return n;
}'''
replace(k32_old, section("k32"), "SW-1160 K32 modules")

# ---- SW-1202: errno bridge (stat64 family) ----
replace('''    struct stat64 st64;
    int ret = fstat64(fd, &st64);
    if (ret == 0 && st) {''',
        '''    struct stat64 st64;
    int ret = fstat64(fd, &st64);
    if (ret != 0) { g_errno_storage = errno; return ret; } /* SW-1202 errno bridge */
    if (st) {''', "SW-1202 _fstat64 bridge")

replace('''    struct stat64 st64;
    int ret = stat64(path, &st64);
    if (ret == 0 && st) {''',
        '''    struct stat64 st64;
    int ret = stat64(path, &st64);
    if (ret != 0) { g_errno_storage = errno; return ret; } /* SW-1202 errno bridge: guest read stale EPERM (zstd positional output) */
    if (st) {''', "SW-1202 _stat64 bridge")

replace('''    if (stat(path, &s) != 0) return -1;''',
        '''    if (stat(path, &s) != 0) { g_errno_storage = errno; return -1; } /* SW-1202 errno bridge */''',
        "SW-1202 _stat64i32 bridge")

# ---- ENTRY additions ----
entry_anchor = '''    ENTRY("OLEAUT32.DLL", "SafeArrayPutElement",                mw_SafeArrayPutElement),'''
replace(entry_anchor,
        entry_anchor + '''
    ENTRY("OLEAUT32.DLL", "SafeArrayGetElement",                mw_SafeArrayGetElement),
    ENTRY("OLEAUT32.DLL", "SafeArrayDestroy",                   mw_SafeArrayDestroy),''',
        "ENTRY SafeArrayGetElement/Destroy")

with open(LOADER, "w") as f:
    f.write(src)
print("splice complete")
