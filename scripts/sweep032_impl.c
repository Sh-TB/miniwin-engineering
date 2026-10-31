/* ==== SWEEP-032 IMPL START ====
 * Train 1 of 5 (user: (continue the train, but this time bring 5 trains, each with many wagons)).
 * Wagons: SW-1106 zoomit64 64MB cap lift + full 70-import surface (66 named
 * + COMCTL32.410/412/413 + SHLWAPI.219 ordinals via wine-mirror spec
 * cross-check 2026-10-21), SW-1152 RtlCaptureContext SSE/FPU capture,
 * engine GDI+ Status-code fix (NotImplemented is 6, not 4/ObjecBusy),
 * real bitmap pixel store for GdipBitmapGetPixel, real file IStream.
 * Sections are spliced by scripts/sweep032_apply.py. */

/* ==== SECTION: sse ==== */
/* ---- SW-1152 (SWEEP-032): full FPU/SSE capture in RtlCaptureContext.
 * SW-1103 landed integer regs + segments + EFlags but left the
 * FLOATING_POINT area zeroed ("documented approximation"). Consumers that
 * read XMM6-15 (callee-saved = true guest values across our ms_abi bridge)
 * or FltSave.ControlWord/StatusWord/MxCsr saw zeros. Now:
 *  - all 16 XMM low-128 read FIRST (before the compiler may emit memset
 *    SSE work), so volatile XMM0-5 hold caller-live values, XMM6-15 are
 *    real by ms_abi callee-save;
 *  - MxCsr = real stmxcsr (host executes guest FP natively), duplicated at
 *    CONTEXT+0x34 and FltSave+0x18 (CONTEXT+0x118) per winnt.h layout;
 *  - x87 ControlWord/StatusWord = real fnstcw/fnstsw; TagWord honest
 *    0xFFFF (all-empty) — documented approximation, decode is 8-way;
 *  - layout per winnt.h: Xmm0..15 = 0x1A0 + 16*i, FltSave = 0x100. */
__attribute__((ms_abi)) void mw_RtlCaptureContext(void* ctx) {
    if (!ctx) return;
    uint8_t xmm[16][16];
    __asm__ volatile (
        "movups %%xmm0,  %0  \n\t movups %%xmm1,  %1  \n\t"
        "movups %%xmm2,  %2  \n\t movups %%xmm3,  %3  \n\t"
        "movups %%xmm4,  %4  \n\t movups %%xmm5,  %5  \n\t"
        "movups %%xmm6,  %6  \n\t movups %%xmm7,  %7  \n\t"
        "movups %%xmm8,  %8  \n\t movups %%xmm9,  %9  \n\t"
        "movups %%xmm10, %10 \n\t movups %%xmm11, %11 \n\t"
        "movups %%xmm12, %12 \n\t movups %%xmm13, %13 \n\t"
        "movups %%xmm14, %14 \n\t movups %%xmm15, %15"
        : "=m"(xmm[0]),  "=m"(xmm[1]),  "=m"(xmm[2]),  "=m"(xmm[3]),
          "=m"(xmm[4]),  "=m"(xmm[5]),  "=m"(xmm[6]),  "=m"(xmm[7]),
          "=m"(xmm[8]),  "=m"(xmm[9]),  "=m"(xmm[10]), "=m"(xmm[11]),
          "=m"(xmm[12]), "=m"(xmm[13]), "=m"(xmm[14]), "=m"(xmm[15])
        : : "memory");
    uint32_t mxcsr = 0x1F80u;
    __asm__ volatile ("stmxcsr %0" : "=m"(mxcsr));
    uint16_t fcw = 0x027Fu, fsw = 0;
    __asm__ volatile ("fnstcw %0" : "=m"(fcw));
    __asm__ volatile ("fnstsw %0" : "=m"(fsw));
    uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t cs, ss, fl, rip;
    __asm__ volatile ("movq %%rax, %0" : "=r"(rax));
    __asm__ volatile ("movq %%rcx, %0" : "=r"(rcx));
    __asm__ volatile ("movq %%rdx, %0" : "=r"(rdx));
    __asm__ volatile ("movq %%rbx, %0" : "=r"(rbx));
    __asm__ volatile ("movq %%rsp, %0" : "=r"(rsp));
    __asm__ volatile ("movq %%rbp, %0" : "=r"(rbp));
    __asm__ volatile ("movq %%rsi, %0" : "=r"(rsi));
    __asm__ volatile ("movq %%rdi, %0" : "=r"(rdi));
    __asm__ volatile ("movq %%r8,  %0" : "=r"(r8));
    __asm__ volatile ("movq %%r9,  %0" : "=r"(r9));
    __asm__ volatile ("movq %%r10, %0" : "=r"(r10));
    __asm__ volatile ("movq %%r11, %0" : "=r"(r11));
    __asm__ volatile ("movq %%r12, %0" : "=r"(r12));
    __asm__ volatile ("movq %%r13, %0" : "=r"(r13));
    __asm__ volatile ("movq %%r14, %0" : "=r"(r14));
    __asm__ volatile ("movq %%r15, %0" : "=r"(r15));
    __asm__ volatile ("movq %%cs,  %0" : "=r"(cs));
    __asm__ volatile ("movq %%ss,  %0" : "=r"(ss));
    __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(fl));
    rip = (uint64_t)__builtin_return_address(0);
    uint8_t* c = (uint8_t*)ctx;
    memset(c, 0, 0x4D0);
    *(uint32_t*)(c + 0x30) = 0x10000Fu; /* CONTEXT_AMD64|CONTROL|INTEGER|SEGMENTS|FLOATING_POINT */
    *(uint32_t*)(c + 0x34) = mxcsr;
    *(uint16_t*)(c + 0x38) = (uint16_t)cs;
    *(uint16_t*)(c + 0x42) = (uint16_t)ss;
    *(uint32_t*)(c + 0x44) = (uint32_t)fl;
    *(uint64_t*)(c + 0x78) = rax;
    *(uint64_t*)(c + 0x80) = rcx;
    *(uint64_t*)(c + 0x88) = rdx;
    *(uint64_t*)(c + 0x90) = rbx;
    *(uint64_t*)(c + 0x98) = rsp + 8; /* caller RSP: RA slot at entry */
    *(uint64_t*)(c + 0xA0) = rbp;
    *(uint64_t*)(c + 0xA8) = rsi;
    *(uint64_t*)(c + 0xB0) = rdi;
    *(uint64_t*)(c + 0xB8) = r8;
    *(uint64_t*)(c + 0xC0) = r9;
    *(uint64_t*)(c + 0xC8) = r10;
    *(uint64_t*)(c + 0xD0) = r11;
    *(uint64_t*)(c + 0xD8) = r12;
    *(uint64_t*)(c + 0xE0) = r13;
    *(uint64_t*)(c + 0xE8) = r14;
    *(uint64_t*)(c + 0xF0) = r15;
    *(uint64_t*)(c + 0xF8) = rip;
    /* XSAVE_FORMAT (FltSave) at CONTEXT+0x100 */
    *(uint16_t*)(c + 0x100) = fcw;      /* ControlWord */
    *(uint16_t*)(c + 0x102) = fsw;      /* StatusWord */
    *(uint16_t*)(c + 0x104) = 0xFFFFu;  /* TagWord: honest approximation */
    *(uint32_t*)(c + 0x118) = mxcsr;    /* FltSave.MxCsr */
    memcpy(c + 0x1A0, xmm, 16 * 16);    /* Xmm0..Xmm15 */
    MW_TRACE("RtlCaptureContext() rip=0x%lx mxcsr=0x%x (SW-1152 SSE captured)", rip, mxcsr);
}

/* ==== SECTION: gdobj ==== */
/* ---- SWEEP-032: GDI+ object store gains real state (SW-1106 zoomit64):
 * pen color/width/caps/join, bitmap pixels (real GetPixel), effect GUIDs.
 * Kinds: 1=graphics 2=pen 3=brush 4=path 5=bitmap 6=effect. ---- */
