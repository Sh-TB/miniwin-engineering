/* ==== SWEEP-031 IMPL START ====
 * Wagons: SW-1201 hook machinery, SW-1202 errno bridge (in stat64),
 * SW-1154 SafeArray real store, SW-1155 polygon scanline raster,
 * SW-1158 CreateDIBitmap CBM_INIT, SW-1159 AlphaBlend real blend,
 * SW-1160 K32EnumProcessModules /proc/self/maps.
 * Sections are spliced by scripts/sweep031_apply.py. */

/* ==== SECTION: hook ==== */
/* ---- SWEEP-031 SW-1201: real hook machinery ----
 * SWEEP-030 decode: winmergeu threw CMemoryException (MFC AfxThrowMemory-
 * Exception chain) the instant SetWindowsHookExW(5=WH_CBT) returned NULL
 * (trace: hook NULL at line 13430, RtlDispatchException 0xe06d7363 next
 * line; ThrowInfo 0x1404fd7b0 -> CMemoryException/CSimpleException/
 * CException/CObject/void*). No guest frame catches it by design — the
 * hook is NOT supposed to fail on real Windows. Fix: real install/
 * unhook machinery with live handle table, WH range validation
 * (WH_MSGFILTER=-1 thread-scoped, WH_MIN..WH_MOUSE_LL=0..14), honest
 * ERROR_INVALID_HANDLE on unhooking foreign handles. Firing model v1:
 * installed hooks observe; CBT/message event delivery = SW-1203. */
#define MW_HOOK_MAX 16
typedef struct { uint8_t live; int idhook; void* proc; void* hmod; uint32_t tid; } MwHook;
static MwHook g_hook[MW_HOOK_MAX];
#define MW_HOOK_BASE 0x00B30000u /* HHOOK cookies 0x00B30001+i (low16=0 handle pattern) */
static int mw_hook_valid_id(int idhook) {
    if (idhook == -1) return 1; /* WH_MSGFILTER (thread-scoped) */
    return idhook >= 0 && idhook <= 14; /* WH_MIN..WH_MOUSE_LL */
}
static void* mw_hook_install(int idhook, void* proc, void* hmod, uint32_t tid, const char* w) {
    MW_TRACE("SetWindowsHookEx%s(id=%d, proc=%p, hmod=%p, tid=%u)", w, idhook, proc, hmod, tid);
    if (!proc || !mw_hook_valid_id(idhook) || (idhook == -1 && tid == 0)) {
        mw_SetLastError(87); /* ERROR_INVALID_PARAMETER */
        MW_TRACE("  -> NULL (invalid args)");
        return NULL;
    }
    for (int i = 0; i < MW_HOOK_MAX; i++) {
        if (g_hook[i].live) continue;
        g_hook[i].live = 1; g_hook[i].idhook = idhook;
        g_hook[i].proc = proc; g_hook[i].hmod = hmod; g_hook[i].tid = tid;
        void* h = (void*)(uintptr_t)(MW_HOOK_BASE | (uintptr_t)(i + 1));
        MW_TRACE("  -> HHOOK %p", h);
        return h;
    }
    mw_SetLastError(5); /* ERROR_ACCESS_DENIED: hook table full */
    MW_TRACE("  -> NULL (table full)");
    return NULL;
}
__attribute__((ms_abi)) void* mw_SetWindowsHookExW(int idhook, void* proc, void* hmod, uint32_t tid) {
    return mw_hook_install(idhook, proc, hmod, tid, "W");
}
__attribute__((ms_abi)) void* mw_SetWindowsHookExA(int idhook, void* proc, void* hmod, uint32_t tid) {
    return mw_hook_install(idhook, proc, hmod, tid, "A");
}
__attribute__((ms_abi)) int mw_UnhookWindowsHookEx(void* hhk) {
    uintptr_t v = (uintptr_t)hhk;
    if ((v & 0xFFFF0000u) == MW_HOOK_BASE) {
        int i = (int)(v & 0xFFFFu) - 1;
        if (i >= 0 && i < MW_HOOK_MAX && g_hook[i].live) {
            MW_TRACE("UnhookWindowsHookEx(%p) -> TRUE (id=%d)", hhk, g_hook[i].idhook);
            g_hook[i].live = 0;
            return 1;
        }
    }
    MW_TRACE("UnhookWindowsHookEx(%p) -> FALSE (bad handle)", hhk);
    mw_SetLastError(1400); /* ERROR_INVALID_HANDLE */
    return 0;
}

