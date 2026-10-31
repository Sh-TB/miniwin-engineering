/* ==== SWEEP-035 IMPL START ====
 * Train 4 of 5. Wagon: SW-1203 hook EVENT firing v1 — WH_CBT events are
 * now DELIVERED to live guest hook procs (real ms_abi callbacks):
 * HCBT_CREATEWND(3) pre-create (nonzero return CANCELS creation — the
 * real CBT contract, honored), HCBT_DESTROYWND(4) pre-destroy,
 * HCBT_ACTIVATE(5) on ShowWindow. CBT_CREATEWND{LPCREATESTRUCT,
 * hwndInsertAfter} + CREATESTRUCT x64 layouts per winuser.h.
 * Spliced by scripts/sweep035_apply.py. */

/* ==== SECTION: hookfire ==== */
/* Fired from CreateWindowExW/A, DestroyWindow, ShowWindow (prototypes are
 * declared next to those functions — they are defined later in the file,
 * after the hook machinery). */
#define MW_HCBT_CREATEWND   3
#define MW_HCBT_DESTROYWND  4
#define MW_HCBT_ACTIVATE    5
#define MW_WH_CBT           5
static int mw_hook_fire(int idhook, int code, uintptr_t wp, void* lp) {
    int fired = 0;
    for (int i = 0; i < MW_HOOK_MAX; i++) {
        if (!g_hook[i].live || g_hook[i].idhook != idhook) continue;
        __attribute__((ms_abi)) long long (*proc)(int, uintptr_t, void*) =
            (__attribute__((ms_abi)) long long (*)(int, uintptr_t, void*))g_hook[i].proc;
        long long r = proc(code, wp, lp);
        fired++;
        MW_TRACE("[HOOK] CBT event %d -> hook[%d] %p returned %lld", code, i, g_hook[i].proc, r);
        if (r != 0) return fired; /* stop the chain on cancel/handled */
    }
    return fired;
}
/* Returns nonzero if a live WH_CBT hook CANCELED the creation (real CBT
 * contract: nonzero return from HCBT_CREATEWND prevents the window). */
int mw_hook_fire_create(uint32_t exstyle, uint32_t style, const void* winname,
        const void* clsname, int x, int y, int w, int h, void* parent,
        void* menu, void* inst, void* param) {
    if (x == 0x80000000) x = 0; /* CW_USEDEFAULT defensive normalization */
    /* CREATESTRUCT (x64, winuser.h): 80 bytes */
    struct {
        void*     lpCreateParams;
        void*     hInstance;
        void*     hMenu;
        void*     hwndParent;
        int32_t   cy, cx, y, x;
        int32_t   style;
        const void* lpszName;
        const void* lpszClass;
        uint32_t  dwExStyle;
    } cs;
    memset(&cs, 0, sizeof(cs));
    cs.lpCreateParams = param;
    cs.hInstance = inst;
    cs.hMenu = menu;
    cs.hwndParent = parent;
    cs.cy = h; cs.cx = w; cs.y = y; cs.x = x;
    cs.style = (int32_t)style;
    cs.lpszName = winname;
    cs.lpszClass = clsname;
    cs.dwExStyle = exstyle;
    struct { void* lpcs; void* hwndInsertAfter; } cbtw;
    cbtw.lpcs = &cs;
    cbtw.hwndInsertAfter = (void*)0x00000002FFFFFFFFull; /* HWND_TOP bogus-safe */
    for (int i = 0; i < MW_HOOK_MAX; i++) {
        if (!g_hook[i].live || g_hook[i].idhook != MW_WH_CBT) continue;
        __attribute__((ms_abi)) long long (*proc)(int, uintptr_t, void*) =
            (__attribute__((ms_abi)) long long (*)(int, uintptr_t, void*))g_hook[i].proc;
        long long r = proc(MW_HCBT_CREATEWND, (uintptr_t)MW_GUI_HWND, &cbtw);
        MW_TRACE("[HOOK] HCBT_CREATEWND -> hook[%d] %p returned %lld", i, g_hook[i].proc, r);
        if (r != 0) {
            MW_TRACE("[HOOK] HCBT_CREATEWND canceled by hook -> NULL");
            return 1; /* canceled (real CBT contract) */
        }
        break; /* one CBT hook consumed (Windows calls the newest first) */
    }
    return 0;
}
void mw_hook_fire_destroy(void* hwnd) {
    mw_hook_fire(MW_WH_CBT, MW_HCBT_DESTROYWND, (uintptr_t)hwnd, NULL);
}
void mw_hook_fire_activate(void* hwnd) {
    /* ACTIVATESTRUCT { hwndActive, hwndDeactivate } */
    uintptr_t st[2] = { (uintptr_t)hwnd, 0 };
    mw_hook_fire(MW_WH_CBT, MW_HCBT_ACTIVATE, (uintptr_t)hwnd, st);
}

/* ==== SWEEP-035 IMPL END ==== */

/* ==== SECTION: apppolicy ==== */
/* SW-1204 (SWEEP-035): winmergeu's worker thread died at
 * GetProcAddress(api-ms-win-appmodel-runtime-l1-1-2,
 * AppPolicyGetThreadInitializationType) -> NULL, then went silent (no API
 * calls) while the main thread spun Sleep(0) x986K waiting for worker init
 * (trace /tmp/wm1, T76c0 26 lines then nothing). Real API (appmodel.h):
 * returns APPPOLICY_THREADINITIALIZATIONTYPE — 0 = NONE (no automatic
 * apartment init), the honest headless answer that lets the worker
 * proceed. Added to the dispatch table so GetProcAddress resolves it. */
__attribute__((ms_abi)) int32_t mw_AppPolicyGetThreadInitializationType(void* process, uint32_t* policy) {
    (void)process;
    if (policy) *policy = 0; /* APPPOLICY_THREADINITIALIZATIONTYPE_NONE */
    MW_TRACE("AppPolicyGetThreadInitializationType -> S_OK (NONE)");
    return 0; /* S_OK */
}

/* ==== SWEEP-035 IMPL END ==== */
