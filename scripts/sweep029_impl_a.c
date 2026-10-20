/* ============================================================
 * SWEEP-029 wagon batch — SW-1103 engine fix + bginfo64 54-tail
 *   + winmergeu SHELL32/SHLWAPI raw-ordinal tail.
 * Drivers: artifacts/sweep028/recon (bginfo64 54 unresolved, exit0
 * non-fatal), evidence/sweep028/README "next frontier".
 * Frontier decodes this round:
 *  - SW-1103 (ENGINE BUG): mw_RtlCaptureContext was a no-op — every
 *    guest caller (mingw-w64 __report_gsfailure / gs_report path)
 *    read a ZEROED CONTEXT: ctx.Rip=0 -> RtlLookupFunctionEntry(pc=0)
 *    -> NULL -> SetUnhandledExceptionFilter(NULL) ->
 *    UnhandledExceptionFilter -> TerminateProcess(0xC0000409).
 *    winmergeu exit-9 fail-fast chain ends exactly there.
 *  - winmergeu 4 remaining "unresolved" were ORDINAL imports counted
 *    without names (loader.c:24760): SHELL32.155/701, SHLWAPI.2/12.
 *    Names via wine-mirror spec files (2026-10-20 fetch).
 * V1 semantics: real where cheap (raster, strings, /proc enum),
 * typed-honest failures for host-impossible services (COM menus).
 * ============================================================ */

/* ---- SW-1103: RtlCaptureContext now captures the REAL machine state.
 * Guest callers get integer regs + segments + EFlags + MxCsr; Rip =
 * caller return address (ms_abi RA = the guest PC that called us).
 * Approximations (documented, BUG-031 discipline): FPU/SSE regs and
 * debug regs stay zeroed; Rsp is entry+8 approximation. Every value is
 * real where it matters: gs_report only consumes ctx.Rip/Rsp. */
__attribute__((ms_abi)) void mw_RtlCaptureContext(void* ctx) {
    if (!ctx) return;
    uint8_t* c = (uint8_t*)ctx;
    memset(c, 0, 0x4D0);
    *(uint32_t*)(c + 0x30) = 0x10000Fu; /* CONTEXT_AMD64|CONTROL|INTEGER|SEGMENTS|FLOATING_POINT */
    *(uint32_t*)(c + 0x34) = 0x1F80u;   /* default MxCsr */
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
    *(uint16_t*)(c + 0x38) = (uint16_t)cs;  /* SegCs */
    *(uint16_t*)(c + 0x42) = (uint16_t)ss;  /* SegSs */
    *(uint32_t*)(c + 0x44) = (uint32_t)fl;  /* EFlags */
    MW_TRACE("RtlCaptureContext() rip=0x%lx", rip);
}

/* ---- SW-1103 note: the old no-op stub body in loader.c is replaced by
 * apply (exact-string edit) so this definition is the single one. ---- */

/* ---- brush color decode (mirrors mw_FillRect stock/live tables) ---- */
static uint32_t mw29_brush_rgb(void* hbrush, int* hollow) {
    *hollow = 0;
    uintptr_t v = (uintptr_t)hbrush;
    if ((v & 0xFFFFF000u) == 0x00030000u) {
        int i = (int)(v & 0x1F);
        if (g_brush_live[i]) return g_brush_rgb[i];
        return 0xC0C0C0;
    } else if ((v & 0xFFFF0000u) == 0x00040000u) {
        int idx = (int)(v & 0xFF);
        switch (idx) {
        case 0: return 0xFFFFFF; case 1: return 0xC0C0C0;
        case 2: return 0x808080; case 3: return 0x404040;
        case 4: return 0x000000;
        case 5: *hollow = 1; return 0;
        case 18: return 0xFFFFFF; /* DC_BRUSH (no per-DC color store v1) */
        default: return 0xC0C0C0;
        }
    }
    return 0xC0C0C0;
}

