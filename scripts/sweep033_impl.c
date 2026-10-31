/* ==== SWEEP-033 IMPL START ====
 * Train 2 of 5. Wagons: SW-1102 ordinal triage (SHLWAPI.176
 * IUnknown_QueryService, COMCTL32.345 TaskDialogIndirect, ACLUI.1
 * CreateSecurityPage — names via wine-mirror spec + MS Learn/SO evidence
 * 2026-10-21; ole32.277/16 = KNOWN-UNKNOWN, no credible name source, stay
 * stubbed), SW-1156 ExtFloodFill real scanline seed fill.
 * Spliced by scripts/sweep033_apply.py. */

/* ==== SECTION: floodfill ==== */
/* ---- SW-1156: ExtFloodFill real scanline seed fill (replaces no-op v1).
 * Fill color = the DC's selected brush (g_cur_brush, hollow = no paint) —
 * the color PARAM is the border (FLOODFILLBORDER) or surface
 * (FLOODFILLSURFACE) match color, NOT the fill color. Scanline algorithm
 * with an explicit span stack (no recursion, 1M-pixel safety cap). Colors
 * compare in the surface's own packing (mwb_get) so border/surface checks
 * are exact. ---- */
__attribute__((ms_abi)) int mw_ExtFloodFill(void* hdc, int x, int y, uint32_t col, uint32_t type) {
    MWBmp* S = mw_dc_surface(hdc);
    if (!S || x < 0 || y < 0 || x >= S->w || y >= S->h) {
        MW_TRACE("[GDI] ExtFloodFill(%d,%d) -> FALSE (no surface / OOB)", x, y);
        return 0;
    }
    if (g_brush_hollow) { /* NULL_BRUSH selected: nothing to paint */
        MW_TRACE("[GDI] ExtFloodFill(%d,%d) -> TRUE (NULL brush, no paint)", x, y);
        return 1;
    }
    uint32_t fill = g_cur_brush;
    uint32_t match = mw_cr2rgb(col);
    uint32_t start = mwb_get(S, x, y);
    if (type == 0u /* FLOODFILLBORDER */) {
        if (start == match) { /* starting inside the border: nothing to fill */
            MW_TRACE("[GDI] ExtFloodFill(border) start==border -> FALSE");
            return 0;
        }
    } else if (type == 1u /* FLOODFILLSURFACE */) {
        if (start != match) { /* surface color not at seed: nothing to fill */
            MW_TRACE("[GDI] ExtFloodFill(surface) start!=surface -> FALSE");
            return 0;
        }
    } else {
        mw_SetLastError(87);
        return 0;
    }
    int filled = 0;
    const int CAP = 1 << 20; /* 1M pixels: sweep-safety cap */
    #define MW_FF_PRED(px) (type == 0u ? ((px) != match) : ((px) == match))
    /* span stack: (y, xl, xr) triples; 4096 spans is ample for UI surfaces */
    static int sy[4096], sxl[4096], sxr[4096];
    int sp = 0;
    sy[sp] = y; sxl[sp] = x; sxr[sp] = x; sp++;
    while (sp > 0 && filled < CAP) {
        sp--;
        int yy = sy[sp], xl = sxl[sp], xr = sxr[sp];
        /* extend the span left/right along the predicate */
        while (xl > 0 && MW_FF_PRED(mwb_get(S, xl - 1, yy))) xl--;
        while (xr < S->w - 1 && MW_FF_PRED(mwb_get(S, xr + 1, yy))) xr++;
        for (int i = xl; i <= xr; i++) { mwb_set(S, i, yy, fill); filled++; }
        /* seed the spans above and below that touch [xl,xr] */
        for (int d = -1; d <= 1; d += 2) {
            int ny = yy + d;
            if (ny < 0 || ny >= S->h) continue;
            int i = xl;
            while (i <= xr) {
                if (MW_FF_PRED(mwb_get(S, i, ny))) {
                    int j = i;
                    while (j + 1 <= xr && MW_FF_PRED(mwb_get(S, j + 1, ny))) j++;
                    if (sp < 4096) { sy[sp] = ny; sxl[sp] = i; sxr[sp] = j; sp++; }
                    i = j + 1;
                } else i++;
            }
        }
    }
    MW_TRACE("[GDI] ExtFloodFill(%d,%d) %s fill=#%06x -> %d px (real scanline)",
             x, y, type == 0u ? "border" : "surface", fill, filled);
    return 1;
    #undef MW_FF_PRED
}

/* ==== SECTION: ordnames ==== */
/* ---- SW-1102 ordinal wagons (procexp64): SHLWAPI.176, COMCTL32.345,
 * ACLUI.1. Real delegation / honest-v1 semantics per the established
 * discipline. ---- */
__attribute__((ms_abi)) uint32_t mw_IUnknown_QueryService(void* punk, const void* guidService, const void* riid, void** out) {
    (void)guidService; /* service SID routing needs a service-provider
                        * object model; direct QI delegation is the v1 */
    if (!out) return 0x80004003u; /* E_POINTER */
    *out = NULL;
    if (!punk) return 0x80004005u; /* E_FAIL: no object to query */
    void** vt = *(void***)punk;
    if (!vt || !vt[0]) return 0x80004002u; /* E_NOINTERFACE */
    __attribute__((ms_abi)) uint32_t (*qi)(void*, const void*, void**) =
        (__attribute__((ms_abi)) uint32_t (*)(void*, const void*, void**))vt[0];
    uint32_t hr = qi(punk, riid, out);
    MW_TRACE("IUnknown_QueryService -> %08x (delegated QI)", hr);
    return hr;
}
/* TASKDIALOGCONFIG has buttons the headless host cannot show; the canned
 * user-CANCEL contract (SW-826..830 family) applies: S_OK + IDCANCEL. */
__attribute__((ms_abi)) uint32_t mw_TaskDialogIndirect(const void* cfg, uint32_t* pnButton, int* pRadioButton, int* pfVerif) {
    if (!cfg) return 0x80070057u; /* E_INVALIDARG */
    if (pnButton) *pnButton = 2; /* IDCANCEL */
    if (pRadioButton) *pRadioButton = 0;
    if (pfVerif) *pfVerif = 0;
    MW_TRACE("TaskDialogIndirect -> S_OK (canned IDCANCEL, headless)");
    return 0u;
}
#define MW_SECPAGE_BASE 0x00B70000u /* HPROPSHEETPAGE cookies (low16=0) */
static uint8_t g_secpage_live[8];
__attribute__((ms_abi)) void* mw_CreateSecurityPage(void* psi) {
    if (!psi) { mw_SetLastError(87); return NULL; }
    for (int i = 0; i < 8; i++) if (!g_secpage_live[i]) {
        g_secpage_live[i] = 1;
        void* h = (void*)(uintptr_t)(MW_SECPAGE_BASE | (unsigned)(i + 1));
        MW_TRACE("CreateSecurityPage(%p) -> %p (page created, never shown)", psi, h);
        return h;
    }
    mw_SetLastError(14);
    return NULL;
}

/* ==== SWEEP-033 IMPL END ==== */