/* ==== SECTION: safearray ==== */
/* ---- SWEEP-031 SW-1154: SafeArray real element store (SW-1148 pattern) ----
 * Real SAFEARRAY model: per-array {vt, dims, bounds, elemsz, data}.
 * Put/Get copy vt-sized elements through the flat (row-major, last dim
 * contiguous) index; AccessData exposes the raw store. VT_BSTR/VT_UNKNOWN
 * elements copy the guest pointer (guest heap owns the object) — the
 * SW-1148 honest-copy discipline. */
#define MW_SA_MAX 16
typedef struct {
    uint8_t live; uint16_t vt; uint16_t dims;
    uint32_t elemsz; uint32_t total;
    int32_t lb[8], nEl[8];
    uint8_t* data;
} MwSa;
static MwSa g_sa[MW_SA_MAX];
#define MW_SA_BASE 0x00B40000u /* SAFEARRAY cookies 0x00B40001+i (low16=0 handle pattern) */
static int mw_sa_slot(void* h) {
    uintptr_t v = (uintptr_t)h;
    if ((v & 0xFFFF0000u) != MW_SA_BASE) return -1;
    int i = (int)(v & 0xFFFFu) - 1;
    return (i >= 0 && i < MW_SA_MAX && g_sa[i].live) ? i : -1;
}
static uint32_t mw_sa_elemsz(uint32_t vt) {
    switch (vt & 0xFFu) {
    case 0x02: return 2;   /* VT_I2 */
    case 0x03: return 4;   /* VT_I4 */
    case 0x04: return 4;   /* VT_R4 */
    case 0x05: return 8;   /* VT_R8 */
    case 0x06: return 8;   /* VT_CY */
    case 0x07: return 8;   /* VT_DATE */
    case 0x08: return 8;   /* VT_BSTR */
    case 0x09: return 8;   /* VT_DISPATCH */
    case 0x0A: return 4;   /* VT_ERROR */
    case 0x0B: return 2;   /* VT_BOOL */
    case 0x0C: return 24;  /* VT_VARIANT (x64) */
    case 0x0D: return 8;   /* VT_UNKNOWN */
    case 0x0E: return 16;  /* VT_DECIMAL */
    case 0x10: return 1;   /* VT_I1 */
    case 0x11: return 1;   /* VT_UI1 */
    case 0x12: return 2;   /* VT_UI2 */
    case 0x13: return 4;   /* VT_UI4 */
    case 0x14: return 8;   /* VT_I8 */
    case 0x15: return 8;   /* VT_UI8 */
    case 0x16: return 4;   /* VT_INT */
    case 0x17: return 4;   /* VT_UINT */
    default:   return 8;
    }
}
__attribute__((ms_abi)) void* mw_SafeArrayCreate(uint32_t vt, uint32_t dims, void* bounds) {
    MW_TRACE("[OLE] SafeArrayCreate(vt=%u dims=%u)", vt, dims);
    if (!bounds || dims < 1 || dims > 8) return NULL;
    const uint32_t* b = (const uint32_t*)bounds; /* {cElements, lLbound} pairs */
    uint64_t total = 1;
    for (uint32_t d = 0; d < dims; d++) {
        uint32_t c = b[2 * d];
        if (c == 0 || c > 0x100000u) return NULL;
        total *= c;
        if (total > 0x1000000u) return NULL; /* 16M elements cap */
    }
    uint32_t elemsz = mw_sa_elemsz(vt);
    for (int i = 0; i < MW_SA_MAX; i++) {
        if (g_sa[i].live) continue;
        memset(&g_sa[i], 0, sizeof(MwSa));
        g_sa[i].live = 1; g_sa[i].vt = (uint16_t)vt; g_sa[i].dims = (uint16_t)dims;
        g_sa[i].elemsz = elemsz; g_sa[i].total = (uint32_t)total;
        for (uint32_t d = 0; d < dims; d++) {
            g_sa[i].nEl[d] = (int32_t)b[2 * d];
            g_sa[i].lb[d] = (int32_t)b[2 * d + 1];
        }
        g_sa[i].data = (uint8_t*)calloc((size_t)total, elemsz);
        if (!g_sa[i].data) { g_sa[i].live = 0; return NULL; }
        void* h = (void*)(uintptr_t)(MW_SA_BASE | (uintptr_t)(i + 1));
        MW_TRACE("  -> %p elemsz=%u total=%u", h, elemsz, (uint32_t)total);
        return h;
    }
    return NULL;
}
__attribute__((ms_abi)) uint32_t mw_SafeArrayGetLBound(void* sa, uint32_t dim, int32_t* v) {
    int s = mw_sa_slot(sa);
    if (s < 0 || dim < 1 || dim > g_sa[s].dims) return 0x8002000Bu; /* DISP_E_BADINDEX */
    if (v) *v = g_sa[s].lb[dim - 1];
    return 0; /* S_OK */
}
__attribute__((ms_abi)) uint32_t mw_SafeArrayGetUBound(void* sa, uint32_t dim, int32_t* v) {
    int s = mw_sa_slot(sa);
    if (s < 0 || dim < 1 || dim > g_sa[s].dims) return 0x8002000Bu;
    if (v) *v = g_sa[s].lb[dim - 1] + g_sa[s].nEl[dim - 1] - 1;
    return 0; /* S_OK */
}
__attribute__((ms_abi)) uint32_t mw_SafeArrayAccessData(void* sa, void** pv) {
    int s = mw_sa_slot(sa);
    if (s < 0) return 0x8002000Bu;
    if (pv) *pv = g_sa[s].data;
    return 0; /* S_OK (locking model v1: single-threaded guest) */
}
__attribute__((ms_abi)) uint32_t mw_SafeArrayUnaccessData(void* sa) {
    return mw_sa_slot(sa) < 0 ? 0x8002000Bu : 0; /* S_OK */
}
/* flat row-major index; last dim contiguous. returns -1 on range error */
static int64_t mw_sa_flat(int s, const int32_t* idx) {
    int64_t flat = 0, stride = 1;
    for (int d = (int)g_sa[s].dims - 1; d >= 0; d--) {
        int64_t off = (int64_t)idx[d] - g_sa[s].lb[d];
        if (idx[d] < g_sa[s].lb[d] || off >= g_sa[s].nEl[d]) return -1;
        flat += off * stride;
        stride *= g_sa[s].nEl[d];
    }
    return flat;
}