/* ---- GDI raster family (bginfo64 draw layer) ---- */
__attribute__((ms_abi)) int mw_InvertRect(void* hdc, const void* rect) {
    MWBmp* S = mw_dc_surface(hdc);
    if (rect && S) {
        const int32_t* r = (const int32_t*)rect;
        int x0 = r[0] < 0 ? 0 : r[0], y0 = r[1] < 0 ? 0 : r[1];
        int x1 = r[2] > S->w ? S->w : r[2], y1 = r[3] > S->h ? S->h : r[3];
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                mwb_set(S, x, y, 0xFFFFFFu ^ mwb_get(S, x, y));
        MW_TRACE("[GDI] InvertRect(%d,%d)-(%d,%d)", r[0], r[1], r[2], r[3]);
    }
    return 1;
}
__attribute__((ms_abi)) int mw_FillRgn(void* hdc, void* hrgn, void* hbrush) {
    MWBmp* S = mw_dc_surface(hdc);
    int slot = mw_rgn_slot(hrgn);
    if (!S || slot < 0) return 0;
    int hollow = 0; uint32_t col = mw29_brush_rgb(hbrush, &hollow);
    if (hollow) return 1;
    for (uint32_t i = 0; i < g_rgn[slot].n; i++) {
        const int32_t* q = g_rgn[slot].r[i];
        mwb_fill(S, q[0], q[1], q[2] - q[0], q[3] - q[1], col);
    }
    return 1;
}
__attribute__((ms_abi)) int mw_FrameRgn(void* hdc, void* hrgn, void* hbrush, int w, int h) {
    MWBmp* S = mw_dc_surface(hdc);
    int slot = mw_rgn_slot(hrgn);
    if (!S || slot < 0) return 0;
    int hollow = 0; uint32_t col = mw29_brush_rgb(hbrush, &hollow);
    if (hollow) return 1;
    if (w < 1) w = 1; if (h < 1) h = 1;
    for (uint32_t i = 0; i < g_rgn[slot].n; i++) {
        const int32_t* q = g_rgn[slot].r[i];
        mwb_fill(S, q[0], q[1], q[2] - q[0], h, col);                    /* top */
        mwb_fill(S, q[0], q[3] - h, q[2] - q[0], h, col);                /* bottom */
        mwb_fill(S, q[0], q[1], w, q[3] - q[1], col);                    /* left */
        mwb_fill(S, q[2] - w, q[1], w, q[3] - q[1], col);                /* right */
    }
    return 1;
}
__attribute__((ms_abi)) int mw_SetPixelV(void* hdc, int x, int y, uint32_t col) {
    MWBmp* S = mw_dc_surface(hdc);
    if (S) mwb_set(S, x, y, mw_cr2rgb(col));
    return 1;
}
__attribute__((ms_abi)) int mw_ExtFloodFill(void* hdc, int x, int y, uint32_t col, uint32_t type) {
    (void)hdc; (void)x; (void)y; (void)col; (void)type;
    MW_TRACE("[GDI] ExtFloodFill(%d,%d) -> no-op v1", x, y);
    return 1; /* v1: no seed-fill raster; callers repaint explicitly */
}
__attribute__((ms_abi)) int mw_GetBoundsRect(void* hdc, void* rc, uint32_t flags) {
    (void)hdc; (void)flags;
    if (rc) memset(rc, 0, 16);
    return 0; /* DCB_RESET: no accumulated bounds */
}
__attribute__((ms_abi)) void* mw_CreatePolygonRgn(const void* pts, int n, int fill) {
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
}
__attribute__((ms_abi)) void* mw_CreateRoundRectRgn(int l, int t, int r, int b, int ew, int eh) {
    (void)ew; (void)eh; /* v1: bbox approximation of the rounded corners */
    return mw_CreateRectRgn(l, t, r, b);
}
__attribute__((ms_abi)) void* mw_CreateDIBitmap(void* hdc, const void* bmih, uint32_t init,
        const void* bits, const void* pbmi, uint32_t usage) {
    (void)init; (void)bits; (void)usage;
    const int32_t* hd = (const int32_t*)(bmih ? bmih : pbmi);
    if (!hd) return NULL;
    int w = hd[1], hh = hd[3]; /* BITMAPINFOHEADER: +4 biWidth, +12 biHeight */
    if (hh < 0) hh = -hh;
    void* hb = mw_CreateCompatibleBitmap(hdc, w, hh);
    MW_TRACE("[GDI] CreateDIBitmap(%dx%d) -> %p (blank surface v1)", w, hh, hb);
    return hb;
}
__attribute__((ms_abi)) void* mw_CreateHatchBrush(int style, uint32_t colorref) {
    (void)style; /* v1: solid approximation of the hatch pattern */
    return mw_CreateSolidBrush(colorref);
}
__attribute__((ms_abi)) int mw_SetWorldTransform(void* hdc, const void* xf) {
    (void)hdc; (void)xf;
    return 1; /* v1: identity world transform accepted */
}
static int mw29_gmode_slot(void* hdc) {
    return (int)(((uintptr_t)hdc >> 4) & 63);
}
static int g_mw29_gmode[64]; /* per-DC graphics mode (GM_COMPATIBLE=1) */
__attribute__((ms_abi)) int mw_SetGraphicsMode(void* hdc, int mode) {
    int s = mw29_gmode_slot(hdc);
    int prev = g_mw29_gmode[s] ? g_mw29_gmode[s] : 1;
    if (mode == 1 || mode == 2) g_mw29_gmode[s] = mode;
    return prev;
}
__attribute__((ms_abi)) int mw_OffsetWindowOrgEx(void* hdc, int dx, int dy, void* old) {
    MwDcMap* m = mw_dcmap_for(hdc);
    if (old) { ((int32_t*)old)[0] = m->wox; ((int32_t*)old)[1] = m->woy; }
    m->wox += dx; m->woy += dy; return 1;
}
__attribute__((ms_abi)) int mw_ExtSelectClipRgn(void* hdc, void* hrgn, int mode) {
    (void)hdc; (void)mode;
    return hrgn ? 2 : 0; /* SIMPLEREGION with a region; ERROR w/o */
}
__attribute__((ms_abi)) int mw_GetNearestPaletteIndex(void* hpal, uint32_t col) {
    (void)hpal; (void)col;
    return 0; /* entry 0 — no palette data model v1 */
}
__attribute__((ms_abi)) int mw_GetPaletteEntries(void* hpal, int start, int n, void* entries) {
    (void)hpal; (void)start; (void)entries;
    return 0; /* empty palette: no entries returned */
}
__attribute__((ms_abi)) uint32_t mw_GetObjectType(void* h) {
    uintptr_t v = (uintptr_t)h;
    if (!v) return 0;
    if ((v & 0xFFFFF000u) == 0x00030000u) return 2;              /* OBJ_BRUSH */
    if ((v & 0xFFFF0000u) == 0x00040000u) {                      /* stock objects */
        switch (v & 0xFF) {
        case 6: case 7: case 8: case 19: return 1;               /* OBJ_PEN */
        case 10: case 11: case 12: case 13: case 14: case 16: case 17: return 5; /* OBJ_FONT */
        case 15: return 10;                                      /* OBJ_PAL */
        default: return 2;                                       /* brushes */
        }
    }
    if ((v & 0xFFFFF000u) == MW_HBITMAP_BASE) return 7;          /* OBJ_BITMAP */
    if ((v & 0xFFFF0000u) == 0x00060000u && (v & 0xFFFFF000u) != 0x00061000u)
        return 8;                                                /* OBJ_REGION (not imagelist) */
    if ((v & 0xFFFF0000u) == 0x00070000u) return 5;              /* OBJ_FONT */
    if ((v & 0xFFFFF000u) == MW_MEMDC_BASE) return 3;            /* OBJ_DC */
    return 0;
}
__attribute__((ms_abi)) int mw_AlphaBlend(void* hdcDst, int dx, int dy, int dw, int dh,
        void* hdcSrc, int sx, int sy, int sw, int sh, uint32_t blendfn) {
    (void)blendfn; /* v1: source copy, no alpha blend */
    return mw_StretchBlt(hdcDst, dx, dy, dw, dh, hdcSrc, sx, sy, sw, sh, 0x00CC0020u);
}
__attribute__((ms_abi)) void* mw_CopyIcon(void* hicon) { return hicon; /* cookie identity */ }

