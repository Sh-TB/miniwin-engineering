#!/usr/bin/env python3
# SWEEP-028 engine edits: MWBmp view format + format-aware DIB paths.
# Applies the 10 surgical loader.c changes idempotently (exact-string).
import sys

LOADER = 'src/loader/loader.c'
txt = open(LOADER, encoding='utf-8').read()
if 'mwb_row(MWBmp* b, int y)' in txt:
    print('engine edits already applied'); sys.exit(0)

EDITS = []

# E1: struct + helpers
EDITS.append((
"""typedef struct { uint8_t* px; int w, h; } MWBmp;

static void mwb_set(MWBmp* b, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) return;
    uint8_t* p = b->px + 3 * ((size_t)y * b->w + x);
    p[0] = (uint8_t)(c & 0xFF); p[1] = (uint8_t)((c >> 8) & 0xFF); p[2] = (uint8_t)((c >> 16) & 0xFF);
}""",
"""/* SWEEP-028 (SW-1101 decode): bitmaps gain a VIEW format. vbpp==0 keeps the
 * legacy 24bpp engine raster byte-exact (CreateCompatibleBitmap/LoadImage/
 * window surfaces never set it). CreateDIBSection sets vbpp/vtop/vstride to
 * the GUEST-requested format: 32bpp top-down DIBs (WinMerge imagelist path)
 * previously underflowed calloc((size_t)w*(size_t)h,3) with h<0 -> NULL ->
 * guest RGBA loop wrote at NULL+off (SIGSEGV RVA 0x12d0c2, x3 deterministic).
 * Even non-NULL 32bpp requests overflowed the 3B/px allocation. */
typedef struct { uint8_t* px; int w, h; uint16_t vbpp; uint16_t vtop; uint32_t vstride; } MWBmp;

/* row start honoring the view format (legacy: w*3 stride) */
static inline uint8_t* mwb_row(MWBmp* b, int y) {
    uint32_t stride = b->vbpp ? b->vstride : (uint32_t)b->w * 3u;
    return b->px + (size_t)stride * (size_t)y;
}
static void mwb_set(MWBmp* b, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) return;
    uint8_t* p;
    switch (b->vbpp) {
    case 32: /* BGRA, alpha forced 0xFF on engine draws (honest opaque) */
        p = mwb_row(b, y) + (size_t)x * 4u;
        p[0] = (uint8_t)(c & 0xFF); p[1] = (uint8_t)((c >> 8) & 0xFF);
        p[2] = (uint8_t)((c >> 16) & 0xFF); p[3] = 0xFF;
        return;
    case 16: /* 5-6-5 (matches mw_dib_row_from_bgr24) */
        {
            uint16_t v = (uint16_t)((((c >> 16) & 0xF8u) << 8) |
                                    (((c >> 8) & 0xFCu) << 3) |
                                    ((c & 0xF8u) >> 3));
            p = mwb_row(b, y) + (size_t)x * 2u;
            p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8);
        }
        return;
    default: /* legacy 3B/px BGR — byte-identical pre-SWEEP-028 behavior */
        p = b->px + 3 * ((size_t)y * b->w + x);
        p[0] = (uint8_t)(c & 0xFF); p[1] = (uint8_t)((c >> 8) & 0xFF); p[2] = (uint8_t)((c >> 16) & 0xFF);
    }
}
/* format-aware pixel read -> 0x00BBGGRR (engine color order) */
static uint32_t mwb_get(MWBmp* b, int x, int y) {
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) return 0;
    const uint8_t* p;
    switch (b->vbpp) {
    case 32:
        p = mwb_row(b, y) + (size_t)x * 4u;
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    case 16:
        {
            p = mwb_row(b, y) + (size_t)x * 2u;
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            return ((((uint32_t)((v >> 11) & 0x1F) * 255u) / 31u) << 16) |
                   ((((uint32_t)((v >> 5) & 0x3F) * 255u) / 63u) << 8) |
                   (((uint32_t)(v & 0x1F) * 255u) / 31u);
        }
    default:
        p = b->px + 3 * ((size_t)y * b->w + x);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    }
}"""))

# E2/E3: StretchBlt + BitBlt copy loops
BLIT_OLD = """            memcpy(D->px + 3 * ((size_t)%s * (size_t)D->w + (size_t)%s),
                   S->px + 3 * ((size_t)%s * (size_t)S->w + (size_t)%s), 3);"""