/* ==== SECTION: safearray2 ==== */
__attribute__((ms_abi)) uint32_t mw_SafeArrayPutElement(void* sa, int32_t* idx, void* data) {
    int s = mw_sa_slot(sa);
    if (!idx || !data) return 0x80070057u; /* E_INVALIDARG */
    if (s < 0) return 0x8002000Bu;
    int64_t flat = mw_sa_flat(s, idx);
    if (flat < 0) return 0x8002000Bu;
    memcpy(g_sa[s].data + (size_t)flat * g_sa[s].elemsz, data, g_sa[s].elemsz);
    MW_TRACE("[OLE] SafeArrayPutElement(%p [%d,%d]) vt=%u", sa, idx[0],
             g_sa[s].dims > 1 ? idx[1] : 0, g_sa[s].vt);
    return 0; /* S_OK */
}
__attribute__((ms_abi)) uint32_t mw_SafeArrayGetElement(void* sa, int32_t* idx, void* out) {
    int s = mw_sa_slot(sa);
    if (!idx || !out) return 0x80070057u;
    if (s < 0) return 0x8002000Bu;
    int64_t flat = mw_sa_flat(s, idx);
    if (flat < 0) return 0x8002000Bu;
    memcpy(out, g_sa[s].data + (size_t)flat * g_sa[s].elemsz, g_sa[s].elemsz);
    return 0; /* S_OK */
}
__attribute__((ms_abi)) uint32_t mw_SafeArrayDestroy(void* sa) {
    int s = mw_sa_slot(sa);
    if (s < 0) return 0x80070057u; /* E_INVALIDARG */
    free(g_sa[s].data);
    g_sa[s].data = NULL;
    g_sa[s].live = 0;
    MW_TRACE("[OLE] SafeArrayDestroy(%p)", sa);
    return 0; /* S_OK */
}