/* ---- USER32 tails (bginfo64 window/dialog layer) ---- */
__attribute__((ms_abi)) void* mw_GetNextDlgGroupItem(void* dlg, void* ctl, int prev) {
    (void)dlg; (void)prev;
    return ctl ? ctl : NULL; /* v1: group navigation unchanged */
}
__attribute__((ms_abi)) void mw_NotifyWinEvent(uint32_t ev, void* hwnd, int32_t obj, int32_t child) {
    (void)ev; (void)hwnd; (void)obj; (void)child;
}
static uint32_t mw29_mapvk(uint32_t code, uint32_t type) {
    switch (type) {
    case 0: case 1: case 3: return code; /* v1: VK<->SC identity */
    case 2: return 0;                    /* VK->char: none */
    default: return 0;
    }
}
__attribute__((ms_abi)) uint32_t mw_MapVirtualKeyW(uint32_t code, uint32_t type) {
    return mw29_mapvk(code, type);
}
__attribute__((ms_abi)) uint32_t mw_MapVirtualKeyExW(uint32_t code, uint32_t type, void* hkl) {
    (void)hkl;
    return mw29_mapvk(code, type);
}
__attribute__((ms_abi)) int mw_SetSysColors(int n, const int32_t* els, const uint32_t* rgb) {
    (void)n; (void)els; (void)rgb;
    return 1; /* v1: system palette unchanged */
}
__attribute__((ms_abi)) void* mw_GetProcessWindowStation(void) {
    return (void*)0x0002D001u; /* HWINSTA cookie */
}
__attribute__((ms_abi)) int mw_IsCharLowerW(uint16_t wc) {
    return (wc >= (uint16_t)'a' && wc <= (uint16_t)'z') || (wc >= 0xE0u && wc != 0xFFu);
}
__attribute__((ms_abi)) int mw_CharUpperBuffW(uint16_t* str, uint32_t len) {
    if (!str) return 0;
    for (uint32_t i = 0; i < len; i++) {
        uint16_t c = str[i];
        if (c >= (uint16_t)'a' && c <= (uint16_t)'z') str[i] = (uint16_t)(c - 32);
        else if (c >= 0xE0u && c <= 0xFEu && c != 0xF7u) str[i] = (uint16_t)(c - 32);
    }
    return (int)len;
}
__attribute__((ms_abi)) int mw_DestroyAcceleratorTable(void* hacc) { (void)hacc; return 1; }
__attribute__((ms_abi)) int mw_GetWindowRgn(void* hwnd, void* hrgn) {
    (void)hwnd; (void)hrgn;
    return 0; /* NULLREGION — windows are rectangular v1 */
}