for dyy, dxx, syy, sxx in [('dy', 'dx', 'sy', 'sx'), ('dy', 'dx', 'sy', 'sx')]:
    pass  # both loops share the same text; handled via replace-count below

BLIT_NEW = "            mwb_set(D, dx, dy, mwb_get(S, sx, sy)); /* SWEEP-028: view-format aware */"
for old in ["""            memcpy(D->px + 3 * ((size_t)dy * (size_t)D->w + (size_t)dx),
                   S->px + 3 * ((size_t)sy * (size_t)S->w + (size_t)sx), 3);"""]:
    EDITS.append((old, BLIT_NEW))

# E4: GetPixel
EDITS.append((
"""__attribute__((ms_abi)) uint32_t mw_GetPixel(void* hdc, int x, int y) {
    MWBmp* S = mw_dc_surface(hdc);
    if (!S || x < 0 || y < 0 || x >= S->w || y >= S->h) return 0xFFFFFFFFu; /* CLR_INVALID */
    const uint8_t* p = S->px + 3 * ((size_t)y * S->w + x);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}""",
"""__attribute__((ms_abi)) uint32_t mw_GetPixel(void* hdc, int x, int y) {
    MWBmp* S = mw_dc_surface(hdc);
    if (!S || x < 0 || y < 0 || x >= S->w || y >= S->h) return 0xFFFFFFFFu; /* CLR_INVALID */
    return mwb_get(S, x, y); /* SWEEP-028: view-format aware */
}"""))

# E5: PatBlt DSTINVERT
EDITS.append((
"""        } else if (rop == 0x00550009u) {
            for (int j = y; j < y + h; j++)
                for (int i = x; i < x + w; i++) {
                    uint8_t* p = S->px + 3 * ((size_t)j * S->w + i);
                    p[0] = (uint8_t)~p[0]; p[1] = (uint8_t)~p[1]; p[2] = (uint8_t)~p[2];
                }
        } else {""",
"""        } else if (rop == 0x00550009u) {
            for (int j = y; j < y + h; j++)
                for (int i = x; i < x + w; i++) {
                    uint32_t v = mwb_get(S, i, j); /* SWEEP-028: view-format aware */
                    mwb_set(S, i, j, (~(v & 0xFFu) & 0xFFu) |
                                     ((~((v >> 8) & 0xFFu) & 0xFFu) << 8) |
                                     ((~((v >> 16) & 0xFFu) & 0xFFu) << 16));
                }
        } else {"""))

# E6: ImageList_Add strip copy
EDITS.append((
"""    for (int j = 0; j < n; j++)
        for (int y = 0; y < L->cy; y++)
            memcpy(L->px + ((size_t)(L->count + j) * L->cy + y) * L->cx * 3,
                   src->px + ((size_t)y * src->w + j * L->cx) * 3,
                   (size_t)L->cx * 3);""",
"""    for (int j = 0; j < n; j++)
        for (int y = 0; y < L->cy; y++) {
            uint8_t* dst = L->px + ((size_t)(L->count + j) * L->cy + y) * L->cx * 3;
            if (src->vbpp == 0) { /* legacy 3B/px source: fast memcpy path */
                memcpy(dst, src->px + ((size_t)y * src->w + j * L->cx) * 3,
                       (size_t)L->cx * 3);
            } else { /* SWEEP-028: convert from view format (32/16bpp DIBs) */
                for (int x = 0; x < L->cx; x++) {
                    uint32_t c = mwb_get(src, j * L->cx + x, y);
                    dst[(size_t)x * 3 + 0] = (uint8_t)(c & 0xFF);
                    dst[(size_t)x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
                    dst[(size_t)x * 3 + 2] = (uint8_t)((c >> 16) & 0xFF);
                }
            }
        }"""))