/* ==== SECTION: polygon ==== */
/* ---- SWEEP-031 SW-1155: true polygon raster (bbox v1 -> even-odd scanline) ----
 * Y-scan with the half-open edge rule (y in [min_ey, max_ey)), crossings
 * sorted pairwise into spans [xa, xb); spans merge vertically into the
 * region rect list (cap MW_RGN_RECTS — extras dropped deterministically,
 * documented). FillRgn/FrameRgn paint the spans = real raster. Fill mode
 * (ALTERNATE/WINDING) shares the even-odd rule v1. */
__attribute__((ms_abi)) void* mw_CreatePolygonRgn(const void* pts, int n, int fill) {
    (void)fill;
    if (!pts || n < 1) return NULL;
    const int32_t* p = (const int32_t*)pts;
    void* h = mw_rgn_alloc();
    if (!h) return NULL;
    int s = mw_rgn_slot(h);
    int32_t ymin = p[1], ymax = p[1];
    for (int i = 1; i < n; i++) {
        int32_t y = p[2 * i + 1];
        if (y < ymin) ymin = y; if (y > ymax) ymax = y;
    }
    if (n < 3 || ymin == ymax) {
        MW_TRACE("[GDI] CreatePolygonRgn(n=%d) -> NULLREGION (degenerate)", n);
        return h; /* zero-area: live region, 0 rects */
    }
    uint32_t spans = 0;
    for (int32_t y = ymin; y < ymax; y++) {
        int32_t xs[64]; int nx = 0;
        for (int e = 0; e < n && nx < 64; e++) {
            int32_t x1 = p[2 * e], y1 = p[2 * e + 1];
            int32_t x2 = p[2 * ((e + 1) % n)], y2 = p[2 * ((e + 1) % n) + 1];
            int32_t ey1 = y1 < y2 ? y1 : y2, ey2 = y1 < y2 ? y2 : y1;
            if (y < ey1 || y >= ey2) continue; /* half-open: no vertex double-count */
            int64_t dx = (int64_t)(x2 - x1), dy = (int64_t)(y2 - y1);
            xs[nx++] = (int32_t)((int64_t)x1 + (((int64_t)y - y1) * dx) / dy);
        }
        if (nx < 2) continue;
        for (int a = 1; a < nx; a++) { /* insertion sort */
            int32_t v = xs[a]; int b2 = a - 1;
            while (b2 >= 0 && xs[b2] > v) { xs[b2 + 1] = xs[b2]; b2--; }
            xs[b2 + 1] = v;
        }
        for (int k = 0; k + 1 < nx; k += 2) { /* even-odd spans */
            int32_t xa = xs[k], xb = xs[k + 1];
            if (xb <= xa) continue;
            spans++;
            int merged = 0;
            for (uint32_t i = 0; i < g_rgn[s].n && !merged; i++)
                if (g_rgn[s].r[i][0] == xa && g_rgn[s].r[i][2] == xb &&
                    g_rgn[s].r[i][3] == y) { g_rgn[s].r[i][3] = y + 1; merged = 1; }
            if (!merged && g_rgn[s].n < MW_RGN_RECTS) {
                g_rgn[s].r[g_rgn[s].n][0] = xa; g_rgn[s].r[g_rgn[s].n][1] = y;
                g_rgn[s].r[g_rgn[s].n][2] = xb; g_rgn[s].r[g_rgn[s].n][3] = y + 1;
                g_rgn[s].n++;
            }
        }
    }
    MW_TRACE("[GDI] CreatePolygonRgn(n=%d) -> %u spans, %u rects", n, spans, g_rgn[s].n);
    return h;
}

/* ==== SECTION: dibitmap ==== */
/* ---- SWEEP-031 SW-1158: CreateDIBitmap CBM_INIT real pixel copy ----
 * DIB source = bottom-up BI_RGB rows (biHeight<0 = top-down), 24bpp
 * (stride 4-aligned) or 32bpp; DIB bytes [B,G,R] land as the engine
 * 0x00BBGGRR-order constant (mwb_set byte0=B) — SetPixelV convention.
 * Consumers get real pixels, not a blank surface. */