#define MW_GDOBJ_BASE 0x00B20000u
typedef struct {
    uint8_t  kind;
    /* pen state (kind 2) */
    uint32_t pen_color; float pen_width;
    uint32_t pen_startcap, pen_endcap, pen_linejoin;
    /* bitmap store (kind 5): 32bpp canonical, stride bytes */
    uint32_t w, h, stride; uint8_t* px; uint8_t px_owned;
    /* effect (kind 6) */
    uint8_t guid[16]; uint32_t effect_params;
} MwGdObj;
static MwGdObj g_gdobj[64];
static void* mw_gdobj_new(uint8_t kind) {
    for (int i = 0; i < 64; i++) {
        if (g_gdobj[i].kind) continue;
        memset(&g_gdobj[i], 0, sizeof(g_gdobj[i]));
        g_gdobj[i].kind = kind;
        if (kind == 2) g_gdobj[i].pen_width = 1.0f; /* GDI+ default width */
        return (void*)(uintptr_t)(MW_GDOBJ_BASE | (unsigned)(i + 1));
    }
    return NULL;
}
static MwGdObj* mw_gdobj_state(void* h) {
    uintptr_t v = (uintptr_t)h;
    if ((v & 0xFFFF0000u) != MW_GDOBJ_BASE) return NULL;
    int i = (int)(v & 0xFF) - 1;
    if (i < 0 || i >= 64) return NULL;
    return g_gdobj[i].kind ? &g_gdobj[i] : NULL;
}
static int mw_gdobj_kind(void* h) {
    MwGdObj* s = mw_gdobj_state(h);
    return s ? s->kind : 0;
}

/* ==== SECTION: pen ==== */
__attribute__((ms_abi)) uint32_t mw_GdipCreatePen1(uint32_t color, float width, uint32_t unit, void** pen) {
    if (!pen) return MW_GDIPLUS_INVALIDPARAM;
    MwGdObj* s = NULL;
    void* p = mw_gdobj_new(2);
    if (!p || !(s = mw_gdobj_state(p))) return MW_GDIPLUS_GENERICERR;
    s->pen_color = color;
    if (width > 0.0f) s->pen_width = width; /* 0 -> real GDI+ also stores 0; keep 1.0 default */
    s->pen_startcap = 0; s->pen_endcap = 0; s->pen_linejoin = 0;
    *pen = p;
    MW_TRACE("GdipCreatePen1(color=%08x width=%f) -> %p", color, width, p);
    return MW_GDIPLUS_OK;
}

/* ==== SECTION: bmpscan0 ==== */
__attribute__((ms_abi)) uint32_t mw_GdipCreateBitmapFromScan0(int w, int h, int stride, uint32_t fmt, void* bits, void** bmp) {
    if (w <= 0 || h <= 0 || !bmp) return MW_GDIPLUS_INVALIDPARAM;
    /* SW-1106: real pixel store (32bpp canonical) so GdipBitmapGetPixel is
     * a real read. External bits (Scan0) are copied once, owned by us. */
    uint32_t st = (stride > 0) ? (uint32_t)stride : (uint32_t)(w * 4);
    MwGdObj* s = NULL;
    void* b = mw_gdobj_new(5);
    if (!b || !(s = mw_gdobj_state(b))) return MW_GDIPLUS_GENERICERR;
    s->w = (uint32_t)w; s->h = (uint32_t)h; s->stride = st;
    s->px = (uint8_t*)calloc(1, (size_t)st * (size_t)h);
    if (!s->px) return 3u; /* GDI+ Status::OutOfMemory */
    s->px_owned = 1;
    if (bits) memcpy(s->px, bits, (size_t)st * (size_t)h);
    (void)fmt; /* pixel format normalized to 32bpp ARGB canonical */
    *bmp = b;
    MW_TRACE("GdipCreateBitmapFromScan0(%dx%d stride=%d fmt=%#x) -> %p", w, h, stride, fmt, b);
    return MW_GDIPLUS_OK;
}

/* ==== SECTION: gdiplus_wagons ==== */
/* ---- SW-1106 zoomit64 GDI+ tail (wine gdiplus semantics, 2026-10-21):
 * pen state real (store), shape draws status-honest v1 (matching the
 * established DrawLineI "status-honest, pixels unrendered" v1), effects
 * real object lifecycle with honest unrendered apply, GetPixel real read
 * from the pixel store, SaveImageToFile honest InvalidParameter (encoder
 * registry is empty — GdipGetImageEncoders returns 0 codecs). ---- */