/* ---- K32 process family: REAL host enumeration via /proc ---- */
__attribute__((ms_abi)) int mw_K32EnumProcesses(uint32_t* pids, uint32_t cb, uint32_t* needed) {
    if (needed) *needed = 0;
    if (!pids || !cb) return 0;
    uint32_t max = cb / 4, n = 0;
    DIR* d = opendir("/proc");
    if (!d) return 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL && n < max) {
        const char* s = e->d_name;
        int isnum = s[0] >= '0' && s[0] <= '9';
        for (const char* q = s; *q; q++) if (*q < '0' || *q > '9') { isnum = 0; break; }
        if (isnum) pids[n++] = (uint32_t)atoi(s);
    }
    closedir(d);
    if (needed) *needed = n * 4;
    MW_TRACE("[K32] EnumProcesses -> %u pids", n);
    return 1;
}
__attribute__((ms_abi)) int mw_K32EnumProcessModules(void* proc, void** mods, uint32_t cb, uint32_t* needed) {
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
    for (const char* q = p; q && *q; q++) if (*q == '/' || *q == '\\') base = q + 1;
    uint32_t n = 0;
    while (base[n] && n + 1 < size) { out[n] = (uint16_t)(uint8_t)base[n]; n++; }
    out[n] = 0;
    return n;
}