__attribute__((ms_abi)) void* mw_CreateDIBitmap(void* hdc, const void* bmih, uint32_t init,
        const void* bits, const void* pbmi, uint32_t usage) {
    (void)usage;
    const void* hdr = bmih ? bmih : pbmi;
    if (!hdr) return NULL;
    const int32_t* hd = (const int32_t*)hdr;
    const uint8_t* hdb = (const uint8_t*)hdr;
    int w = hd[1], hh = hd[3]; /* biWidth @+4, biHeight @+12 */
    int topdown = 0;
    if (hh < 0) { topdown = 1; hh = -hh; }
    uint16_t bpp = (uint16_t)(hdb[14] | (hdb[15] << 8)); /* biBitCount @+14 */
    uint32_t comp = (uint32_t)hd[6];                     /* biCompression @+24 */
    void* hb = mw_CreateCompatibleBitmap(hdc, w, hh);
    if (hb && (init & 0x4u) && bits && comp == 0 && (bpp == 24 || bpp == 32)) {
        int slot = (int)((uintptr_t)hb & 0xFFu) - 1;
        MWBmp* b = (slot >= 0 && slot < MW_MEMDC_MAX && g_bitmap_live[slot])
                   ? &g_bitmap_obj[slot] : 0;
        if (b && b->px) {
            uint32_t bppc = bpp / 8;
            uint32_t stride = ((uint32_t)w * bppc + 3u) & ~3u;
            uint32_t copied = 0;
            for (int y = 0; y < hh && y < b->h; y++) {
                const uint8_t* row = (const uint8_t*)bits +
                    (size_t)stride * (size_t)(topdown ? y : (hh - 1 - y));
                for (int x = 0; x < w && x < b->w; x++) {
                    const uint8_t* q = row + (size_t)x * bppc;
                    mwb_set(b, x, y, (uint32_t)q[0] | ((uint32_t)q[1] << 8) |
                                     ((uint32_t)q[2] << 16));
                    copied++;
                }
            }
            MW_TRACE("[GDI] CreateDIBitmap CBM_INIT %dx%dx%u %s -> %u px",
                     w, hh, bpp, topdown ? "topdown" : "bottomup", copied);
            return hb;
        }
    }
    MW_TRACE("[GDI] CreateDIBitmap(%dx%d) -> %p (blank%s)", w, hh, hb,
             (init & 0x4u) ? ", CBM_INIT fmt unsupported" : "");
    return hb;
}

/* ==== SECTION: alphablend ==== */
/* ---- SWEEP-031 SW-1159: AlphaBlend real per-channel blend ----
 * BLENDFUNCTION = {Op, Flags, SrcConstantAlpha, AlphaFormat}. AC_SRC_OVER
 * only (the only op Windows defines). out = (src*a + dst*(255-a) + 127)/255
 * per channel; a = SCA. AC_SRC_ALPHA per-pixel alpha collapses to opaque
 * in the engine view (RGB, no alpha byte) — documented v1. SCA=255 is the
 * common toolbar case and equals a real SRCCOPY. */