static uint32_t mw_pen_state(void* pen, int which, uint32_t v) {
    MwGdObj* s = mw_gdobj_state(pen);
    if (!s || s->kind != 2) return MW_GDIPLUS_INVALIDPARAM;
    switch (which) {
    case 0: s->pen_linejoin = v; break;
    case 1: s->pen_startcap = v; break;
    case 2: s->pen_endcap = v; break;
    }
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipSetPenLineJoin(void* pen, uint32_t join) { return mw_pen_state(pen, 0, join); }
__attribute__((ms_abi)) uint32_t mw_GdipSetPenStartCap(void* pen, uint32_t cap) { return mw_pen_state(pen, 1, cap); }
__attribute__((ms_abi)) uint32_t mw_GdipSetPenEndCap(void* pen, uint32_t cap) { return mw_pen_state(pen, 2, cap); }
/* LineCap197819 = LineCap legacy entry point (start, end, dash cap) */
__attribute__((ms_abi)) uint32_t mw_GdipSetPenLineCap197819(void* pen, uint32_t start, uint32_t end, uint32_t dash) {
    (void)dash;
    uint32_t r = mw_pen_state(pen, 1, start);
    if (r) return r;
    return mw_pen_state(pen, 2, end);
}
/* Shape draws: validate graphics + brush/pen handles, status-honest v1. */
static uint32_t mw_gd_shape(void* g, void* h, int want_kind, const char* fn) {
    if (mw_gdobj_kind(g) != 1) return MW_GDIPLUS_INVALIDPARAM;
    if (h && mw_gdobj_kind(h) != want_kind) return MW_GDIPLUS_INVALIDPARAM;
    MW_TRACE("%s (status-honest v1, pixels unrendered)", fn);
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipDrawRectangleI(void* g, void* pen, int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
    return mw_gd_shape(g, pen, 2, "GdipDrawRectangleI");
}
__attribute__((ms_abi)) uint32_t mw_GdipDrawEllipse(void* g, void* pen, float x, float y, float w, float h) {
    (void)x; (void)y; (void)w; (void)h;
    return mw_gd_shape(g, pen, 2, "GdipDrawEllipse");
}
__attribute__((ms_abi)) uint32_t mw_GdipDrawEllipseI(void* g, void* pen, int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
    return mw_gd_shape(g, pen, 2, "GdipDrawEllipseI");
}
__attribute__((ms_abi)) uint32_t mw_GdipDrawArc(void* g, void* pen, float x, float y, float w, float h, float start, float sweep) {
    (void)x; (void)y; (void)w; (void)h; (void)start; (void)sweep;
    return mw_gd_shape(g, pen, 2, "GdipDrawArc");
}
__attribute__((ms_abi)) uint32_t mw_GdipFillRectangle(void* g, void* brush, float x, float y, float w, float h) {
    (void)x; (void)y; (void)w; (void)h;
    return mw_gd_shape(g, brush, 3, "GdipFillRectangle");
}
__attribute__((ms_abi)) uint32_t mw_GdipFillEllipse(void* g, void* brush, float x, float y, float w, float h) {
    (void)x; (void)y; (void)w; (void)h;
    return mw_gd_shape(g, brush, 3, "GdipFillEllipse");
}
__attribute__((ms_abi)) uint32_t mw_GdipFillEllipseI(void* g, void* brush, int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
    return mw_gd_shape(g, brush, 3, "GdipFillEllipseI");
}
__attribute__((ms_abi)) uint32_t mw_GdipFillPolygon(void* g, void* brush, const void* pts, uint32_t n, uint32_t fill) {
    (void)pts; (void)fill;
    if (mw_gdobj_kind(g) != 1) return MW_GDIPLUS_INVALIDPARAM;
    if (mw_gdobj_kind(brush) != 3) return MW_GDIPLUS_INVALIDPARAM;
    if (!pts || n < 2) return MW_GDIPLUS_INVALIDPARAM;
    MW_TRACE("GdipFillPolygon(n=%u) (status-honest v1, pixels unrendered)", n);
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipBitmapGetPixel(void* bmp, int x, int y, uint32_t* color) {
    MwGdObj* s = mw_gdobj_state(bmp);
    if (!s || s->kind != 5 || !s->px || !color) return MW_GDIPLUS_INVALIDPARAM;
    if (x < 0 || (uint32_t)x >= s->w || y < 0 || (uint32_t)y >= s->h)
        return MW_GDIPLUS_INVALIDPARAM;
    const uint8_t* p = s->px + (size_t)y * s->stride + (size_t)x * 4;
    *color = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    MW_TRACE("GdipBitmapGetPixel(%d,%d) -> %08x", x, y, *color);
    return MW_GDIPLUS_OK;
}
/* Effects: real object lifecycle (GUID stored), apply honest NotImplemented:
 * no filter engine — GDI+ Status::NotImplemented = 6 (SWEEP-032 engine fix). */
__attribute__((ms_abi)) uint32_t mw_GdipCreateEffect(const uint8_t* guid, void** effect) {
    if (!guid || !effect) return MW_GDIPLUS_INVALIDPARAM;
    void* e = mw_gdobj_new(6);
    MwGdObj* s = e ? mw_gdobj_state(e) : NULL;
    if (!s) return MW_GDIPLUS_GENERICERR;
    memcpy(s->guid, guid, 16);
    *effect = e;
    MW_TRACE("GdipCreateEffect(%02x%02x%02x%02x-...) -> %p", guid[0], guid[1], guid[2], guid[3], e);
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipDeleteEffect(void* effect) {
    return mw_gdobj_kind(effect) == 6 ? MW_GDIPLUS_OK : MW_GDIPLUS_INVALIDPARAM;
}
__attribute__((ms_abi)) uint32_t mw_GdipSetEffectParameters(void* effect, const void* params, uint32_t size) {
    MwGdObj* s = mw_gdobj_state(effect);
    if (!s || s->kind != 6) return MW_GDIPLUS_INVALIDPARAM;
    (void)params;
    s->effect_params = size;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipBitmapApplyEffect(void* bmp, void* effect, const void* roi, uint32_t flags, void* cb, void* cbd) {
    (void)roi; (void)flags; (void)cb; (void)cbd;
    if (mw_gdobj_kind(bmp) != 5 || mw_gdobj_kind(effect) != 6)
        return MW_GDIPLUS_INVALIDPARAM;
    MW_TRACE("GdipBitmapApplyEffect -> NotImplemented(6) (no filter engine)");
    return MW_GDIPLUS_NOTIMPL;
}
__attribute__((ms_abi)) uint32_t mw_GdipSaveImageToFile(void* img, const uint16_t* file, const void* clsid, const void* params) {
    (void)clsid; (void)params;
    if (mw_gdobj_kind(img) != 5) return MW_GDIPLUS_INVALIDPARAM;
    char u8[512];
    if (file) w3_w16_to_utf8z(file, u8, sizeof(u8)); else u8[0] = 0;
    MW_TRACE("GdipSaveImageToFile(%s) -> InvalidParameter (encoder registry empty: 0 codecs)", u8);
    return MW_GDIPLUS_INVALIDPARAM;
}

/* ==== SECTION: user32_wagons ==== */
/* ---- SW-1106 zoomit64 USER32 tail: real state where cheap (extra-info slot,
 * clip cursor, display affinity, clipboard housekeeping, send-input count,
 * monitor rect intersection, US-layout VkKeyScanW), typed-honest for the
 * host-impossible (layered attributes need a compositor). ---- */
static uintptr_t g_msg_extra = 0;
__attribute__((ms_abi)) void* mw_SetMessageExtraInfo(void* v) {
    void* old = (void*)g_msg_extra;
    g_msg_extra = (uintptr_t)v;
    return old;
}
__attribute__((ms_abi)) void* mw_GetMessageExtraInfo(void) { return (void*)g_msg_extra; }
static int g_clip_set = 0;
static int g_clip_l, g_clip_t, g_clip_r, g_clip_b;
__attribute__((ms_abi)) int mw_ClipCursor(const void* rect) {
    if (rect) {
        const int32_t* r = (const int32_t*)rect;
        g_clip_l = r[0]; g_clip_t = r[1]; g_clip_r = r[2]; g_clip_b = r[3];
        g_clip_set = 1;
        MW_TRACE("ClipCursor(l=%d t=%d r=%d b=%d) -> TRUE", g_clip_l, g_clip_t, g_clip_r, g_clip_b);
    } else {
        g_clip_set = 0;
        MW_TRACE("ClipCursor(NULL) -> TRUE (unclipped)");
    }
    return 1;
}
__attribute__((ms_abi)) int mw_GetClipCursor(void* rect) {
    if (!rect) return 0;
    int32_t* r = (int32_t*)rect;
    if (g_clip_set) { r[0] = g_clip_l; r[1] = g_clip_t; r[2] = g_clip_r; r[3] = g_clip_b; }
    else { /* full virtual screen (default: no clip active) */
        r[0] = mw_GetSystemMetrics(76); r[1] = mw_GetSystemMetrics(77);
        r[2] = r[0] + mw_GetSystemMetrics(78); r[3] = r[1] + mw_GetSystemMetrics(79);
    }
    return 1;
}
__attribute__((ms_abi)) int mw_VkKeyScanW(uint16_t ch) {
    /* US-layout table (SW-1106 zoomit64): return (shift<<8)|vk, -1 unmapped.
     * shift bits: 1=Shift 2=Ctrl 4=Alt. Real table for ASCII, -1 else. */
    int vk = -1, sh = 0;
    if (ch >= 'a' && ch <= 'z') { vk = ch - 'a' + 0x41; }
    else if (ch >= 'A' && ch <= 'Z') { vk = ch - 'A' + 0x41; sh = 1; }
    else if (ch >= '0' && ch <= '9') { vk = ch; }
    else switch (ch) {
    case ' ':  vk = 0x20; break;
    case '\t': vk = 0x09; break;
    case '\r': case '\n': vk = 0x0D; break;
    case '!': vk = 0x31; sh = 1; break; case '@': vk = 0x32; sh = 1; break;
    case '#': vk = 0x33; sh = 1; break; case '$': vk = 0x34; sh = 1; break;
    case '%': vk = 0x35; sh = 1; break; case '^': vk = 0x36; sh = 1; break;
    case '&': vk = 0x37; sh = 1; break; case '*': vk = 0x38; sh = 1; break;
    case '(': vk = 0x39; sh = 1; break; case ')': vk = 0x30; sh = 1; break;
    case '-': vk = 0xBD; break;    case '_': vk = 0xBD; sh = 1; break;
    case '=': vk = 0xBB; break;    case '+': vk = 0xBB; sh = 1; break;
    case '[': vk = 0xDB; break;    case '{': vk = 0xDB; sh = 1; break;
    case ']': vk = 0xDD; break;    case '}': vk = 0xDD; sh = 1; break;
    case '\\': vk = 0xDC; break;   case '|': vk = 0xDC; sh = 1; break;
    case ';': vk = 0xBA; break;    case ':': vk = 0xBA; sh = 1; break;
    case '\'': vk = 0xDE; break;   case '"': vk = 0xDE; sh = 1; break;
    case ',': vk = 0xBC; break;    case '<': vk = 0xBC; sh = 1; break;
    case '.': vk = 0xBE; break;    case '>': vk = 0xBE; sh = 1; break;
    case '/': vk = 0xBF; break;    case '?': vk = 0xBF; sh = 1; break;
    case '`': vk = 0xC0; break;    case '~': vk = 0xC0; sh = 1; break;
    default: return -1;
    }
    return (sh << 8) | vk;
}
static int g_disp_affinity = 0;
__attribute__((ms_abi)) int mw_SetWindowDisplayAffinity(void* hwnd, uint32_t affinity) {
    (void)hwnd;
    if (affinity != 0 && affinity != 1 && affinity != 0x11u) { /* WDA_NONE/MONITOR/EXCLUDEFROMCAPTURE */
        mw_SetLastError(87);
        return 0;
    }
    g_disp_affinity = (int)affinity;
    MW_TRACE("SetWindowDisplayAffinity(%u) -> TRUE", affinity);
    return 1;
}
__attribute__((ms_abi)) int mw_GetLayeredWindowAttributes(void* hwnd, uint32_t* key, uint8_t* alpha, uint32_t* flags) {
    (void)hwnd; (void)key; (void)alpha; (void)flags;
    mw_SetLastError(87); /* ERROR_INVALID_PARAMETER: window is not layered */
    MW_TRACE("GetLayeredWindowAttributes -> FALSE (window not layered)");
    return 0;
}
#define MONITOR_PRIMARY ((void*)0x00020001u)
__attribute__((ms_abi)) void* mw_MonitorFromRect(const void* rect, uint32_t flags) {
    if (rect) {
        const int32_t* r = (const int32_t*)rect;
        int32_t ml = 0, mt = 0;
        int32_t mr = mw_GetSystemMetrics(0), mb = mw_GetSystemMetrics(1);
        if (r[0] < mr && r[2] > ml && r[1] < mb && r[3] > mt) {
            MW_TRACE("MonitorFromRect -> primary (intersects)");
            return MONITOR_PRIMARY;
        }
    }
    if (flags == 0) { MW_TRACE("MonitorFromRect -> NULL (no intersect, DEFAULTTONULL)"); return NULL; }
    MW_TRACE("MonitorFromRect -> primary (DEFAULTTONEAREST/PRIMARY)");
    return MONITOR_PRIMARY;
}
__attribute__((ms_abi)) uint32_t mw_SendInput(uint32_t n, void* inputs, int cbSize) {
    if (cbSize != 40) { mw_SetLastError(87); return 0; } /* sizeof(INPUT) x64 = 40 */
    if (!inputs && n) { mw_SetLastError(87); return 0; }
    uint32_t ok = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t type = *(const uint32_t*)((const uint8_t*)inputs + (size_t)i * 40);
        if (type > 2) break; /* MOUSE=0 KEYBD=1 HARDWARE=2 */
        ok++;
    }
    MW_TRACE("SendInput(n=%u accepted=%u, headless inject)", n, ok);
    return ok;
}
__attribute__((ms_abi)) int mw_SendNotifyMessageW(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp) {
    (void)hwnd; (void)wp; (void)lp;
    MW_TRACE("SendNotifyMessageW(msg=%#x) posted-and-returned TRUE (headless)", msg);
    return 1; /* real: posts without waiting */
}
__attribute__((ms_abi)) int mw_ChangeDisplaySettingsExW(const uint16_t* dev, void* dm, void* hwnd, uint32_t flags, void* param) {
    (void)dev; (void)hwnd; (void)param;
    char u8[128] = {0};
    if (dev) w3_w16_to_utf8z(dev, u8, sizeof(u8));
    /* CDS_TEST(0x2) = capability query; NULL devmode + no real change = no-op.
     * Actual mode changes are impossible on the headless host -> FAILED(-1). */
    if (flags & 0x2u) { MW_TRACE("ChangeDisplaySettingsExW(%s, CDS_TEST) -> 0 (SUCCESSFUL)", u8); return 0; }
    if (!dm) { MW_TRACE("ChangeDisplaySettingsExW(%s, NULL dm) -> 0 (no-op)", u8); return 0; }
    MW_TRACE("ChangeDisplaySettingsExW(%s) -> -1 (DISP_CHANGE_FAILED: headless)", u8);
    return (int)0xFFFFFFFFu;
}
/* g_clip_owner/g_clip_seq live next to the existing g_clip_* globals
 * (loader.c:8982) so OpenClipboard/SetClipboardData/EmptyClipboard can
 * wire them — see sweep032_apply.py clipboard replacements. */
__attribute__((ms_abi)) void* mw_GetOpenClipboardWindow(void) {
    MW_TRACE("GetOpenClipboardWindow() -> %p", g_clip_owner);
    return g_clip_owner;
}
__attribute__((ms_abi)) uint32_t mw_GetClipboardSequenceNumber(void) {
    MW_TRACE("GetClipboardSequenceNumber() -> %u", g_clip_seq);
    return g_clip_seq;
}
/* WTSAPI32 (SW-1106): session-notification registration table. */
static void* g_wts_hwnd[8];
__attribute__((ms_abi)) int mw_WTSRegisterSessionNotification(void* hwnd, uint32_t flags) {
    (void)flags;
    for (int i = 0; i < 8; i++) if (g_wts_hwnd[i] == hwnd) return 1; /* idempotent */
    for (int i = 0; i < 8; i++) if (!g_wts_hwnd[i]) {
        g_wts_hwnd[i] = hwnd;
        MW_TRACE("WTSRegisterSessionNotification(%p) -> TRUE", hwnd);
        return 1;
    }
    mw_SetLastError(14); /* ERROR_OUTOFMEMORY */
    return 0;
}
__attribute__((ms_abi)) int mw_WTSUnRegisterSessionNotification(void* hwnd) {
    for (int i = 0; i < 8; i++) if (g_wts_hwnd[i] == hwnd) {
        g_wts_hwnd[i] = NULL;
        MW_TRACE("WTSUnRegisterSessionNotification(%p) -> TRUE", hwnd);
        return 1;
    }
    MW_TRACE("WTSUnRegisterSessionNotification(%p) -> FALSE (not registered)", hwnd);
    return 0;
}


/* ==== SECTION: kernel32_wagons ==== */
/* ---- SW-1106 zoomit64 KERNEL32 tail + WINMM timers + threadpool-wait
 * objects. Semaphores route to the existing semaphore table. Ordinal
 * compare (CompareStringOrdinal) is REAL per wine kernelbase semantics
 * (also SW-508 rust overlap). ---- */
__attribute__((ms_abi)) void* mw_CreateSemaphoreExW(void* attr, int32_t init, int32_t max, const uint16_t* name, uint32_t flags, uint32_t access) {
    (void)flags; (void)access;
    return mw_CreateSemaphoreW(attr, init, max, name);
}
__attribute__((ms_abi)) void* mw_OpenSemaphoreW(uint32_t access, int inherit, const uint16_t* name) {
    (void)access; (void)inherit;
    char n8[256];
    if (name) w3_w16_to_utf8z(name, n8, sizeof(n8)); else n8[0] = 0;
    MW_TRACE("OpenSemaphoreW(%s) -> NULL", n8);
    mw_SetLastError(2); /* ERROR_FILE_NOT_FOUND: named objects unsupported */
    return NULL;
}
/* LCID -> BCP-47 (real table for common locales; neutral LCIDs honored with
 * LOCALE_ALLOW_NEUTRAL_NAMES(0x8000000) per Win10+ contract). */
static const struct { uint32_t lcid; const char* name; } mw_lcids[] = {
    {0x0409,"en-US"},{0x0809,"en-GB"},{0x0407,"de-DE"},{0x040c,"fr-FR"},
    {0x0410,"it-IT"},{0x040a,"es-ES"},{0x0416,"pt-BR"},{0x0816,"pt-PT"},
    {0x0419,"ru-RU"},{0x0429,"fa-IR"},{0x0804,"zh-CN"},{0x0404,"zh-TW"},
    {0x0411,"ja-JP"},{0x0412,"ko-KR"},{0x041d,"sv-SE"},{0x0415,"pl-PL"},
    {0x0413,"nl-NL"},{0x0405,"cs-CZ"},{0x040d,"he-IL"},{0x040e,"hu-HU"},
    {0x0406,"da-DK"},{0x040b,"fi-FI"},{0x0408,"el-GR"},{0x0418,"ro-RO"},
    {0x041e,"th-TH"},{0x0422,"uk-UA"},{0x042a,"vi-VN"},{0x041f,"tr-TR"},
    {0x0401,"ar-SA"},{0x0439,"hi-IN"},
};
__attribute__((ms_abi)) int mw_LCIDToLocaleName(uint32_t lcid, uint16_t* name, int cch, uint32_t flags) {
    const char* out = NULL;
    char neutral[3];
    for (size_t i = 0; i < sizeof(mw_lcids)/sizeof(mw_lcids[0]); i++)
        if (mw_lcids[i].lcid == lcid) { out = mw_lcids[i].name; break; }
    if (!out && (lcid & 0xFF00u) == 0 && (lcid & 0xFFFFu)) { /* neutral lang id */
        static const struct { uint16_t p; const char* n; } neu[] = {
            {0x09,"en"},{0x07,"de"},{0x0c,"fr"},{0x10,"it"},{0x0a,"es"},
            {0x19,"ru"},{0x29,"fa"},{0x04,"zh"},{0x11,"ja"},{0x12,"ko"},
        };
        for (size_t i = 0; i < sizeof(neu)/sizeof(neu[0]); i++)
            if (neu[i].p == (lcid & 0xFFu)) {
                if (flags & 0x8000000u) { /* LOCALE_ALLOW_NEUTRAL_NAMES */
                    neutral[0] = neu[i].n[0]; neutral[1] = neu[i].n[1]; neutral[2] = 0;
                    out = neutral;
                }
                break;
            }
    }
    if (!out || !name) { mw_SetLastError(87); return 0; }
    int need = (int)strlen(out) + 1;
    if (cch <= 0) return need; /* size query */
    if (cch < need) { mw_SetLastError(122); return 0; } /* ERROR_INSUFFICIENT_BUFFER */
    for (int i = 0; i < need; i++) name[i] = (uint16_t)(unsigned char)out[i];
    MW_TRACE("LCIDToLocaleName(%#x) -> %s", lcid, out);
    return need;
}
static int mw_wupper_ord(uint16_t c) {
    /* ordinal fold: ASCII A-Z -> a-z; otherwise code point (documented). */
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}
__attribute__((ms_abi)) int mw_CompareStringOrdinal(const uint16_t* s1, int c1, const uint16_t* s2, int c2, uint32_t flags) {
    if (!s1 && c1 > 0) return 0;
    if (!s2 && c2 > 0) return 0;
    if (!s1 && !s2) return 1; /* both NULL: less-than (defensive; MS undefined) */
    size_t l1 = (c1 < 0) ? 0 : (size_t)c1, l2 = (c2 < 0) ? 0 : (size_t)c2;
    if (c1 < 0) { while (s1[l1]) l1++; }
    if (c2 < 0) { while (s2[l2]) l2++; }
    size_t n = l1 < l2 ? l1 : l2;
    for (size_t i = 0; i < n; i++) {
        int a = (flags & 1u) ? mw_wupper_ord(s1[i]) : s1[i]; /* NORM_IGNORECASE */
        int b = (flags & 1u) ? mw_wupper_ord(s2[i]) : s2[i];
        if (a < b) return 1; /* CSTR_LESS_THAN */
        if (a > b) return 3; /* CSTR_GREATER_THAN */
    }
    if (l1 < l2) return 1;
    if (l1 > l2) return 3;
    return 2; /* CSTR_EQUAL */
}
/* Threadpool-wait objects: real handle table; callback firing is event
 * delivery = SW-1203-class frontier (registered honestly here). */
#define MW_TPW_BASE 0x00B60000u
#define MW_TPW_MAX 32
typedef struct { uint8_t live; void* evt; void* pctx; } MwTpWait;
static MwTpWait g_tpw[MW_TPW_MAX];
static MwTpWait* mw_tpw_get(void* h) {
    uintptr_t v = (uintptr_t)h;
    if ((v & 0xFFFF0000u) != MW_TPW_BASE) return NULL;
    int i = (int)(v & 0xFFFFu) - 1;
    if (i < 0 || i >= MW_TPW_MAX || !g_tpw[i].live) return NULL;
    return &g_tpw[i];
}
__attribute__((ms_abi)) void* mw_CreateThreadpoolWait(void* cb, void* pv, void* env) {
    (void)cb; (void)env;
    for (int i = 0; i < MW_TPW_MAX; i++) if (!g_tpw[i].live) {
        g_tpw[i].live = 1; g_tpw[i].evt = NULL; g_tpw[i].pctx = pv;
        void* h = (void*)(uintptr_t)(MW_TPW_BASE | (unsigned)(i + 1));
        MW_TRACE("CreateThreadpoolWait -> %p", h);
        return h;
    }
    return NULL;
}
__attribute__((ms_abi)) void mw_SetThreadpoolWait(void* w, void* evt, const void* timeout) {
    MwTpWait* s = mw_tpw_get(w);
    if (!s) { MW_TRACE("SetThreadpoolWait(%p) -> invalid handle", w); return; }
    s->evt = evt;
    (void)timeout; /* NULL = infinite wait arm */
    MW_TRACE("SetThreadpoolWait(%p, evt=%p) armed", w, evt);
}
__attribute__((ms_abi)) void mw_CloseThreadpoolWait(void* w) {
    MwTpWait* s = mw_tpw_get(w);
    if (!s) { MW_TRACE("CloseThreadpoolWait(%p) -> invalid handle", w); return; }
    s->live = 0;
    MW_TRACE("CloseThreadpoolWait(%p) closed", w);
}
__attribute__((ms_abi)) int mw_SetThreadpoolWaitEx(void* w, void* evt, const void* timeout, void* reserved) {
    MwTpWait* s = mw_tpw_get(w);
    if (!s) { MW_TRACE("SetThreadpoolWaitEx(%p) -> FALSE (invalid)", w); return 0; }
    s->evt = evt; (void)timeout; (void)reserved;
    MW_TRACE("SetThreadpoolWaitEx(%p, evt=%p) -> TRUE", w, evt);
    return 1;
}
__attribute__((ms_abi)) int mw_SetThreadpoolTimerEx(void* t, const void* due, uint32_t period, uint32_t window) {
    (void)due; (void)period; (void)window;
    if (!t) { MW_TRACE("SetThreadpoolTimerEx(NULL) -> FALSE"); return 0; }
    MW_TRACE("SetThreadpoolTimerEx(%p) -> TRUE (arm acknowledged; firing=SW-1203)", t);
    return 1;
}
/* WINMM time events: real registration table (fire = event delivery SW-1203). */
#define MW_TMREVT_MAX 16
typedef struct { uint8_t live; uint32_t delay, user, flags; void* cb; } MwTmrEvt;
static MwTmrEvt g_tmrevt[MW_TMREVT_MAX];
__attribute__((ms_abi)) uint32_t mw_timeSetEvent(uint32_t delay, uint32_t res, void* cb, uint32_t user, uint32_t flags) {
    (void)res;
    if (!cb || !delay) return 0; /* real: 0 = failure */
    for (int i = 0; i < MW_TMREVT_MAX; i++) if (!g_tmrevt[i].live) {
        g_tmrevt[i].live = 1; g_tmrevt[i].delay = delay;
        g_tmrevt[i].cb = cb; g_tmrevt[i].user = user; g_tmrevt[i].flags = flags;
        MW_TRACE("timeSetEvent(delay=%u flags=%#x) -> id %d", delay, flags, i + 1);
        return (uint32_t)(i + 1);
    }
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_timeKillEvent(uint32_t id) {
    if (id >= 1 && id <= MW_TMREVT_MAX && g_tmrevt[id - 1].live) {
        g_tmrevt[id - 1].live = 0;
        MW_TRACE("timeKillEvent(%u) -> TIMERR_NOERROR", id);
        return 0;
    }
    return 10; /* MMSYSERR_INVALPARAM: no such event */
}

/* ==== SECTION: reg_wagons ==== */
/* ---- SW-1106 zoomit64: RegSet/RegDeleteKeyValueW routed through the
 * existing in-memory hive (RegCreateKeyExW + RegSetValueExW /
 * RegOpenKeyExW + RegDeleteValueW). ---- */
__attribute__((ms_abi)) int32_t mw_RegSetKeyValueW(void* key, const uint16_t* sub, const uint16_t* name, uint32_t type, const void* data, uint32_t size) {
    void* k = key;
    uint32_t r;
    if (sub && sub[0]) {
        r = (uint32_t)mw_RegCreateKeyExW(key, (const wchar_t*)sub, 0, NULL, 0, 0x000F0000u /*KEY_ALL_ACCESS*/, NULL, &k, NULL);
        if (r != 0) return (int32_t)r;
    }
    r = (uint32_t)mw_RegSetValueExW(k, (const wchar_t*)(name ? name : (const uint16_t*)L""), 0, type, data, size);
    if (k != key) mw_RegCloseKey(k);
    MW_TRACE("RegSetKeyValueW -> %u", r);
    return (int32_t)r;
}
__attribute__((ms_abi)) int32_t mw_RegDeleteKeyValueW(void* key, const uint16_t* sub, const uint16_t* name) {
    if (!name) { mw_SetLastError(87); return 87; }
    void* k = key;
    uint32_t r;
    if (sub && sub[0]) {
        r = (uint32_t)mw_RegOpenKeyExW(key, (const wchar_t*)sub, 0, 0x000F0000u, &k);
        if (r != 0) return (int32_t)r;
    }
    r = (uint32_t)mw_RegDeleteValueW(k, (const wchar_t*)name);
    if (k != key) mw_RegCloseKey(k);
    MW_TRACE("RegDeleteKeyValueW -> %u", r);
    return (int32_t)r;
}

/* ==== SECTION: com_wagons ==== */
/* ---- SW-1106 zoomit64 COM tail: generic COM shell (QI/AddRef/Release),
 * real file-backed IStream (SHCreateStreamOnFileEx), WinRT error-info
 * state family, apartment-type tracking, MF platform pair + real
 * MFCopyImage + honest device enumeration. HRESULTs are the real ones. ---- */
static const uint8_t MW_IID_IUNKNOWN[16] = {0,0,0,0,0,0,0,0,0xC0,0,0,0,0,0,0,0x46};
typedef struct { void** vtbl; uint32_t refs; } MwComShell;
/* ALL vtable targets are ms_abi: the GUEST dereferences *(void***)obj and
 * calls through the vtable with the Windows x64 ABI (RCX=this). */
__attribute__((ms_abi)) uint32_t mw_comshell_qi(void* self, const void* riid, void** out) {
    if (!out) return 0x80004003u; /* E_POINTER */
    *out = NULL;
    if (!riid) return 0x80004003u;
    if (memcmp(riid, MW_IID_IUNKNOWN, 16) == 0) {
        MwComShell* s = (MwComShell*)self;
        s->refs++;
        *out = self;
        return 0; /* S_OK */
    }
    return 0x80004002u; /* E_NOINTERFACE */
}
__attribute__((ms_abi)) uint32_t mw_comshell_addref(void* self) { return ++((MwComShell*)self)->refs; }
__attribute__((ms_abi)) uint32_t mw_comshell_release(void* self) {
    MwComShell* s = (MwComShell*)self;
    if (s->refs > 1) return --s->refs;
    free(s);
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_comshell_notimpl(void) { return 0x80004001u; } /* E_NOTIMPL */
/* Build a COM shell object with `nslots` vtable slots: [0]=QI [1]=AddRef
 * [2]=Release, rest = generic E_NOTIMPL. Returns NULL on OOM. */
static void* mw_comshell_new(int nslots) {
    if (nslots < 3) nslots = 3;
    MwComShell* s = (MwComShell*)malloc(sizeof(MwComShell));
    void** vt = (void**)malloc(sizeof(void*) * (size_t)nslots);
    if (!s || !vt) { free(s); free(vt); return NULL; }
    vt[0] = (void*)mw_comshell_qi;
    vt[1] = (void*)mw_comshell_addref;
    vt[2] = (void*)mw_comshell_release;
    for (int i = 3; i < nslots; i++) vt[i] = (void*)mw_comshell_notimpl;
    s->vtbl = vt; s->refs = 1;
    return s;
}
/* --- real file-backed IStream (SHCreateStreamOnFileEx) ---
 * IStream vtable layout (x64): QI(0) AddRef(1) Release(2) Read(3) Write(4)
 * Seek(5) SetSize(6) CopyTo(7) Commit(8) Revert(9) LockRegion(10)
 * UnlockRegion(11) Stat(12) Clone(13) — 14 slots.
 * The fd position IS the stream position: read()/write()/lseek() maintain
 * it natively (dedicated fd per stream). STREAM_SEEK_(SET=0,CUR=1,END=2)
 * are numerically identical to the Linux SEEK_* constants. */
#define MW_ISTREAM_VSLOTS 14
typedef struct { void** vtbl; uint32_t refs; int fd; } MwFileStream;
__attribute__((ms_abi)) uint32_t mw_fs_read(void* self, void* pv, uint32_t cb, uint32_t* pcbRead) {
    MwFileStream* s = (MwFileStream*)self;
    if (!pv && cb) return 0x80030006u; /* STG_E_READFAULT */
    ssize_t n = read(s->fd, pv, cb);
    if (n < 0) return 0x80030006u;
    if (pcbRead) *pcbRead = (uint32_t)n;
    return ((uint32_t)n == cb) ? 0u : 1u; /* S_OK / S_FALSE (EOF before cb) */
}
__attribute__((ms_abi)) uint32_t mw_fs_write(void* self, const void* pv, uint32_t cb, uint32_t* pcbWritten) {
    MwFileStream* s = (MwFileStream*)self;
    if (!pv && cb) return 0x80030013u; /* STG_E_WRITEFAULT */
    ssize_t n = write(s->fd, pv, cb);
    if (n < 0) return 0x80030013u;
    if (pcbWritten) *pcbWritten = (uint32_t)n;
    return ((uint32_t)n == cb) ? 0u : 1u;
}
__attribute__((ms_abi)) uint32_t mw_fs_seek(void* self, int64_t move, uint32_t origin, uint64_t* pos) {
    MwFileStream* s = (MwFileStream*)self;
    if (origin > 2) return 0x80030019u; /* STG_E_INVALIDFUNCTION */
    off_t n = lseek(s->fd, (off_t)move, (int)origin);
    if (n < 0) return 0x80030019u;
    if (pos) *pos = (uint64_t)n;
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_fs_setsize(void* self, uint64_t size) {
    MwFileStream* s = (MwFileStream*)self;
    return ftruncate(s->fd, (off_t)size) == 0 ? 0u : 0x80030019u;
}
__attribute__((ms_abi)) uint32_t mw_fs_stat(void* self, void* pstatstg, uint32_t flag) {
    MwFileStream* s = (MwFileStream*)self;
    if (!pstatstg) return 0x80004003u; /* E_POINTER */
    uint8_t* st = (uint8_t*)pstatstg;
    memset(st, 0, 0x50); /* sizeof(STATSTG) x64: pwcsName(8) type(4+pad) cbSize(8)
                          * mtime(8) ctime(8) atime(8) grfMode(4) locks(4)
                          * clsid(16) stateBits(4) reserved(4) */
    struct stat sb;
    if (fstat(s->fd, &sb) == 0) *(uint64_t*)(st + 8 + 4 + 4) = (uint64_t)sb.st_size;
    *(uint32_t*)(st + 8) = 2; /* STGTY_STREAM */
    (void)flag; /* STATFLAG_NONAME honored: pwcsName stays NULL (documented) */
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_SHCreateStreamOnFileEx(const uint16_t* path, uint32_t mode, uint32_t attrs, int create, void* tmpl, void** stream) {
    (void)attrs; (void)tmpl;
    if (!stream) return 0x80004003u; /* E_POINTER */
    *stream = NULL;
    if (!path) return 0x80070057u; /* E_INVALIDARG */
    char u8[1024];
    w3_w16_to_utf8z(path, u8, sizeof(u8));
    int fl = 0;
    switch (mode & 0xFu) { /* STGM_ACCESS mask */
    case 0: fl = O_RDONLY; break;                    /* STGM_READ */
    case 1: fl = O_WRONLY | O_CREAT; break;          /* STGM_WRITE */
    case 2: fl = O_RDWR | O_CREAT; break;            /* STGM_READWRITE */
    default: return 0x80030001u;                     /* STG_E_INVALIDFLAG */
    }
    if (mode & 0x1000u || create) fl |= O_TRUNC;     /* STGM_CREATE */
    int fd = open(u8, fl, 0644);
    if (fd < 0) {
        int e = errno;
        MW_TRACE("SHCreateStreamOnFileEx(%s, mode=%#x) open failed errno=%d", u8, mode, e);
        return (e == ENOENT) ? 0x80030002u /* STG_E_FILENOTFOUND */
             : (e == EACCES) ? 0x80030005u /* STG_E_ACCESSDENIED */
             : 0x80030001u;
    }
    void* vt = malloc(sizeof(void*) * MW_ISTREAM_VSLOTS);
    MwFileStream* s = (MwFileStream*)malloc(sizeof(MwFileStream));
    if (!vt || !s) { free(vt); free(s); close(fd); return 0x8007000Eu; } /* E_OUTOFMEMORY */
    void** v = (void**)vt;
    v[0] = (void*)mw_comshell_qi; v[1] = (void*)mw_comshell_addref; v[2] = (void*)mw_comshell_release;
    v[3] = (void*)mw_fs_read; v[4] = (void*)mw_fs_write;
    v[5] = (void*)mw_fs_seek; v[6] = (void*)mw_fs_setsize;
    v[7] = (void*)mw_comshell_notimpl; /* CopyTo */
    v[8] = (void*)mw_comshell_notimpl; /* Commit */ v[9] = (void*)mw_comshell_notimpl; /* Revert */
    v[10] = (void*)mw_comshell_notimpl; v[11] = (void*)mw_comshell_notimpl; /* Lock/Unlock */
    v[12] = (void*)mw_fs_stat; v[13] = (void*)mw_comshell_notimpl; /* Stat, Clone */
    s->vtbl = v; s->refs = 1; s->fd = fd;
    *stream = s;
    MW_TRACE("SHCreateStreamOnFileEx(%s, mode=%#x) -> IStream %p (real fd %d)", u8, mode, (void*)s, fd);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_CoGetApartmentType(uint32_t* apt, uint32_t* qualifier) {
    if (!apt || !qualifier) return 0x80004003u; /* E_POINTER */
    *apt = 0; *qualifier = 0;
    if (!g_com_init) return 0x800401F0u; /* CO_E_NOTINITIALIZED */
    *apt = 1; /* APTTYPE_MTA: single native-thread engine executes all guest code */
    *qualifier = 0; /* APTTYPEQUALIFIER_NONE */
    MW_TRACE("CoGetApartmentType -> S_OK (MTA)");
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_CoReleaseMarshalData(void* stm) {
    MW_TRACE("CoReleaseMarshalData(%p) -> S_OK (no marshal bytes)", stm);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_RoGetAgileReference(uint32_t options, const void* riid, void* punk, void** ref) {
    (void)options; (void)riid;
    if (!ref) return 0x80004003u;
    *ref = NULL;
    if (!punk) return 0x80070057u; /* E_INVALIDARG */
    MW_TRACE("RoGetAgileReference -> E_NOINTERFACE (no agile-ref service)");
    return 0x80004002u;
}
__attribute__((ms_abi)) uint32_t mw_CreateStreamOverRandomAccessStream(void* punk, const void* riid, void** out) {
    (void)punk; (void)riid;
    if (!out) return 0x80004003u;
    *out = NULL;
    MW_TRACE("CreateStreamOverRandomAccessStream -> E_NOINTERFACE (no WinRT streams)");
    return 0x80004002u;
}
__attribute__((ms_abi)) uint32_t mw_GetRestrictedErrorInfo(void** info) {
    if (!info) return 0x80004003u;
    *info = NULL;
    MW_TRACE("GetRestrictedErrorInfo -> S_OK (no info set)");
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_SetRestrictedErrorInfo(void* info) {
    if (!info) return 0x80004003u; /* E_POINTER */
    MW_TRACE("SetRestrictedErrorInfo(%p) -> S_OK", info);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_RoTransformError(uint32_t oldhr, uint32_t newhr, const uint16_t* msg) {
    MW_TRACE("RoTransformError(%08x -> %08x) -> S_OK", oldhr, newhr);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_RoOriginateLanguageException(uint32_t hr, const uint16_t* msg, void* ex) {
    (void)msg; (void)ex;
    MW_TRACE("RoOriginateLanguageException(%08x) -> S_OK", hr);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_RoFailFastWithErrorContext(uint32_t hr, const void* info, uint32_t cb, uint32_t* pcb) {
    (void)info; (void)cb; (void)pcb;
    fprintf(stderr, "[FASTFAIL] RoFailFastWithErrorContext hr=0x%08x — fail-fast (real WinRT contract)\n", hr);
    fflush(stderr);
    exit(9);
}
__attribute__((ms_abi)) uint32_t mw_RoGetActivationFactory(const uint16_t* classId, const void* riid, void** factory) {
    (void)riid;
    if (!factory) return 0x80004003u;
    *factory = NULL;
    char u8[256] = {0};
    if (classId) w3_w16_to_utf8z(classId, u8, sizeof(u8));
    MW_TRACE("RoGetActivationFactory(%s) -> RO_E_METADATA_NAME_NOT_FOUND", u8);
    return 0x8000000Fu;
}
/* Media Foundation (SW-1106): platform pair + real MFCopyImage + honest
 * object shells (QI/AddRef/Release real, attributes E_NOTIMPL v1). */
#define MF_VERSION_OK 0x460002u /* MF_VERSION = (MF_API_VERSION<<16)|MF_SDK_VERSION */
static int g_mf_started = 0;
__attribute__((ms_abi)) uint32_t mw_MFStartup(uint32_t version, uint32_t flags) {
    (void)flags;
    if (version != MF_VERSION_OK) {
        MW_TRACE("MFStartup(version=%#x) -> MF_E_BADSTARTUPVERSION", version);
        return 0xC00D36C2u;
    }
    g_mf_started++;
    MW_TRACE("MFStartup -> S_OK (started=%d)", g_mf_started);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_MFShutdown(void) {
    if (g_mf_started > 0) g_mf_started--;
    MW_TRACE("MFShutdown -> S_OK (started=%d)", g_mf_started);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_MFCopyImage(uint8_t* dst, int dstpitch, const uint8_t* src, int srcpitch, int width, int lines) {
    if (!dst || !src || width < 0 || lines < 0) return 0x80070057u; /* E_INVALIDARG */
    for (int i = 0; i < lines; i++)
        memcpy(dst + (size_t)i * dstpitch, src + (size_t)i * srcpitch, (size_t)width);
    MW_TRACE("MFCopyImage(w=%d lines=%d) real copy", width, lines);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_MFCreateMediaType(void** mt) {
    if (!mt) return 0x80004003u;
    *mt = mw_comshell_new(16); /* IMFMediaType extends IMFAttributes */
    if (!*mt) return 0x8007000Eu;
    MW_TRACE("MFCreateMediaType -> S_OK (%p)", *mt);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_MFCreateAttributes(void** attr, uint32_t initial) {
    (void)initial;
    if (!attr) return 0x80004003u;
    *attr = mw_comshell_new(16);
    if (!*attr) return 0x8007000Eu;
    MW_TRACE("MFCreateAttributes -> S_OK (%p)", *attr);
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_MFEnumDeviceSources(void* attr, void*** sources, uint32_t* count) {
    (void)attr;
    if (sources) *sources = NULL;
    if (count) *count = 0; /* headless: zero media devices (GdipGetImageEncoders precedent) */
    MW_TRACE("MFEnumDeviceSources -> S_OK (0 devices)");
    return 0u;
}
__attribute__((ms_abi)) uint32_t mw_MFCreateSourceReaderFromMediaSource(void* src, void* params, void** reader) {
    (void)params;
    if (!reader) return 0x80004003u;
    *reader = NULL;
    if (!src) return 0x80070057u; /* E_INVALIDARG: no media-source objects exist */
    MW_TRACE("MFCreateSourceReaderFromMediaSource -> E_INVALIDARG (no real media sources)");
    return 0x80070057u;
}
/* SHELL32 item objects (SW-1106): real path-existence check; items are
 * COM shells (QI real, IShellItem methods E_NOTIMPL v1). */
__attribute__((ms_abi)) uint32_t mw_SHCreateItemFromParsingName(const uint16_t* path, void* pbc, const void* riid, void** out) {
    (void)pbc; (void)riid;
    if (!out) return 0x80004003u;
    *out = NULL;
    if (!path) return 0x80070057u;
    char u8[1024];
    w3_w16_to_utf8z(path, u8, sizeof(u8));
    struct stat sb;
    if (stat(u8, &sb) != 0) {
        MW_TRACE("SHCreateItemFromParsingName(%s) -> 0x80070002 (not found)", u8);
        return 0x80070002u; /* HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) */
    }
    void* it = mw_comshell_new(8);
    if (!it) return 0x8007000Eu;
    *out = it;
    MW_TRACE("SHCreateItemFromParsingName(%s) -> S_OK (item %p)", u8, it);
    return 0u;
}
static const struct { uint8_t g[16]; const char* name; } mw_known_folders[] = {
    {{0x3A,0xCC,0xBF,0xB4,0x2C,0xDB,0x4C,0x42,0xB0,0x29,0x7F,0xE9,0x9A,0x87,0xC6,0x41},"Desktop"},
    {{0xD0,0x9A,0xD3,0xFD,0x8F,0x23,0xAF,0x46,0xAD,0xB4,0x6C,0x85,0x48,0x03,0x69,0xC7},"Documents"},
    {{0x90,0xE2,0x4D,0x37,0x3F,0x12,0x65,0x45,0x91,0x64,0x39,0xC4,0x92,0x5E,0x46,0x7B},"Downloads"},
    {{0x30,0x81,0xE2,0x33,0x1E,0x4E,0x76,0x46,0x83,0x5A,0x98,0x39,0x5C,0x3B,0xC3,0xBB},"Pictures"},
    {{0x71,0xD5,0xD8,0x4B,0x19,0x6D,0xD3,0x48,0xBE,0x97,0x42,0x22,0x20,0x08,0x0E,0x43},"Music"},
    {{0x1D,0x9B,0x98,0x18,0xB5,0x99,0x5B,0x45,0x84,0x1C,0xAB,0x7C,0x74,0xE4,0xDD,0xFC},"Videos"},
    {{0x85,0x27,0xB3,0xF1,0xBA,0x6F,0xCF,0x4F,0x9D,0x55,0x7B,0x8E,0x7F,0x15,0x70,0x91},"LocalAppData"},
    {{0xDB,0x85,0xB6,0x3E,0xF9,0x65,0xF6,0x4C,0xA0,0x3A,0xE3,0xEF,0x65,0x72,0x9F,0x3D},"RoamingAppData"},
};
__attribute__((ms_abi)) uint32_t mw_SHGetKnownFolderItem(const uint8_t* rfid, uint32_t flags, void* token, const void* riid, void** out) {
    (void)flags; (void)token; (void)riid;
    if (!out) return 0x80004003u;
    *out = NULL;
    if (!rfid) return 0x80070057u;
    for (size_t i = 0; i < sizeof(mw_known_folders)/sizeof(mw_known_folders[0]); i++) {
        if (memcmp(mw_known_folders[i].g, rfid, 16) == 0) {
            void* it = mw_comshell_new(8);
            if (!it) return 0x8007000Eu;
            *out = it;
            MW_TRACE("SHGetKnownFolderItem(%s) -> S_OK (item %p)", mw_known_folders[i].name, it);
            return 0u;
        }
    }
    MW_TRACE("SHGetKnownFolderItem(unknown GUID) -> E_INVALIDARG");
    return 0x80070057u;
}

/* ==== SECTION: subclass_wagons ==== */
/* ---- SW-1106 zoomit64 COMCTL32 ordinals 410/412/413 (wine spec cross-check
 * 2026-10-21): real per-hwnd subclass chain + traversal-tracking
 * DefSubclassProc, and SHLWAPI.219 QISearch (real QI delegation). ---- */
#define MW_SUB_MAX 64
typedef struct { void* hwnd; void* proc; uintptr_t id; uintptr_t ref; } MwSub;
static MwSub g_sub[MW_SUB_MAX];
static int g_sub_n = 0;
static struct { void* hwnd; uint32_t msg; int idx; int active; } g_sub_cur;
__attribute__((ms_abi)) int mw_SetWindowSubclass(void* hwnd, void* proc, uintptr_t id, uintptr_t ref) {
    if (!hwnd || !proc) return 0;
    for (int i = 0; i < g_sub_n; i++)
        if (g_sub[i].hwnd == hwnd && g_sub[i].proc == proc && g_sub[i].id == id) {
            g_sub[i].ref = ref; /* real: re-subclass updates the ref data */
            MW_TRACE("SetWindowSubclass(hwnd=%p id=%llx) updated", hwnd, (unsigned long long)id);
            return 1;
        }
    if (g_sub_n >= MW_SUB_MAX) { mw_SetLastError(87); return 0; }
    g_sub[g_sub_n].hwnd = hwnd; g_sub[g_sub_n].proc = proc;
    g_sub[g_sub_n].id = id; g_sub[g_sub_n].ref = ref;
    g_sub_n++;
    MW_TRACE("SetWindowSubclass(hwnd=%p id=%llx) -> TRUE (chain %d)", hwnd, (unsigned long long)id, g_sub_n);
    return 1;
}
__attribute__((ms_abi)) int mw_RemoveWindowSubclass(void* hwnd, void* proc, uintptr_t id) {
    for (int i = 0; i < g_sub_n; i++)
        if (g_sub[i].hwnd == hwnd && g_sub[i].proc == proc && g_sub[i].id == id) {
            for (int j = i; j < g_sub_n - 1; j++) g_sub[j] = g_sub[j + 1];
            g_sub_n--;
            MW_TRACE("RemoveWindowSubclass(hwnd=%p id=%llx) -> TRUE", hwnd, (unsigned long long)id);
            return 1;
        }
    MW_TRACE("RemoveWindowSubclass(hwnd=%p id=%llx) -> FALSE (not installed)", hwnd, (unsigned long long)id);
    return 0;
}
__attribute__((ms_abi)) long long mw_DefSubclassProc(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp) {
    /* Traversal: track (hwnd,msg) recursion so each proc that calls us gets
     * the next proc in the chain; tail falls back to DefWindowProcW. */
    int start = 0;
    if (g_sub_cur.active && g_sub_cur.hwnd == hwnd && g_sub_cur.msg == msg)
        start = g_sub_cur.idx;
    for (int i = start; i < g_sub_n; i++) {
        if (g_sub[i].hwnd != hwnd) continue;
        g_sub_cur.hwnd = hwnd; g_sub_cur.msg = msg; g_sub_cur.idx = i + 1; g_sub_cur.active = 1;
        long long r;
        __attribute__((ms_abi)) long long (*proc)(void*, uint32_t, uintptr_t, intptr_t) =
            (__attribute__((ms_abi)) long long (*)(void*, uint32_t, uintptr_t, intptr_t))g_sub[i].proc;
        r = proc(hwnd, msg, wp, (long long)lp);
        g_sub_cur.active = 0;
        MW_TRACE("DefSubclassProc(hwnd=%p msg=%#x) -> proc[%d] = %llx", hwnd, msg, i, (unsigned long long)r);
        return r;
    }
    return mw_DefWindowProcW(hwnd, msg, wp, lp);
}
__attribute__((ms_abi)) uint32_t mw_QISearch(void* punk, const void* iidtbl, const void* riid, void** out) {
    (void)iidtbl; /* QISearch walks an IID table; delegation to the object's
                     own QI is the superset behavior guests rely on */
    if (!punk || !out) return 0x80004003u;
    *out = NULL;
    void** vt = *(void***)punk;
    if (!vt || !vt[0]) return 0x80004002u;
    __attribute__((ms_abi)) uint32_t (*qi)(void*, const void*, void**) =
        (__attribute__((ms_abi)) uint32_t (*)(void*, const void*, void**))vt[0];
    return qi(punk, riid, out);
}

/* ==== SWEEP-032 IMPL END ==== */