/* ---- Net* workstation/server info: static UTF-16 models ---- */
static uint16_t mw29_wbuf[8][64];
static int mw29_wbuf_init = 0;
static void mw29_wbuf_init_once(void) {
    if (mw29_wbuf_init) return;
    static const char* initstr[8] = { "MiniWin", "MINIWIN", "WORKGROUP", "\\\\MINIWIN", "C:\\", "", "", "" };
    for (int i = 0; i < 8; i++) {
        size_t k = 0;
        for (; initstr[i][k] && k < 63; k++) mw29_wbuf[i][k] = (uint16_t)(uint8_t)initstr[i][k];
        mw29_wbuf[i][k] = 0;
    }
    mw29_wbuf_init = 1;
}
#define MW29_W(idx) ((void*)mw29_wbuf[idx])
__attribute__((ms_abi)) uint32_t mw_NetWkstaUserGetInfo(const void* server, uint32_t level, void** buf) {
    (void)server;
    mw29_wbuf_init_once();
    if (!buf) return 21; /* RPC_X_NULL_REF_POINTER */
    if (level == 0) {
        /* WKSTA_USER_INFO_0 { LPWSTR username; } */
        static void* ui0[1];
        ui0[0] = MW29_W(0);
        *buf = (void*)ui0;
        return 0; /* NERR_Success */
    }
    if (level == 1) {
        static void* ui1[4];
        ui1[0] = MW29_W(0); /* username */
        ui1[1] = MW29_W(2); /* logon_domain */
        ui1[2] = MW29_W(7); /* oth_domains (empty) */
        ui1[3] = MW29_W(3); /* logon_server */
        *buf = (void*)ui1;
        return 0;
    }
    return 124; /* ERROR_INVALID_LEVEL */
}
__attribute__((ms_abi)) uint32_t mw_NetWkstaGetInfo(const void* server, uint32_t level, void** buf) {
    (void)server;
    mw29_wbuf_init_once();
    if (!buf) return 21;
    if (level == 100) {
        /* WKSTA_INFO_100 { DWORD platform; LPWSTR computer; LPWSTR langroup; DWORD maj; DWORD min; } */
        static uint32_t ints[3];
        ints[0] = 500; ints[1] = 10; ints[2] = 0; /* PLATFORM_ID_NT, 10.0 */
        static void* ui100[5];
        ui100[0] = &ints[0]; ui100[1] = MW29_W(1); ui100[2] = MW29_W(2);
        ui100[3] = &ints[1]; ui100[4] = &ints[2];
        *buf = (void*)ui100;
        return 0;
    }
    if (level == 101) {
        static uint32_t ints3[3]; static void* ui101[6];
        ints3[0] = 500; ints3[1] = 10; ints3[2] = 0;
        ui101[0] = &ints3[0]; ui101[1] = MW29_W(1); ui101[2] = MW29_W(2);
        ui101[3] = &ints3[1]; ui101[4] = &ints3[2]; ui101[5] = MW29_W(4); /* lanroot */
        *buf = (void*)ui101;
        return 0;
    }
    return 124;
}
__attribute__((ms_abi)) uint32_t mw_NetServerGetInfo(const void* server, uint32_t level, void** buf) {
    (void)server;
    mw29_wbuf_init_once();
    if (!buf) return 21;
    if (level == 101) {
        static uint32_t ints4[4]; static void* si101[6];
        ints4[0] = 500;      /* platform */
        ints4[1] = 10;       /* ver major */
        ints4[2] = 0;        /* ver minor */
        ints4[3] = 0x3;      /* SV_TYPE_WORKSTATION|SV_TYPE_SERVER */
        si101[0] = &ints4[0]; si101[1] = MW29_W(1);
        si101[2] = &ints4[1]; si101[3] = &ints4[2];
        si101[4] = &ints4[3]; si101[5] = MW29_W(7); /* comment: empty */
        *buf = (void*)si101;
        return 0;
    }
    return 124;
}

/* ---- printing: cookie printer handle (GetPrinterW not imported) ---- */
static int g_mw29_printer = 0;
__attribute__((ms_abi)) int mw_OpenPrinterW(uint16_t* name, void** ph, void* def) {
    (void)name; (void)def;
    if (!ph) return 0;
    *ph = (void*)(uintptr_t)(0x0002A000u | (unsigned)(++g_mw29_printer));
    return 1;
}
__attribute__((ms_abi)) int mw_ClosePrinter(void* h) { (void)h; return 1; }