__attribute__((ms_abi)) int mw_AlphaBlend(void* hdcDst, int dx, int dy, int dw, int dh,
        void* hdcSrc, int sx, int sy, int sw, int sh, uint32_t blendfn) {
    MWBmp* D = mw_dc_surface(hdcDst);
    MWBmp* S = mw_dc_surface(hdcSrc);
    if (!D || !S || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return 0;
    uint32_t op = blendfn & 0xFFu;
    uint32_t sca = (blendfn >> 16) & 0xFFu;
    uint32_t afmt = (blendfn >> 24) & 0xFFu;
    (void)afmt;
    if (op != 1u) { /* AC_SRC_OVER */
        MW_TRACE("[GDI] AlphaBlend op=%u -> FALSE (unsupported)", op);
        return 0;
    }
    uint32_t a = sca; /* SCA=255 -> pure copy fast path below */
    int blended = 0;
    for (int row = 0; row < dh; row++) {
        int sy2 = sy + (int)((long long)sh * row / dh);
        int dy2 = dy + row;
        if (sy2 < 0 || sy2 >= S->h || dy2 < 0 || dy2 >= D->h) continue;
        for (int col = 0; col < dw; col++) {
            int sx2 = sx + (int)((long long)sw * col / dw);
            int dx2 = dx + col;
            if (sx2 < 0 || sx2 >= S->w || dx2 < 0 || dx2 >= D->w) continue;
            uint32_t s = mwb_get(S, sx2, sy2);
            if (a == 255u) { mwb_set(D, dx2, dy2, s); blended++; continue; }
            uint32_t d = mwb_get(D, dx2, dy2), out = 0;
            for (int ch = 0; ch < 3; ch++) {
                uint32_t sc = (s >> (8 * ch)) & 0xFFu, dcc = (d >> (8 * ch)) & 0xFFu;
                out |= (((sc * a + dcc * (255u - a) + 127u) / 255u) << (8 * ch));
            }
            mwb_set(D, dx2, dy2, out);
            blended++;
        }
    }
    MW_TRACE("[GDI] AlphaBlend %dx%d@%d,%d <- %dx%d@%d,%d sca=%u afmt=%u (%d px)",
             dw, dh, dx, dy, sw, sh, sx, sy, sca, afmt, blended);
    return 1;
}

/* ==== SECTION: k32 ==== */
/* ---- SWEEP-031 SW-1160: K32EnumProcessModules real module list ----
 * The engine process IS the module host: /proc/self/maps executable
 * file-backed mappings -> stable module cookies 0x00076001+i (contiguous
 * same-path regions coalesced). The guest image (g_image_base) leads the
 * list like the exe on Windows. K32GetModuleBaseNameW resolves both. */
#define MW_K32MOD_MAX 64
static struct { void* handle; uint64_t lo, hi; char name[256]; } g_k32mod[MW_K32MOD_MAX];
static int g_k32mod_n = 0, g_k32mod_done = 0;
static const char* mw_k32_basename(const char* p) {
    const char* base = p;
    for (const char* q = p; q && *q; q++) if (*q == '/' || *q == '\\') base = q + 1;
    return base;
}
static void mw_k32mod_scan(void) {
    if (g_k32mod_done) return;
    g_k32mod_done = 1;
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[768];
    while (fgets(line, sizeof line, f) && g_k32mod_n < MW_K32MOD_MAX) {
        uint64_t lo = 0, hi = 0; char perms[8] = {0}; char path[400] = {0};
        if (sscanf(line, "%lx-%lx %7s %*s %*s %*s %399s", &lo, &hi, perms, path) < 4)
            continue;
        if (perms[2] != 'x' || path[0] != '/') continue; /* exec + file-backed */
        if (g_k32mod_n && strcmp(g_k32mod[g_k32mod_n - 1].name, path) == 0) {
            g_k32mod[g_k32mod_n - 1].hi = hi; /* coalesce contiguous same-path */
            continue;
        }
        int slot = g_k32mod_n++;
        g_k32mod[slot].lo = lo; g_k32mod[slot].hi = hi;
        g_k32mod[slot].handle = (void*)(uintptr_t)(0x00B50001u + (uintptr_t)slot);
        snprintf(g_k32mod[slot].name, sizeof g_k32mod[slot].name, "%s", path);
    }
    fclose(f);
    MW_TRACE("[K32] module scan -> %d exec modules", g_k32mod_n);
}
__attribute__((ms_abi)) int mw_K32EnumProcessModules(void* proc, void** mods, uint32_t cb, uint32_t* needed) {
    (void)proc;
    mw_k32mod_scan();
    uint32_t n = (uint32_t)g_k32mod_n + 1; /* + guest image (exe first) */
    if (needed) *needed = n * 8;
    if (!mods || cb < n * 8) return 0; /* FALSE: buffer too small */
    uint32_t k = 0;
    mods[k++] = g_image_base;
    for (int i = 0; i < g_k32mod_n; i++) mods[k++] = g_k32mod[i].handle;
    MW_TRACE("[K32] EnumProcessModules -> %u modules", n);
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_K32GetModuleBaseNameW(void* proc, void* mod, uint16_t* out, uint32_t size) {
    (void)proc;
    if (!out || !size) return 0;
    const char* base = NULL;
    if ((void*)mod == g_image_base) {
        base = mw_k32_basename(g_exe_path ? g_exe_path : "");
    } else {
        for (int i = 0; i < g_k32mod_n; i++)
            if (g_k32mod[i].handle == mod) { base = mw_k32_basename(g_k32mod[i].name); break; }
    }
    if (!base || !*base) return 0;
    uint32_t n = 0;
    while (base[n] && n + 1 < size) { out[n] = (uint16_t)(uint8_t)base[n]; n++; }
    out[n] = 0;
    return n;
}

/* ==== SWEEP-031 IMPL END ==== */
