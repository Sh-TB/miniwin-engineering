/* ============================================================
 * SWEEP-027 wagon batch — cross-program unresolved tails
 * (drivers: artifacts/sweep027/recon — autoruns64 52, bginfo64 201,
 *  cports 20, searchmyfiles 17, winmergeu 202, procexp64 161)
 * V1 semantics: real where cheap (strings/INI/accel/CLSID/time),
 * typed-honest failures for host-impossible services (WinHttp,
 * services, wow64, themes->classic fallback, GDI+, net).
 * ============================================================ */

/* ---- SW-1128 accel store: real create/copy/destroy ---- */
#define MW_ACCEL_BASE 0x00AC0000u
typedef struct { uint32_t fvirt_key_cmd[2]; int n; int live; } mw_accel_slot_t;
static mw_accel_slot_t g_accel_slots[8];
static uint32_t g_accel_next = 1;
__attribute__((ms_abi)) void* mw_CreateAcceleratorTableW(const void* acc, int n) {
    if (!acc || n <= 0 || n > 512) return NULL;
    int slot = -1;
    for (int i = 0; i < 8; i++) if (!g_accel_slots[i].live && slot < 0) slot = i;
    if (slot < 0) return NULL;
    int copy = n > 256 ? 256 : n;
    memcpy(g_accel_slots[slot].fvirt_key_cmd, acc, (size_t)copy * 6u);
    g_accel_slots[slot].n = n; g_accel_slots[slot].live = 1;
    uintptr_t h = MW_ACCEL_BASE | (uintptr_t)(slot + 1);
    MW_TRACE("CreateAcceleratorTableW(n=%d) -> %p", n, (void*)h);
    return (void*)h;
}
__attribute__((ms_abi)) int mw_CopyAcceleratorTableW(void* h, void* out, int n) {
    uintptr_t v = (uintptr_t)h;
    if ((v & 0xFFFF0000u) != MW_ACCEL_BASE) return 0;
    int slot = (int)(v & 0xFFu) - 1;
    if (slot < 0 || slot >= 8 || !g_accel_slots[slot].live) return 0;
    if (!out) return g_accel_slots[slot].n;
    int copy = g_accel_slots[slot].n < n ? g_accel_slots[slot].n : n;
    if (copy > 0) memcpy(out, g_accel_slots[slot].fvirt_key_cmd, (size_t)copy * 6u);
    MW_TRACE("CopyAcceleratorTableW(%p, n=%d) -> %d", h, n, copy);
    return copy;
}
__attribute__((ms_abi)) int mw_DestroyAcceleratorTableW(void* h) {
    uintptr_t v = (uintptr_t)h;
    if ((v & 0xFFFF0000u) == MW_ACCEL_BASE) {
        int slot = (int)(v & 0xFFu) - 1;
        if (slot >= 0 && slot < 8) g_accel_slots[slot].live = 0;
    }
    return 1;
}
__attribute__((ms_abi)) int mw_TranslateAcceleratorA(void* hwnd, void* tab, void* msg) {
    (void)hwnd; (void)tab; (void)msg;
    return 0; /* no translation: fall through to normal dispatch */
}
/* mw_TranslateAcceleratorW: already exists upstream (SWEEP-025) */