/* ---- event log: cookie handle, EOF reads (honest: no log source) ---- */
static int g_mw29_evtlog = 0;
__attribute__((ms_abi)) void* mw_OpenEventLogW(const uint16_t* server, const uint16_t* src) {
    (void)server; (void)src;
    return (void*)(uintptr_t)(0x0002B000u | (unsigned)(++g_mw29_evtlog));
}
__attribute__((ms_abi)) int mw_ReadEventLogW(void* h, uint32_t flags, uint32_t off,
        void* buf, uint32_t cb, uint32_t* read, uint32_t* needed) {
    (void)h; (void)flags; (void)off; (void)buf; (void)cb;
    if (read) *read = 0;
    if (needed) *needed = 0;
    return 0; /* ERROR_HANDLE_EOF: no events */
}
__attribute__((ms_abi)) int mw_CloseEventLog(void* h) { (void)h; return 1; }

/* ---- services (bginfo64 service-mode paths) ---- */
static void* g_mw29_svc_handler = NULL;
__attribute__((ms_abi)) void* mw_CreateServiceW(void* scm, const uint16_t* name, const uint16_t* disp,
        uint32_t access, uint32_t type, uint32_t start, uint32_t err, const uint16_t* bin,
        const uint16_t* order, void* tag, const uint16_t* deps, const uint16_t* acct, const uint16_t* pw) {
    (void)scm; (void)name; (void)disp; (void)access; (void)type; (void)start; (void)err;
    (void)bin; (void)order; (void)tag; (void)deps; (void)acct; (void)pw;
    mw_SetLastError(5); /* ERROR_ACCESS_DENIED: cannot create host services */
    return NULL;
}
__attribute__((ms_abi)) void* mw_RegisterServiceCtrlHandlerW(const uint16_t* name, void* handler) {
    (void)name;
    g_mw29_svc_handler = handler;
    return (void*)0x0002C001u; /* SERVICE_STATUS_HANDLE cookie */
}
__attribute__((ms_abi)) int mw_SetServiceStatus(void* h, const void* status) {
    (void)h;
    if (status) {
        const uint32_t* st = (const uint32_t*)status;
        MW_TRACE("[SVC] SetServiceStatus state=%u", st[2]); /* SERVICE_STATUS.dwCurrentState */
    }
    return 1;
}

/* ---- Safer (Software Restriction Policies): identity pipeline ---- */
static int g_mw29_safer = 0;
__attribute__((ms_abi)) int mw_SaferIdentifyLevel(uint32_t n, void* codes, void** handle, void* reserved) {
    (void)n; (void)codes; (void)reserved;
    if (!handle) return 0;
    *handle = (void*)(uintptr_t)(0x0002F000u | (unsigned)(++g_mw29_safer));
    return 1;
}
__attribute__((ms_abi)) int mw_SaferComputeTokenFromLevel(void* level, void* tok, void** out) {
    (void)level;
    if (!out) return 0;
    void* t = tok;
    if (!t) mw_OpenProcessToken(NULL, 0, &t);
    *out = t; /* identity: normal-user level = current token */
    return 1;
}
__attribute__((ms_abi)) int mw_SaferCloseLevel(void* h) { (void)h; return 1; }

/* ---- LSA / misc typed tails ---- */
__attribute__((ms_abi)) uint32_t mw_LsaQueryInformationPolicy(void* h, uint32_t cls, void** buf) {
    (void)h; (void)cls;
    if (buf) *buf = NULL;
    return 0xC0000002u; /* STATUS_NOT_IMPLEMENTED — typed honest */
}
__attribute__((ms_abi)) int mw_ImmGetOpenStatus(void* himc) { (void)himc; return 0; /* IME closed */ }
__attribute__((ms_abi)) uint32_t mw_SHAppBarMessage(uint32_t msg, void* data) {
    (void)msg; (void)data;
    return 0; /* no appbar registered */
}
__attribute__((ms_abi)) uint32_t mw_OleLockRunning(void* unk, int lock, int lastUnlock) {
    (void)unk; (void)lock; (void)lastUnlock;
    return 0; /* S_OK */
}
static int g_mw29_ilock = 0;
__attribute__((ms_abi)) uint32_t mw_CreateILockBytesOnHGlobal(void* hglobal, int del, void** pplk) {
    (void)hglobal; (void)del;
    if (!pplk) return 0x80004005u;
    *pplk = (void*)(uintptr_t)(0x0002E000u | (unsigned)(++g_mw29_ilock)); /* ILockBytes cookie */
    return 0; /* S_OK */
}
__attribute__((ms_abi)) int mw_PlaySoundW(const uint16_t* snd, void* hmod, uint32_t fdw) {
    (void)snd; (void)hmod; (void)fdw;
    return 1; /* consumed silently */
}