# E7: CreateDIBSection root-cause fix
EDITS.append((
""" * hand the guest a real pointer to the pixels. 32bpp BI_RGB input is down-
 * converted on use by Blt-class callers; here we store 24bpp honestly and
 * document the limitation. Negative biHeight (top-down DIB) is accepted and
 * normalized — the pixels are zeroed either way. */
__attribute__((ms_abi)) void* mw_CreateDIBSection(void* hdc, const void* pbmi,
        uint32_t usage, void** ppvBits, void* hSection, uint32_t offset) {
    (void)hdc; (void)usage; (void)hSection; (void)offset;
    if (!pbmi || !ppvBits) return NULL;
    const uint8_t* bmi = (const uint8_t*)pbmi;
    int32_t w  = *(const int32_t*)(bmi + 4);
    int32_t h  = *(const int32_t*)(bmi + 8);
    uint16_t bpp = *(const uint16_t*)(bmi + 14);
    uint32_t comp = *(const uint32_t*)(bmi + 16);
    if (w <= 0 || h == 0) return NULL;
    if (comp != 0 /*BI_RGB*/ && comp != 3 /*BI_BITFIELDS*/) {
        MW_TRACE("CreateDIBSection -> NULL (compression %u unsupported)", comp);
        return NULL;
    }
    if (bpp != 16 && bpp != 24 && bpp != 32) {
        MW_TRACE("CreateDIBSection -> NULL (%u bpp unsupported)", bpp);
        return NULL;
    }
    for (int i = 0; i < MW_MEMDC_MAX; i++) {
        if (!g_bitmap_live[i]) {
            g_bitmap_live[i] = 1;
            g_bitmap_obj[i].w = w; g_bitmap_obj[i].h = h;
            g_bitmap_obj[i].px = (uint8_t*)calloc((size_t)w * (size_t)h, 3);
            if (!g_bitmap_obj[i].px) { g_bitmap_live[i] = 0; return NULL; }
            *ppvBits = g_bitmap_obj[i].px; /* REAL pixel pointer, 3B/px BGR */
            MW_TRACE("CreateDIBSection(%dx%d %ubpp) -> 0x%08x bits=%p",
                     w, h, bpp, MW_HBITMAP_BASE | (unsigned)(i + 1), (void*)*ppvBits);
            return (void*)(uintptr_t)(MW_HBITMAP_BASE | (unsigned)(i + 1));
        }
    }
    MW_TRACE("CreateDIBSection -> NULL (table full)");
    return NULL;
}""",
""" * hand the guest a real pointer to the pixels. SWEEP-028: the pool stores the
 * GUEST-requested view format (vbpp/vtop/vstride on MWBmp) — 16/24/32bpp and
 * top-down all honest; engine reads/writes go through mwb_get/mwb_set. */
__attribute__((ms_abi)) void* mw_CreateDIBSection(void* hdc, const void* pbmi,
        uint32_t usage, void** ppvBits, void* hSection, uint32_t offset) {
    (void)hdc; (void)usage; (void)hSection; (void)offset;
    if (!pbmi || !ppvBits) return NULL;
    const uint8_t* bmi = (const uint8_t*)pbmi;
    int32_t w  = *(const int32_t*)(bmi + 4);
    int32_t h  = *(const int32_t*)(bmi + 8);
    uint16_t bpp = *(const uint16_t*)(bmi + 14);
    uint32_t comp = *(const uint32_t*)(bmi + 16);
    if (w <= 0 || h == 0) {
        MW_TRACE("CreateDIBSection -> NULL (bad dims %dx%d)", w, h);
        return NULL;
    }
    if (comp != 0 /*BI_RGB*/ && comp != 3 /*BI_BITFIELDS*/) {
        MW_TRACE("CreateDIBSection -> NULL (compression %u unsupported)", comp);
        return NULL;
    }
    if (bpp != 16 && bpp != 24 && bpp != 32) {
        MW_TRACE("CreateDIBSection -> NULL (%u bpp unsupported)", bpp);
        return NULL;
    }
    /* SWEEP-028 (SW-1101 root cause): h<0 = TOP-DOWN DIB. Old code computed
     * (size_t)w*(size_t)h -> underflow -> calloc failed -> NULL with NO trace;
     * WinMerge's 32bpp top-down toolbar DIB then hit SelectObject(NULL) and
     * its RGBA loop wrote at NULL+0x154 (SIGSEGV RVA 0x12d0c2). */
    int32_t abs_h = (h > 0) ? h : -h;
    uint32_t vstride = ((uint32_t)w * (uint32_t)bpp + 31u) / 32u * 4u;
    for (int i = 0; i < MW_MEMDC_MAX; i++) {
        if (!g_bitmap_live[i]) {
            g_bitmap_live[i] = 1;
            g_bitmap_obj[i].w = w; g_bitmap_obj[i].h = abs_h;
            g_bitmap_obj[i].vbpp = bpp;
            g_bitmap_obj[i].vtop = (h < 0) ? 1 : 0;
            g_bitmap_obj[i].vstride = vstride;
            g_bitmap_obj[i].px = (uint8_t*)calloc((size_t)vstride * (size_t)abs_h, 1);
            if (!g_bitmap_obj[i].px) {
                g_bitmap_live[i] = 0;
                MW_TRACE("CreateDIBSection -> NULL (OOM %ux%u %ubpp)", (unsigned)w, (unsigned)abs_h, bpp);
                return NULL;
            }
            /* REAL pixel pointer in the GUEST's format/stride — ppvBits writes
             * are bytes the engine now reads back format-aware (mwb_get). */
            *ppvBits = g_bitmap_obj[i].px;
            MW_TRACE("CreateDIBSection(%dx%d %ubpp %s) -> 0x%08x bits=%p stride=%u",
                     w, h, bpp, (h < 0) ? "topdown" : "bottomup",
                     MW_HBITMAP_BASE | (unsigned)(i + 1), (void*)*ppvBits, vstride);
            return (void*)(uintptr_t)(MW_HBITMAP_BASE | (unsigned)(i + 1));
        }
    }
    MW_TRACE("CreateDIBSection -> NULL (table full)");
    return NULL;
}"""))