/* ---- SW-1128 menu/string/misc user32 tail ---- */
__attribute__((ms_abi)) int mw_SetCursorPos(int x, int y) {
    MW_TRACE("SetCursorPos(%d,%d) -> TRUE (headless)", x, y);
    return 1;
}
__attribute__((ms_abi)) int mw_GetMenuStringW(void* menu, uint32_t id, uint16_t* buf, int cch, uint32_t flags) {
    (void)menu; (void)id; (void)flags;
    if (buf && cch > 0) buf[0] = 0;
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_GetMenuState(void* menu, uint32_t id, uint32_t flags) {
    (void)menu; (void)id; (void)flags;
    return 0xFFFFFFFFu; /* HIBYTE=MF_GRAYED|LOBYTE=0: item absent */
}
__attribute__((ms_abi)) uint32_t mw_GetMenuCheckMarkDimensions(void) {
    return (13u << 16) | 13u; /* 13x13 classic check glyph */
}
__attribute__((ms_abi)) int mw_SetMenuItemInfoA(void* menu, uint32_t item, int bypos, const void* info) {
    (void)menu; (void)item; (void)bypos; (void)info;
    return 1;
}
__attribute__((ms_abi)) int mw_DrawTextExW(void* dc, uint16_t* s, int c, void* rect, uint32_t fmt, void* pdt) {
    (void)dc; (void)fmt; (void)pdt;
    if (rect) { int32_t* r = (int32_t*)rect; r[3] = r[1] + 16; } /* one line height */
    MW_TRACE("DrawTextExW(c=%d fmt=0x%x) -> 16", c, fmt);
    return 16;
}
__attribute__((ms_abi)) intptr_t mw_DefFrameProcW(void* hwnd, void* client, uint32_t msg, uintptr_t wp, intptr_t lp) {
    (void)hwnd; (void)client; (void)msg; (void)wp; (void)lp;
    return 0;
}
__attribute__((ms_abi)) intptr_t mw_DefMDIChildProcW(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp) {
    (void)hwnd; (void)msg; (void)wp; (void)lp;
    return 0;
}
__attribute__((ms_abi)) int mw_WaitMessage(void) { return 1; }
__attribute__((ms_abi)) int mw_ToUnicodeEx(uint32_t vk, uint32_t sc, const uint8_t* kbd,
        uint16_t* buf, int cch, uint32_t flags, void* hkl) {
    (void)vk; (void)sc; (void)kbd; (void)flags; (void)hkl;
    if (buf && cch > 0) buf[0] = 0;
    return 0;
}
__attribute__((ms_abi)) int mw_WinHelpW(void* hwnd, const char* file, uint32_t cmd, uintptr_t data) {
    (void)hwnd; (void)file; (void)cmd; (void)data;
    MW_TRACE("WinHelpW(cmd=%u) -> TRUE (no help engine)", cmd);
    return 1;
}
__attribute__((ms_abi)) int mw_UnpackDDElParam(uint32_t msg, intptr_t lp, uintptr_t* plo, uintptr_t* phi) {
    (void)msg;
    if (plo) *plo = 0;
    if (phi) *phi = (uintptr_t)lp;
    return 0;
}
__attribute__((ms_abi)) int mw_TranslateMDISysAccel(void* client, void* msg) {
    (void)client; (void)msg;
    return 0;
}
__attribute__((ms_abi)) long mw_TabbedTextOutW(void* dc, int x, int y, const uint16_t* s, int c,
        int nt, const int* tabs, int org) {
    (void)dc; (void)nt; (void)tabs; (void)org;
    int n = c < 0 ? 0 : c;
    if (c < 0 && s) { while (s[n]) n++; }
    MW_TRACE("TabbedTextOutW(%d,%d c=%d) -> %d", x, y, c, n * 8);
    return n * 8;
}
__attribute__((ms_abi)) int mw_UnregisterHotKey(void* hwnd, int id) { (void)hwnd; (void)id; return 1; }
__attribute__((ms_abi)) void* mw_SetClipboardViewer(void* hwnd) { (void)hwnd; return NULL; }
__attribute__((ms_abi)) int mw_GetKeyNameTextW(intptr_t lp, uint16_t* buf, int cch) {
    (void)lp;
    if (buf && cch > 0) buf[0] = 0;
    return 0;
}
__attribute__((ms_abi)) uint16_t mw_GetUserDefaultUILanguage(void) { return 0x0409u; }
__attribute__((ms_abi)) void* mw_CopyImage(void* h, uint32_t type, int cx, int cy, uint32_t flags) {
    (void)type; (void)cx; (void)cy; (void)flags;
    return h; /* identity: same image handle */
}
__attribute__((ms_abi)) long mw_SetWindowLongA(void* hwnd, int idx, long val) {
    return (long)mw_SetWindowLongPtrW(hwnd, idx, (intptr_t)val);
}
__attribute__((ms_abi)) long mw_GetWindowLongA(void* hwnd, int idx) {
    return (long)mw_GetWindowLongPtrW(hwnd, idx);
}
__attribute__((ms_abi)) int mw_GetThreadSelectorEntry(void* h, uint32_t sel, void* e) {
    (void)h; (void)sel; (void)e;
    mw_SetLastError(87); /* ERROR_INVALID_PARAMETER: no LDT on this engine */
    return 0;
}
__attribute__((ms_abi)) int mw_EnumFontsW(void* dc, const uint16_t* face, void* proc, intptr_t lp) {
    (void)dc; (void)face; (void)proc; (void)lp;
    return 0; /* zero faces enumerated: caller falls back to stock */
}
__attribute__((ms_abi)) int mw_ImageList_Copy(void* dst, int di, void* src, int si, uint32_t flags) {
    (void)dst; (void)di; (void)src; (void)si; (void)flags;
    return 1;
}
__attribute__((ms_abi)) int mw_UpdateLayeredWindow(void* hwnd, void* hdcDst, void* pptDst, void* psize,
        void* hdcSrc, void* pptSrc, uint32_t key, void* blend, uint32_t flags) {
    (void)hwnd; (void)hdcDst; (void)pptDst; (void)psize; (void)hdcSrc;
    (void)pptSrc; (void)key; (void)blend; (void)flags;
    mw_SetLastError(87);
    return 0; /* layered windows not supported headless */
}
__attribute__((ms_abi)) int mw_TransparentBlt(void* hdc, int x, int y, int w, int h,
        void* hdc2, int x2, int y2, int w2, int h2, uint32_t key) {
    (void)hdc; (void)hdc2; (void)key;
    MW_TRACE("TransparentBlt(%d,%d %dx%d <- %d,%d %dx%d key=0x%x) v1",
             x, y, w, h, x2, y2, w2, h2, key);
    return 1;
}
__attribute__((ms_abi)) int mw_SubtractRect(void* dst, const void* a, const void* b) {
    if (!dst || !a) return 0;
    memcpy(dst, a, 16);
    if (!b) return 1;
    const int32_t* r = (const int32_t*)a;
    const int32_t* s = (const int32_t*)b;
    int covered = (s[0] <= r[0] && s[1] <= r[1] && s[2] >= r[2] && s[3] >= r[3]);
    MW_TRACE("SubtractRect -> %d", covered ? 0 : 1);
    return covered ? 0 : 1;
}

/* ---- SW-1128 kernel32: INI/registry/services/wow64/misc ---- */
__attribute__((ms_abi)) uint32_t mw_GetPrivateProfileStringA(const char* app, const char* key,
        const char* def, char* buf, uint32_t size, const char* file) {
    (void)app; (void)key; (void)file;
    if (!buf || size == 0) return 0;
    uint32_t n = 0;
    if (def && (uintptr_t)def >= 0x10000u) {
        for (; n + 1 < size && def[n]; n++) buf[n] = def[n];
    }
    buf[n] = 0;
    return n;
}
__attribute__((ms_abi)) uint32_t mw_GetPrivateProfileIntA(const char* app, const char* key,
        int def, const char* file) {
    (void)app; (void)key; (void)file;
    return (uint32_t)def;
}
__attribute__((ms_abi)) int mw_WritePrivateProfileStringA(const char* app, const char* key,
        const char* val, const char* file) {
    MW_TRACE("WritePrivateProfileStringA(%s.%s -> %s) accepted v1", app ? app : "?", key ? key : "?", file ? file : "?");
    return 1;
}
__attribute__((ms_abi)) int mw_WritePrivateProfileStringW(const uint16_t* app, const uint16_t* key,
        const uint16_t* val, const uint16_t* file) {
    MW_TRACE("WritePrivateProfileStringW accepted v1");
    return 1;
}
__attribute__((ms_abi)) int32_t mw_RegEnumKeyW(void* key, uint32_t idx, char* name, uint32_t namelen) {
    uint32_t cap = namelen;
    return mw_RegEnumKeyExA(key, idx, name, &cap, NULL, NULL, NULL, NULL);
}
#define MW_SCMSVC_BASE 0x005C0000u
static uint32_t g_scmsvc_next = 1;
__attribute__((ms_abi)) void* mw_OpenServiceW(void* scm, const uint16_t* name, uint32_t access) {
    (void)scm; (void)access;
    MW_TRACE("OpenServiceW -> cookie %u", g_scmsvc_next);
    return (void*)(uintptr_t)(MW_SCMSVC_BASE | (g_scmsvc_next++ & 0xFFFFu));
}
__attribute__((ms_abi)) void* mw_OpenServiceA(void* scm, const char* name, uint32_t access) {
    (void)name;
    return (void*)(uintptr_t)(MW_SCMSVC_BASE | (g_scmsvc_next++ & 0xFFFFu));
}
__attribute__((ms_abi)) int mw_QueryServiceStatus(void* svc, void* status) {
    (void)svc;
    if (!status) return 0;
    uint32_t* s = (uint32_t*)status;
    s[0] = 0x10u;      /* dwServiceType = SERVICE_WIN32_OWN_PROCESS */
    s[1] = 0x00000001u; /* dwCurrentState = SERVICE_STOPPED */
    for (int i = 2; i < 9; i++) s[i] = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_StartServiceW(void* svc, int argc, const uint16_t** argv) {
    (void)svc; (void)argv;
    MW_TRACE("StartServiceW(argc=%d) -> fail ERROR_ACCESS_DENIED (no SCM)", argc);
    mw_SetLastError(5);
    return 0;
}
__attribute__((ms_abi)) int mw_StartServiceA(void* svc, int argc, const char** argv) {
    (void)argv;
    mw_SetLastError(5);
    return 0;
}
__attribute__((ms_abi)) int mw_StartServiceCtrlDispatcherW(const void* table) {
    (void)table;
    MW_TRACE("StartServiceCtrlDispatcherW -> 1063 FAILED_SERVICE_CONTROLLER_CONNECT (app path)");
    mw_SetLastError(1063);
    return 0;
}
__attribute__((ms_abi)) int mw_GetSystemWow64DirectoryW(uint16_t* buf, uint32_t size) {
    MW_TRACE("GetSystemWow64DirectoryW -> ERROR_CALL_NOT_IMPLEMENTED (no wow64 layer)");
    mw_SetLastError(120);
    return 0;
}
__attribute__((ms_abi)) int mw_Wow64DisableWow64FsRedirection(void** old) {
    if (old) *old = NULL;
    return 1; /* nothing to disable: host has no wow64 fs view */
}
__attribute__((ms_abi)) int mw_Wow64RevertWow64FsRedirection(void* old) {
    (void)old;
    return 1;
}
__attribute__((ms_abi)) int32_t mw_FindPackagesByPackageFamily(const uint16_t* fam, uint32_t filter,
        uint32_t* count, uint32_t* buf, uint32_t buflen) {
    (void)fam; (void)filter; (void)buf; (void)buflen;
    if (count) *count = 0;
    return 0; /* success, zero packages */
}
__attribute__((ms_abi)) uint32_t mw_QueryDosDeviceW(const char* dev, char* buf, int max) {
    (void)dev;
    if (buf && max > 0) buf[0] = 0;
    mw_SetLastError(2);
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_K32GetMappedFileNameW(void* proc, void* addr, char* buf, uint32_t cch) {
    (void)proc; (void)addr;
    if (buf && cch > 0) buf[0] = 0;
    mw_SetLastError(1168); /* ERROR_NOT_FOUND */
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_SearchPathW(const uint16_t* path, const uint16_t* name,
        const uint16_t* ext, uint32_t cch, uint16_t* buf, uint16_t** filepart) {
    (void)path; (void)ext;
    if (filepart) *filepart = NULL;
    if (buf && cch > 0) buf[0] = 0;
    mw_SetLastError(2);
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_GetTempFileNameA(const char* path, const char* prefix,
        uint32_t unique, char* buf) {
    char tmp[512];
    if (!buf) return 0;
    for (uint32_t t = (unique ? unique & 0xFFFFu : 1u); t <= 0xFFFFu; t++) {
        snprintf(tmp, sizeof(tmp), "%s\\%s%04X.tmp", path ? path : ".", prefix ? prefix : "tmp", (unsigned)t);
        FILE* f = fopen(tmp, "wb");
        if (f) { fclose(f); snprintf(buf, 512, "%s", tmp); MW_TRACE("GetTempFileNameA -> %s", tmp); return t; }
        if (unique) break;
    }
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_WinExec(const char* cmd, uint32_t show) {
    MW_TRACE("WinExec('%s' show=%u) -> SE_ERR (no child exec v1)", cmd ? cmd : "", show);
    mw_SetLastError(2);
    return 2;
}
__attribute__((ms_abi)) int mw_GetTimeFormatA(uint32_t loc, uint32_t flags, const void* st,
        const char* fmt, char* buf, int cch) {
    (void)loc; (void)flags; (void)fmt;
    if (!buf || cch <= 0) return 0;
    unsigned h = 0, m = 0, s = 0;
    if (st) {
        const uint16_t* t = (const uint16_t*)st; /* SYSTEMTIME: hour@8 min@10 sec@12 */
        h = t[4]; m = t[5]; s = t[6];
    } else {
        time_t now = time(NULL);
        struct tm tmv; localtime_r(&now, &tmv);
        h = (unsigned)tmv.tm_hour; m = (unsigned)tmv.tm_min; s = (unsigned)tmv.tm_sec;
    }
    int n = snprintf(buf, (size_t)cch, "%02u:%02u:%02u", h, m, s);
    return n < 0 ? 0 : n + 1;
}
__attribute__((ms_abi)) int mw_GetNumberFormatA(uint32_t loc, uint32_t flags, const char* value,
        const void* fmt, char* buf, int cch) {
    (void)loc; (void)flags; (void)fmt;
    if (!value || !buf || cch <= 0) return 0;
    int n = 0;
    for (; value[n] && n + 1 < cch; n++) buf[n] = value[n];
    buf[n] = 0;
    return n + 1;
}
__attribute__((ms_abi)) int mw_GetNumberFormatEx(uint32_t loc, uint32_t flags, const uint16_t* value,
        const void* fmt, uint16_t* buf, int cch) {
    (void)loc; (void)flags; (void)fmt;
    if (!value || !buf || cch <= 0) return 0;
    int n = 0;
    for (; value[n] && n + 1 < cch; n++) buf[n] = value[n];
    buf[n] = 0;
    return n + 1;
}
__attribute__((ms_abi)) void* mw_GlobalReAlloc(void* p, size_t bytes, uint32_t flags) {
    (void)flags;
    if (!p) return NULL;
    int s = w3_reg_find(p);
    void* q = realloc(p, bytes ? bytes : 1);
    if (q && q != p) {
        if (s >= 0) w3_reg_del(p);
        w3_reg_add(q);
    }
    MW_TRACE("GlobalReAlloc(%p, 0x%lx) = %p", p, (unsigned long)bytes, q);
    return q;
}

/* ---- SW-1128 threadpool timers: cookies, no fire v1 ---- */
#define MW_TPTIMER_BASE 0x00B10000u
static uint32_t g_tptimer_next = 1;
__attribute__((ms_abi)) void* mw_CreateThreadpoolTimer(void* cb, void* ctx, void* env) {
    (void)cb; (void)ctx; (void)env;
    return (void*)(uintptr_t)(MW_TPTIMER_BASE | (g_tptimer_next++ & 0xFFFFu));
}
__attribute__((ms_abi)) void mw_SetThreadpoolTimer(void* t, const void* due, uint32_t period, uint32_t winlen) {
    MW_TRACE("SetThreadpoolTimer(%p due=%p period=%u win=%u) armed no-fire v1", t, due, period, winlen);
}
__attribute__((ms_abi)) void mw_CloseThreadpoolTimer(void* t) { (void)t; }
__attribute__((ms_abi)) void mw_WaitForThreadpoolTimerCallbacks(void* t, int cancel) { (void)t; (void)cancel; }
__attribute__((ms_abi)) int mw_IsThreadpoolTimerSet(void* t) { (void)t; return 0; }

/* ---- SW-1128 WinHttp family: typed no-network failures ---- */
#define MW_WINHTTP_BASE 0x00B00000u
static uint32_t g_winhttp_next = 1;
static void* mw_winhttp_cookie(void) { return (void*)(uintptr_t)(MW_WINHTTP_BASE | (g_winhttp_next++ & 0xFFFFu)); }
__attribute__((ms_abi)) void* mw_WinHttpOpen(const uint16_t* ua, uint32_t access, const uint16_t* proxy,
        const uint16_t* bypass, uint32_t flags) {
    (void)ua; (void)access; (void)proxy; (void)bypass; (void)flags;
    MW_TRACE("WinHttpOpen -> cookie (session)");
    return mw_winhttp_cookie();
}
__attribute__((ms_abi)) void* mw_WinHttpConnect(void* h, const uint16_t* host, uint32_t port, uint32_t res) {
    (void)h; (void)host; (void)port; (void)res;
    return mw_winhttp_cookie();
}
__attribute__((ms_abi)) void* mw_WinHttpOpenRequest(void* h, const uint16_t* verb, const uint16_t* obj,
        const uint16_t* ver, const uint16_t* ref, const uint16_t** types, uint32_t flags) {
    (void)h; (void)verb; (void)obj; (void)ver; (void)ref; (void)types; (void)flags;
    return mw_winhttp_cookie();
}
__attribute__((ms_abi)) int mw_WinHttpSendRequest(void* h, const uint16_t* hdrs, uint32_t hlen,
        void* opt, uint32_t optlen, uint32_t total, uintptr_t ctx) {
    (void)h; (void)hdrs; (void)hlen; (void)opt; (void)optlen; (void)total; (void)ctx;
    mw_SetLastError(12029); /* ERROR_WINHTTP_CANNOT_CONNECT */
    return 0;
}
__attribute__((ms_abi)) int mw_WinHttpReceiveResponse(void* h, void* res) { (void)h; (void)res; return 0; }
__attribute__((ms_abi)) int mw_WinHttpReadData(void* h, void* buf, uint32_t toread, uint32_t* read) {
    (void)h; (void)buf; (void)toread;
    if (read) *read = 0;
    return 0;
}
__attribute__((ms_abi)) int mw_WinHttpWriteData(void* h, const void* buf, uint32_t towrite, uint32_t* written) {
    (void)h; (void)buf; (void)towrite;
    if (written) *written = 0;
    return 0;
}
__attribute__((ms_abi)) int mw_WinHttpQueryHeaders(void* h, uint32_t info, const uint16_t* name,
        void* buf, uint32_t* len, uint32_t* index) {
    (void)h; (void)info; (void)name; (void)buf; (void)index;
    if (len) *len = 0;
    return 0;
}
__attribute__((ms_abi)) int mw_WinHttpQueryDataAvailable(void* h, uint32_t* avail) {
    (void)h;
    if (avail) *avail = 0;
    return 0;
}
__attribute__((ms_abi)) int mw_WinHttpSetOption(void* h, uint32_t opt, void* buf, uint32_t len) {
    (void)h; (void)opt; (void)buf; (void)len;
    return 1;
}
__attribute__((ms_abi)) int mw_WinHttpCloseHandle(void* h) { (void)h; return 1; }
__attribute__((ms_abi)) int mw_WinHttpGetProxyForUrl(void* h, const uint16_t* url, void* opts, void* info) {
    (void)h; (void)url; (void)opts; (void)info;
    mw_SetLastError(12180); /* ERROR_WINHTTP_AUTODETECTION_FAILED */
    return 0;
}

/* ---- SW-1128 shlwapi strings: real ---- */
__attribute__((ms_abi)) const uint16_t* mw_StrChrW(const uint16_t* s, uint16_t c) {
    if (!s) return NULL;
    for (const uint16_t* p = s; *p; p++) if (*p == c) return p;
    if (c) return NULL;
    const uint16_t* e = s;
    while (*e) e++; /* NUL sentinel: StrChrW(., 0) returns end pointer */
    return e;
}
__attribute__((ms_abi)) const uint16_t* mw_StrStrIW(const uint16_t* hay, const uint16_t* needle) {
    if (!hay || !needle) return NULL;
    for (const uint16_t* p = hay; *p; p++) {
        const uint16_t* a = p; const uint16_t* b = needle;
        while (*a && *b) {
            uint16_t ca = *a, cb = *b;
            if (ca >= L'A' && ca <= L'Z') ca = (uint16_t)(ca + 32);
            if (cb >= L'A' && cb <= L'Z') cb = (uint16_t)(cb + 32);
            if (ca != cb) break;
            a++; b++;
        }
        if (!*b) return p;
    }
    return NULL;
}
__attribute__((ms_abi)) int mw_StrCmpIW(const uint16_t* a, const uint16_t* b) {
    while (*a && *b) {
        uint16_t ca = *a, cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca = (uint16_t)(ca + 32);
        if (cb >= L'A' && cb <= L'Z') cb = (uint16_t)(cb + 32);
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    return (int)*a - (int)*b;
}
__attribute__((ms_abi)) int mw_StrCmpLogicalW(const uint16_t* a, const uint16_t* b) {
    for (;;) {
        int da = (*a >= L'0' && *a <= L'9'), db = (*b >= L'0' && *b <= L'9');
        if (da && db) {
            unsigned long long va = 0, vb = 0;
            while (*a >= L'0' && *a <= L'9') { va = va * 10 + (unsigned)(*a - L'0'); a++; }
            while (*b >= L'0' && *b <= L'9') { vb = vb * 10 + (unsigned)(*b - L'0'); b++; }
            if (va != vb) return va < vb ? -1 : 1;
            continue;
        }
        if (*a != *b) return (int)*a - (int)*b;
        if (!*a) return 0;
        a++; b++;
    }
}
__attribute__((ms_abi)) int mw_StrTrimW(uint16_t* s, const uint16_t* set) {
    if (!s || !set) return 0;
    int n = 0;
    while (s[n]) n++;
    int e = n;
    while (e > 0) {
        int found = 0;
        for (const uint16_t* t = set; *t; t++) if (s[e - 1] == *t) { found = 1; break; }
        if (!found) break;
        e--;
    }
    int b = 0;
    while (b < e) {
        int found = 0;
        for (const uint16_t* t = set; *t; t++) if (s[b] == *t) { found = 1; break; }
        if (!found) break;
        b++;
    }
    int m = e - b;
    if (m > 0) memmove(s, s + b, (size_t)m * 2u);
    s[m] = 0;
    return m > 0;
}
__attribute__((ms_abi)) int mw_StrFormatKBSizeW(unsigned long long q, uint16_t* buf, uint32_t cch) {
    char tmp[64];
    unsigned long long kb = (q + 1023ull) / 1024ull;
    snprintf(tmp, sizeof(tmp), "%llu KB", kb);
    int n = 0;
    if (buf) for (; tmp[n] && n + 1 < (int)cch; n++) buf[n] = (uint16_t)(unsigned char)tmp[n];
    if (buf) buf[n] = 0;
    return n;
}
__attribute__((ms_abi)) int mw_StrFormatByteSizeW(long long q, uint16_t* buf, uint32_t cch) {
    char tmp[64];
    double v = (double)q;
    const char* unit = " bytes";
    if (v >= 1073741824.0) { v /= 1073741824.0; unit = " GB"; }
    else if (v >= 1048576.0) { v /= 1048576.0; unit = " MB"; }
    else if (v >= 1024.0) { v /= 1024.0; unit = " KB"; }
    if (v >= 10.0 || q < 1024) snprintf(tmp, sizeof(tmp), "%.0f%s", v, unit);
    else snprintf(tmp, sizeof(tmp), "%.1f%s", v, unit);
    int n = 0;
    if (buf) for (; tmp[n] && n + 1 < (int)cch; n++) buf[n] = (uint16_t)(unsigned char)tmp[n];
    if (buf) buf[n] = 0;
    return n;
}
__attribute__((ms_abi)) int mw_UrlIsW(const uint16_t* url, uint32_t scheme) {
    (void)url; (void)scheme;
    return 0;
}
__attribute__((ms_abi)) int mw_PathIsNetworkPathW(const uint16_t* p) { (void)p; return 0; }

/* ---- SW-1128 CRT (msvcrt) tails: real ---- */
__attribute__((ms_abi)) int mw__strcmpi(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
__attribute__((ms_abi)) int mw__mbscmp(const uint8_t* a, const uint8_t* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)*a - (int)*b;
}
__attribute__((ms_abi)) long long mw__wtoi64(const uint16_t* s) {
    if (!s) return 0;
    while (*s == L' ' || *s == L'\t') s++;
    int neg = 0;
    if (*s == L'-') { neg = 1; s++; }
    else if (*s == L'+') s++;
    long long v = 0;
    while (*s >= L'0' && *s <= L'9') { v = v * 10 + (*s - L'0'); s++; }
    return neg ? -v : v;
}
__attribute__((ms_abi)) int mw__wtoi(const uint16_t* s) {
    return (int)mw__wtoi64(s);
}
__attribute__((ms_abi)) uint16_t* mw__wcslwr(uint16_t* s) {
    if (!s) return s;
    for (uint16_t* p = s; *p; p++)
        if (*p >= L'A' && *p <= L'Z') *p = (uint16_t)(*p + 32);
    return s;
}
__attribute__((ms_abi)) uint16_t* mw__itow(int v, uint16_t* buf, int base) {
    char tmp[40];
    if (base == 16) snprintf(tmp, sizeof(tmp), "%x", v);
    else if (base == 8) snprintf(tmp, sizeof(tmp), "%o", v);
    else snprintf(tmp, sizeof(tmp), "%d", v);
    int n = 0;
    for (; tmp[n]; n++) buf[n] = (uint16_t)(unsigned char)tmp[n];
    buf[n] = 0;
    return buf;
}
__attribute__((ms_abi)) uint16_t* mw__ui64tow(unsigned long long v, uint16_t* buf, int base) {
    char tmp[80];
    if (base == 16) snprintf(tmp, sizeof(tmp), "%llx", v);
    else if (base == 8) snprintf(tmp, sizeof(tmp), "%llo", v);
    else snprintf(tmp, sizeof(tmp), "%llu", v);
    int n = 0;
    for (; tmp[n]; n++) buf[n] = (uint16_t)(unsigned char)tmp[n];
    buf[n] = 0;
    return buf;
}
__attribute__((ms_abi)) size_t mw__msize(void* p) {
    return p ? malloc_usable_size(p) : 0;
}
__attribute__((ms_abi)) uint16_t* mw_wcsncat(uint16_t* dst, const uint16_t* src, size_t n) {
    if (!dst || !src) return dst;
    uint16_t* d = dst;
    while (*d) d++;
    size_t i = 0;
    while (i < n && src[i]) { d[i] = src[i]; i++; }
    d[i] = 0;
    return dst;
}

/* ---- SW-1128 shell/ole tails ---- */
__attribute__((ms_abi)) uint32_t mw_WNetGetUniversalNameW(const uint16_t* path, uint32_t level,
        void* buf, uint32_t* size) {
    (void)path; (void)level; (void)buf;
    if (size) *size = 0;
    mw_SetLastError(2250); /* ERROR_NOT_CONNECTED */
    return 2250;
}
__attribute__((ms_abi)) int mw_SHFileOperationW(void* op) {
    MW_TRACE("SHFileOperationW -> 1 (typed fail: no shell FS ops v1)");
    return 1;
}
__attribute__((ms_abi)) int32_t mw_SHBindToParent(const void* pidl, const void* iid, void** ppv, void** last) {
    (void)pidl; (void)iid;
    if (ppv) *ppv = NULL;
    if (last) *last = NULL;
    return (int32_t)0x80004002LL; /* E_NOINTERFACE */
}
__attribute__((ms_abi)) int32_t mw_StgCreateDocfileOnILockBytes(void* lk, uint32_t mode, uint32_t res, void** stg) {
    (void)lk; (void)mode; (void)res;
    if (stg) *stg = NULL;
    return (int32_t)0x80030001LL; /* STG_E_INVALIDFLAG */
}
__attribute__((ms_abi)) int32_t mw_StringFromCLSID(const void* guid, uint16_t** out) {
    if (!guid || !out) return (int32_t)0x80070057LL; /* E_INVALIDARG */
    const uint8_t* g = (const uint8_t*)guid;
    uint32_t d1; uint16_t d2, d3;
    memcpy(&d1, g, 4); memcpy(&d2, g + 4, 2); memcpy(&d3, g + 6, 2);
    char tmp[64];
    snprintf(tmp, sizeof(tmp),
        "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        d1, d2, d3, g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    uint16_t* s = (uint16_t*)mw_CoTaskMemAlloc(80);
    if (!s) return (int32_t)0x8007000ELL; /* E_OUTOFMEMORY */
    int n = 0;
    for (; tmp[n]; n++) s[n] = (uint16_t)(unsigned char)tmp[n];
    s[n] = 0;
    *out = s;
    return 0; /* S_OK */
}
__attribute__((ms_abi)) int32_t mw_CoLockObjectExternal(void* unk, int lock, int last) {
    (void)unk; (void)lock; (void)last;
    return 0; /* S_OK */
}
__attribute__((ms_abi)) int32_t mw_CoDisconnectObject(void* unk, uint32_t flags) {
    (void)unk; (void)flags;
    return 0; /* S_OK */
}
__attribute__((ms_abi)) int32_t mw_CoGetObject(const uint16_t* name, void* opts, const void* iid, void** ppv) {
    (void)name; (void)opts; (void)iid;
    if (ppv) *ppv = NULL;
    return (int32_t)0x80004002LL; /* E_NOINTERFACE */
}
__attribute__((ms_abi)) int32_t mw_CoCreateFreeThreadedMarshaler(void* unk, void** ppv) {
    (void)unk;
    if (ppv) *ppv = NULL;
    return (int32_t)0x80004002LL;
}
__attribute__((ms_abi)) int32_t mw_CoRevokeClassObject(uint32_t reg) { (void)reg; return 0; }
__attribute__((ms_abi)) int mw_CreateProcessAsUserW(void* tok, const uint16_t* app, const uint16_t* cmd,
        void* pa, void* ta, int inherit, uint32_t flags, void* env, const uint16_t* dir,
        const void* si, void* pi) {
    (void)tok; (void)app; (void)cmd; (void)pa; (void)ta; (void)inherit;
    (void)flags; (void)env; (void)dir; (void)si; (void)pi;
    mw_SetLastError(5);
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_DocumentPropertiesW(void* hwnd, void* printer, const uint16_t* name,
        void* devmode, void* dmode, uint32_t mode) {
    (void)hwnd; (void)printer; (void)name; (void)dmode; (void)mode;
    if (devmode) memset(devmode, 0, 220);
    return 220; /* sizeof(DEVMODEW): bytes needed */
}

/* ---- SW-1128 GDI+ tails: no codecs, typed failures ---- */
__attribute__((ms_abi)) int mw_GdipGetImageEncodersSize(uint32_t* n, uint32_t* size) {
    if (n) *n = 0;
    if (size) *size = 0;
    return 0; /* Ok: zero codecs installed */
}
__attribute__((ms_abi)) int mw_GdipGetImageGraphicsContext(void* img, void** gfx) {
    (void)img;
    if (gfx) *gfx = NULL;
    return 1; /* GenericError */
}
__attribute__((ms_abi)) int mw_GdipGetImagePalette(void* img, void* pal, int size) {
    (void)img; (void)pal; (void)size;
    return 1;
}
__attribute__((ms_abi)) int mw_GdipGetImagePaletteSize(void* img, int* size) {
    (void)img;
    if (size) *size = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_GdipCloneImage(void* img, void** out) { (void)img; if (out) *out = NULL; return 1; }
__attribute__((ms_abi)) int mw_GdipDrawImageI(void* gfx, void* img, int x, int y) {
    (void)gfx; (void)img; (void)x; (void)y;
    return 1;
}
__attribute__((ms_abi)) int mw_GdipDrawImageRectI(void* gfx, void* img, int x, int y, int w, int h) {
    (void)gfx; (void)img; (void)x; (void)y; (void)w; (void)h;
    return 1;
}
__attribute__((ms_abi)) int mw_GdipDrawImageRectRectI(void* gfx, void* img, int dx, int dy, int dw, int dh,
        int sx, int sy, int sw, int sh, int unit, const void* attrs, void* cb, void* cbd) {
    (void)gfx; (void)img; (void)dx; (void)dy; (void)dw; (void)dh;
    (void)sx; (void)sy; (void)sw; (void)sh; (void)unit; (void)attrs; (void)cb; (void)cbd;
    return 1;
}
__attribute__((ms_abi)) int mw_GdipSaveImageToStream(void* img, void* stream, const void* clsid, const void* params) {
    (void)img; (void)stream; (void)clsid; (void)params;
    return 1;
}

/* ---- SW-1128 SNMP/NetApi (bginfo64 tail): real tiny ---- */
__attribute__((ms_abi)) int mw_SnmpUtilOidNCmp(const void* a, const void* b, uint32_t n) {
    /* AsnObjectIdentifier = { UINT idLength; UINT* ids; } */
    const uint32_t* A = (const uint32_t*)a;
    const uint32_t* B = (const uint32_t*)b;
    uint32_t la = A[0], lb = B[0];
    uint32_t* ia = (uint32_t*)A[1];
    uint32_t* ib = (uint32_t*)B[1];
    uint32_t m = n;
    if (la < m) m = la;
    if (lb < m) m = lb;
    for (uint32_t i = 0; i < m; i++)
        if (ia[i] != ib[i]) return ia[i] < ib[i] ? -1 : 1;
    if (la == lb) return 0;
    return la < lb ? -1 : 1;
}
__attribute__((ms_abi)) int mw_SnmpUtilOidCpy(void* dst, const void* src) {
    if (!dst || !src) return 1; /* SNMPAPI_ERROR */
    uint32_t* D = (uint32_t*)dst;
    const uint32_t* S = (const uint32_t*)src;
    D[0] = S[0];
    D[1] = S[1]; /* shared ids: read-only consumers */
    return 0;    /* SNMPAPI_NOERROR */
}
__attribute__((ms_abi)) uint32_t mw_SnmpSvcGetUptime(void) { return 0; }
__attribute__((ms_abi)) int32_t mw_NetApiBufferFree(void* buf) { (void)buf; return 0; }

/* ---- SW-1128 uxtheme: themes unavailable -> classic fallback path ---- */
__attribute__((ms_abi)) void* mw_OpenThemeData(void* hwnd, const uint16_t* classlist) {
    (void)hwnd; (void)classlist;
    MW_TRACE("OpenThemeData -> NULL (no theme engine; caller uses classic metrics)");
    return NULL;
}
__attribute__((ms_abi)) void* mw_OpenThemeDataEx(void* hwnd, const uint16_t* cls, uint32_t flags) {
    (void)hwnd; (void)cls; (void)flags;
    return NULL;
}
__attribute__((ms_abi)) int32_t mw_CloseThemeData(void* h) { (void)h; return 0; /* S_OK */ }
/* mw_IsAppThemed/mw_IsThemeActive/mw_SetWindowTheme: already exist upstream */
__attribute__((ms_abi)) int32_t mw_GetCurrentThemeName(uint16_t* a, int ca, uint16_t* b, int cb, uint16_t* c, int cc) {
    (void)a; (void)ca; (void)b; (void)cb; (void)c; (void)cc;
    return (int32_t)0x80004005LL; /* E_FAIL: no theme */
}
__attribute__((ms_abi)) void* mw_GetWindowTheme(void* h) { (void)h; return NULL; }
__attribute__((ms_abi)) int32_t mw_DrawThemeBackground(void* h, void* dc, int part, int state, const void* rect, const void* clip) {
    (void)h; (void)dc; (void)part; (void)state; (void)rect; (void)clip;
    return (int32_t)0x80004001LL; /* E_NOTIMPL */
}
__attribute__((ms_abi)) int32_t mw_DrawThemeText(void* h, void* dc, int part, int state, const uint16_t* text,
        int cch, uint32_t flags, uint32_t flags2, const void* rect) {
    (void)h; (void)dc; (void)part; (void)state; (void)text; (void)cch; (void)flags; (void)flags2; (void)rect;
    return (int32_t)0x80004001LL;
}
__attribute__((ms_abi)) int32_t mw_DrawThemeParentBackground(void* hwnd, void* dc, const void* clip) {
    (void)hwnd; (void)dc; (void)clip;
    return (int32_t)0x80004001LL;
}
__attribute__((ms_abi)) int32_t mw_GetThemePartSize(void* h, void* dc, int part, int state, const void* rect, int eSize, void* size) {
    (void)h; (void)dc; (void)part; (void)state; (void)rect; (void)eSize;
    if (size) { int32_t* s = (int32_t*)size; s[0] = 0; s[1] = 0; }
    return (int32_t)0x80004001LL;
}
__attribute__((ms_abi)) int32_t mw_GetThemeMargins(void* h, void* dc, int part, int state, int prop, const void* rect, void* margins) {
    (void)h; (void)dc; (void)part; (void)state; (void)prop; (void)rect;
    if (margins) memset(margins, 0, 24);
    return (int32_t)0x80004001LL;
}
__attribute__((ms_abi)) int32_t mw_GetThemeInt(void* h, int part, int state, int prop, int* val) {
    (void)h; (void)part; (void)state; (void)prop;
    if (val) *val = 0;
    return (int32_t)0x80004001LL;
}
__attribute__((ms_abi)) int32_t mw_GetThemeColor(void* h, int part, int state, int prop, uint32_t* color) {
    (void)h; (void)part; (void)state; (void)prop;
    if (color) *color = 0;
    return (int32_t)0x80004001LL;
}
__attribute__((ms_abi)) int32_t mw_GetThemeFont(void* h, void* dc, int part, int state, int prop, void* font) {
    (void)h; (void)dc; (void)part; (void)state; (void)prop;
    if (font) memset(font, 0, 92);
    return (int32_t)0x80004001LL;
}
__attribute__((ms_abi)) uint32_t mw_GetThemeSysColor(void* h, int id) { (void)h; (void)id; return 0; }
__attribute__((ms_abi)) int32_t mw_GetThemeTextExtent(void* h, void* dc, int part, int state,
        const uint16_t* text, int cch, uint32_t flags, const void* bounds, const void* font, void* rect) {
    (void)h; (void)dc; (void)part; (void)state; (void)text; (void)cch; (void)flags; (void)bounds; (void)font;
    if (rect) memcpy(rect, bounds ? bounds : rect, 16);
    return (int32_t)0x80004001LL;
}
__attribute__((ms_abi)) int mw_IsThemeBackgroundPartiallyTransparent(void* h, int part, int state) {
    (void)h; (void)part; (void)state;
    return 0;
}