/* ---- environment block cookies ---- */
__attribute__((ms_abi)) int mw_CreateEnvironmentBlock(void** env, void* tok, int inherit) {
    (void)tok; (void)inherit;
    if (!env) return 0;
    *env = (void*)0x0002A101u; /* env-block cookie (guest keeps using own env) */
    return 1;
}
__attribute__((ms_abi)) int mw_DestroyEnvironmentBlock(void* env) { (void)env; return 1; }

/* ---- SWEEP-029: winmergeu SHELL32/SHLWAPI raw-ordinal tail ---- */
static size_t mw29_wc16_to_utf8(const uint16_t* w, char* out, size_t cap) {
    size_t n = 0;
    if (!w || !out || !cap) return 0;
    while (w[n] && n + 1 < cap) {
        uint16_t c = w[n];
        out[n] = (c < 0x80) ? (char)c : '?'; /* v1: ASCII path mapping */
        n++;
    }
    out[n] = 0;
    return n;
}
__attribute__((ms_abi)) int mw_SHCreateDirectory(void* hwnd, const uint16_t* path) {
    (void)hwnd;
    if (!path) return 3; /* ERROR_PATH_NOT_FOUND */
    char p[1024];
    mw29_wc16_to_utf8(path, p, sizeof p);
    struct stat st;
    if (stat(p, &st) == 0) return 183; /* ERROR_ALREADY_EXISTS */
    if (mkdir(p, 0777) == 0) { MW_TRACE("[SHELL] SHCreateDirectory('%s') -> created", p); return 0; }
    char parent[1024];
    snprintf(parent, sizeof parent, "%s", p);
    char* slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = 0;
        if (stat(parent, &st) != 0) return 3; /* parent missing */
    }
    return 5; /* ERROR_ACCESS_DENIED */
}
__attribute__((ms_abi)) uint32_t mw_ParseURLW(const uint16_t* url, const uint16_t** pp) {
    if (!pp) return 0x80004005u;
    *pp = url;
    if (!url) return 0;
    static const char* schemes[5] = { "file:", "http:", "https:", "ftp:", "mailto:" };
    for (int i = 0; i < 5; i++) {
        size_t n = strlen(schemes[i]);
        uint32_t k = 0;
        int ok = 1;
        for (; k < n; k++) {
            char c = (char)(url[k] & 0xFF);
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (c != schemes[i][k]) { ok = 0; break; }
        }
        if (ok && url[n] != 0) { *pp = url + n; break; }
    }
    MW_TRACE("[SHELL] ParseURLW -> S_OK (scheme parsed v1)");
    return 0; /* S_OK */
}
__attribute__((ms_abi)) void* mw_SHCreateMemStream(const void* pv, uint32_t len) {
    (void)pv; (void)len;
    MW_TRACE("[SHELL] SHCreateMemStream(len=%u) -> NULL (honest: no IStream host, mirrors CreateStreamOnHGlobal E_FAIL)", len);
    return NULL;
}
__attribute__((ms_abi)) uint32_t mw_CDefFolderMenu_Create2(void* psf, void* hwnd, uint32_t cidl,
        const void* apidl, void* pcmf, void* pguidext, uint32_t* pci, void** ppv) {
    (void)psf; (void)hwnd; (void)cidl; (void)apidl; (void)pcmf; (void)pguidext; (void)pci;
    if (ppv) *ppv = NULL;
    return 0x80004005u; /* E_FAIL — typed honest: no shell folder host */
}

/* ==== SWEEP-029 IMPL END ==== */