# E8: GetDIBits format-aware + top-down
EDITS.append((
"""    *(uint32_t*)(h8 + 0) = 40;              /* biSize */
    *(int32_t*)(h8 + 4) = b->w;             /* biWidth */
    *(int32_t*)(h8 + 8) = b->h;             /* biHeight: positive = bottom-up */
    *(uint16_t*)(h8 + 12) = 1;              /* biPlanes */
    *(uint16_t*)(h8 + 14) = want;           /* SWEEP-023 SW-981: honored */
    *(uint32_t*)(h8 + 16) = 0;              /* BI_RGB */
    *(uint32_t*)(h8 + 20) = 0;              /* biSizeImage len 0 for BI_RGB ok */
    *(uint32_t*)(h8 + 24) = mw_dib_stride((uint32_t)b->w, want) * (uint32_t)b->h;
    if (!bits) return 1;                    /* header-only probe */
    uint32_t n = lines ? lines : (uint32_t)b->h;
    if (n > (uint32_t)b->h) n = (uint32_t)b->h;
    uint32_t stride = mw_dib_stride((uint32_t)b->w, want);
    uint8_t* row24 = (uint8_t*)malloc((size_t)b->w * 3u);
    if (!row24) return 0;
    for (uint32_t y = 0; y < n; y++) {      /* bottom-up row order */
        memcpy(row24, b->px + (size_t)y * (size_t)b->w * 3u, (size_t)b->w * 3u);
        mw_dib_row_from_bgr24(row24, b->w, want,
                              (uint8_t*)bits + (size_t)(n - 1 - y) * stride, stride);
    }
    free(row24);""",
"""    *(uint32_t*)(h8 + 0) = 40;              /* biSize */
    *(int32_t*)(h8 + 4) = b->w;             /* biWidth */
    *(int32_t*)(h8 + 8) = b->vtop ? -b->h : b->h; /* SWEEP-028: honest sign */
    *(uint16_t*)(h8 + 12) = 1;              /* biPlanes */
    *(uint16_t*)(h8 + 14) = want;           /* SWEEP-023 SW-981: honored */
    *(uint32_t*)(h8 + 16) = 0;              /* BI_RGB */
    *(uint32_t*)(h8 + 20) = 0;              /* biSizeImage len 0 for BI_RGB ok */
    *(uint32_t*)(h8 + 24) = mw_dib_stride((uint32_t)b->w, want) * (uint32_t)b->h;
    if (!bits) return 1;                    /* header-only probe */
    uint32_t n = lines ? lines : (uint32_t)b->h;
    if (n > (uint32_t)b->h) n = (uint32_t)b->h;
    uint32_t stride = mw_dib_stride((uint32_t)b->w, want);
    uint8_t* row24 = (uint8_t*)malloc((size_t)b->w * 3u);
    if (!row24) return 0;
    if (b->vbpp == 0) {                     /* legacy 3B/px raster */
        for (uint32_t y = 0; y < n; y++) {  /* bottom-up row order */
            memcpy(row24, b->px + (size_t)y * (size_t)b->w * 3u, (size_t)b->w * 3u);
            mw_dib_row_from_bgr24(row24, b->w, want,
                                  (uint8_t*)bits + (size_t)(n - 1 - y) * stride, stride);
        }
    } else {                                /* SWEEP-028: view-format source */
        for (uint32_t y = 0; y < n; y++) {
            uint32_t dstY = b->vtop ? y : (n - 1 - y); /* top-down stays, bottom-up flips */
            for (int x = 0; x < b->w; x++) {
                uint32_t c = mwb_get(b, x, (int)y);
                row24[(size_t)x * 3 + 0] = (uint8_t)(c & 0xFF);
                row24[(size_t)x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
                row24[(size_t)x * 3 + 2] = (uint8_t)((c >> 16) & 0xFF);
            }
            mw_dib_row_from_bgr24(row24, b->w, want,
                                  (uint8_t*)bits + (size_t)dstY * stride, stride);
        }
    }
    free(row24);"""))

# E9: SetDIBits format-aware + top-down source
EDITS.append((
"""    for (uint32_t y = 0; y < n; y++) {      /* source is bottom-up */
        const uint8_t* src = (const uint8_t*)bits + (size_t)y * stride;
        mw_dib_row_to_bgr24(src, cw, sbpp, masks, row24);
        uint8_t* dst = b->px + (size_t)(n - 1 - y) * (size_t)b->w * 3u;
        memcpy(dst, row24, (size_t)cw * 3u);
    }""",
"""    for (uint32_t y = 0; y < n; y++) {      /* source is bottom-up */
        const uint8_t* src = (const uint8_t*)bits + (size_t)y * stride;
        mw_dib_row_to_bgr24(src, cw, sbpp, masks, row24);
        uint32_t dstY = (h > 0) ? (n - 1 - y) : y; /* SWEEP-028: top-down source */
        uint8_t* dst = b->px + (b->vbpp ? (size_t)b->vstride * (size_t)dstY
                                        : (size_t)dstY * (size_t)b->w * 3u);
        if (b->vbpp == 32) {
            for (int x = 0; x < cw; x++) {
                dst[(size_t)x * 4 + 0] = row24[(size_t)x * 3 + 0];
                dst[(size_t)x * 4 + 1] = row24[(size_t)x * 3 + 1];
                dst[(size_t)x * 4 + 2] = row24[(size_t)x * 3 + 2];
                dst[(size_t)x * 4 + 3] = 0xFF;
            }
        } else {
            memcpy(dst, row24, (size_t)cw * 3u); /* legacy 3B/px raster */
        }
    }"""))

# E10: StretchDIBits store via mwb_set
EDITS.append((
"""        for (int col = 0; col < dw; col++) {
            int sxx = (int)((long long)sw * col / dw);
            if (sxx >= w) continue;
            int ddx = dx + col;
            if (ddx < 0 || ddx >= D->w) continue;
            memcpy(D->px + 3 * ((size_t)ddy * (size_t)D->w + (size_t)ddx),
                   row24 + (size_t)sxx * 3u, 3);
            copied++;
        }
    }
    free(row24);
    MW_TRACE("[GDI] StretchDIBits %dx%d@%d,%d <- %dx%d %ubpp (%d px)",""",
"""        for (int col = 0; col < dw; col++) {
            int sxx = (int)((long long)sw * col / dw);
            if (sxx >= w) continue;
            int ddx = dx + col;
            if (ddx < 0 || ddx >= D->w) continue;
            mwb_set(D, ddx, ddy, /* SWEEP-028: view-format aware store */
                    (uint32_t)row24[(size_t)sxx * 3 + 0] |
                    ((uint32_t)row24[(size_t)sxx * 3 + 1] << 8) |
                    ((uint32_t)row24[(size_t)sxx * 3 + 2] << 16));
            copied++;
        }
    }
    free(row24);
    MW_TRACE("[GDI] StretchDIBits %dx%d@%d,%d <- %dx%d %ubpp (%d px)","""))

applied = 0
for old, new in EDITS:
    if old not in txt:
        print('EDIT MISS — aborting (checkpoint mismatch)'); sys.exit(1)
    txt = txt.replace(old, new, 1)
    applied += 1

open(LOADER, 'w', encoding='utf-8').write(txt)
print(f'engine edits applied: {applied}/{len(EDITS)}')
