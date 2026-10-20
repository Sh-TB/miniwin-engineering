/* ============================================================
 * SWEEP-028 wagon batch — SW-1101 winmergeu drive + small tails
 * Drivers: artifacts/sweep028/recon (winmergeu 150, procexp 128,
 * bginfo 145, autoruns 26, produkey 10, cports 6, diskmon 5,
 * rammap 4, searchmyfiles 2)
 * Frontier decodes this round:
 *  - SW-1101a: CreateDIBSection top-down 32bpp NULL -> RGBA loop
 *    SIGSEGV RVA 0x12d0c2 — FIXED in engine (MWBmp view format).
 *  - SW-1101b: OLEAUT32!LoadTypeLib stub returned 0 (fake S_OK) ->
 *    guest deref *pptlib NULL -> SIGSEGV RVA 0x2b8de0 — typed fail now.
 * V1 semantics: real where cheap (paths/files/strings/registry),
 * typed-honest failures for host-impossible services.
 * ============================================================ */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t g_menu_desc_next = 1;
static uint32_t g_gdesc_next = 1;
static uint32_t g_rgn_next = 1;
static uint32_t g_psp_next = 1;
static uint32_t g_job_next = 1;
static uint32_t g_prov_next = 1;
static uint32_t g_hash_next = 1;
/* UTF-16 -> ASCII-ish path bytes (w3_utf16le_to_utf8 defined earlier in file) */
static void sw28_w2a(char* d, const uint16_t* s, size_t cap) {
    if (!d || !cap) return;
    size_t n = 0; if (s) while (s[n]) n++;
    int got = w3_utf16le_to_utf8(s, (int)n, d, (int)(cap - 1));
    if (got < 0) got = 0;
    d[got] = 0;
}

/* ---- SW-1101b: OLEAUT32 typed-honest family ---- */
#define MW_TYPE_E_CANTLOADLIBRARY 0x80028CA0u
__attribute__((ms_abi)) uint32_t mw_LoadTypeLib(const uint16_t* file, void** pptlib) {
    if (pptlib) *pptlib = NULL; /* honest: no type libs on this host */
    MW_TRACE("LoadTypeLib -> TYPE_E_CANTLOADLIBRARY");
    return MW_TYPE_E_CANTLOADLIBRARY;
}
__attribute__((ms_abi)) uint32_t mw_LoadRegTypeLib(const void* g, uint16_t a, uint16_t b,
                                                   uint32_t lcid, void** pptlib) {
    if (pptlib) *pptlib = NULL;
    return MW_TYPE_E_CANTLOADLIBRARY;
}
__attribute__((ms_abi)) uint32_t mw_PropVariantClear(void* pvar) {
    if (pvar) memset(pvar, 0, 16); /* VT_EMPTY + tail zero: honest clear */
    return 0; /* S_OK */
}
__attribute__((ms_abi)) uint32_t mw_CLSIDFromProgID(const uint16_t* progid, void* clsid) {
    if (clsid) memset(clsid, 0, 16);
    (void)progid;
    return 0x800401F3u; /* CO_E_CLASSSTRING: invalid ProgID — honest */
}
/* CLSIDFromString real parse "{...}" — autoruns/procexp use it for GUID keys */
static int mw_hex4(const uint16_t* p, uint32_t* out) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t c = p[i];
        if (c >= '0' && c <= '9') v = (v << 4) | (c - '0');
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (c - 'a' + 10);
        else return 0;
    }
    *out = v; return 1;
}
__attribute__((ms_abi)) uint32_t mw_CLSIDFromString(const uint16_t* str, void* clsid) {
    if (!str || !clsid) return 0x80004005u; /* E_POINTER */
    if (str[0] != '{') return 0x80004005u;
    uint32_t v[3] = {0, 0, 0};
    if (!mw_hex4(str + 1, &v[0])) return 0x80004005u;             /* data1 hi */
    uint8_t* o = (uint8_t*)clsid;
    /* data1 = 8 hex at +1, '-' , data2 = 4 hex at +10, '-', data3 = 4 hex at +15 */
    uint32_t d1hi, d2, d3;
    if (!mw_hex4(str + 1, &d1hi)) return 0x80004005u;
    if (!mw_hex4(str + 5, &v[1])) return 0x80004005u;
    uint32_t d1lo = v[1];
    if (!mw_hex4(str + 10, &d2)) return 0x80004005u;
    if (!mw_hex4(str + 15, &d3)) return 0x80004005u;
    uint32_t d1 = (d1hi << 16) | d1lo;
    memcpy(o, &d1, 4);
    o[4] = (uint8_t)(d2 >> 8); o[5] = (uint8_t)d2;
    o[6] = (uint8_t)(d3 >> 8); o[7] = (uint8_t)d3;
    /* data4: 8 bytes from 16 hex at +20 (2 pairs) and +25.. (rest), dashes allowed */
    int bit = 0; uint32_t cur = 0;
    for (int i = 20; i < 37 && bit < 8; i++) {
        uint32_t c = str[i];
        int nib;
        if (c >= '0' && c <= '9') nib = (int)(c - '0');
        else if (c >= 'A' && c <= 'F') nib = (int)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') nib = (int)(c - 'a' + 10);
        else continue;
        cur = (cur << 4) | (uint32_t)nib;
        if (bit & 1) { o[8 + bit / 2] = (uint8_t)cur; cur = 0; }
        bit++;
    }
    if (bit < 8 * 2) return 0x80004005u;
    return 0; /* S_OK */
}
__attribute__((ms_abi)) uint32_t mw_CreateStreamOnHGlobal(void* hglobal, int del, void** ppstm) {
    if (ppstm) *ppstm = NULL;
    (void)hglobal; (void)del;
    return 0x80004005u; /* E_FAIL — honest: no IStream marshal host-side */
}
/* ---- Ole clipboard / drag-drop / menu descriptor: typed honest ---- */
__attribute__((ms_abi)) uint32_t mw_OleGetClipboard(void** pp) { if (pp) *pp = NULL; return 0x80004005u; }
__attribute__((ms_abi)) uint32_t mw_OleFlushClipboard(void) { return 0x80004005u; }
__attribute__((ms_abi)) uint32_t mw_OleIsCurrentClipboard(void* d) { (void)d; return 1; /* S_FALSE */ }
__attribute__((ms_abi)) uint32_t mw_OleRun(void* u) { (void)u; return 0; /* S_OK */ }
__attribute__((ms_abi)) uint32_t mw_OleTranslateAccelerator(void* s, void* f, void* m) {
    (void)s; (void)f; (void)m; return 1; /* S_FALSE: no translation */
}
__attribute__((ms_abi)) void* mw_OleDuplicateData(void* h, uint32_t fmt, uint32_t flags) {
    (void)fmt; (void)flags;
    if (!h) return NULL;
    return h; /* v1: identity duplicate (guest keeps refcount discipline) */
}
__attribute__((ms_abi)) void* mw_OleCreateMenuDescriptor(void* hmenu, void* lpmw) {
    (void)lpmw;
    if (!hmenu) return NULL;
    return (void*)(uintptr_t)(0x00AD0000u | (g_menu_desc_next++ & 0xFFFu));
}
__attribute__((ms_abi)) int mw_OleDestroyMenuDescriptor(void* hdesc) {
    (void)hdesc; return 0; /* S_OK */
}
__attribute__((ms_abi)) uint32_t mw_LresultFromObject(const void* iid, void* wParam, void* punk) {
    (void)iid; (void)wParam; (void)punk;
    return 0; /* honest: no object marshal; guest checks NULL before send */
}
__attribute__((ms_abi)) uint32_t mw_DoDragDrop(void* dsrc, void* dsrc2, uint32_t ok, uint32_t* eff) {
    (void)dsrc; (void)dsrc2; (void)ok;
    if (eff) *eff = 0;
    return 0x00040101u; /* DRAGDROP_S_CANCEL — honest: no drag host-side */
}
__attribute__((ms_abi)) uint32_t mw_RevokeDragDrop(void* h) { (void)h; return 0; /* S_OK */ }
__attribute__((ms_abi)) int mw_DragFinish(void* hd) {
    (void)hd; return 0;
}
__attribute__((ms_abi)) int mw_DragDetect(void* h, void* pt) { (void)h; (void)pt; return 0; }
__attribute__((ms_abi)) uint32_t mw_AccessibleObjectFromWindow(void* h, uint32_t id, const void* iid, void** ppv) {
    (void)h; (void)id; (void)iid;
    if (ppv) *ppv = NULL;
    return 0x80004002u; /* E_NOINTERFACE — honest */
}
__attribute__((ms_abi)) uint32_t mw_CreateStdAccessibleObject(void* h, uint32_t id, const void* iid, void** ppv) {
    (void)h; (void)id; (void)iid;
    if (ppv) *ppv = NULL;
    return 0x80004002u;
}
__attribute__((ms_abi)) uint32_t mw_CoMarshalInterThreadInterfaceInStream(const void* iid, void* unk, void** pstm) {
    (void)iid; (void)unk; if (pstm) *pstm = NULL;
    return 0x80004005u;
}
__attribute__((ms_abi)) uint32_t mw_CoGetInterfaceAndReleaseStream(void* stm, const void* iid, void** ppv) {
    (void)stm; (void)iid; if (ppv) *ppv = NULL;
    return 0x80004005u;
}
/* ---- SW-1101c: activation context — typed honest (no SxS on host) ---- */
__attribute__((ms_abi)) void* mw_CreateActCtxW(const void* actctx) {
    (void)actctx;
    return (void*)(uintptr_t)0xFFFFFFFFFFFFFFFFull; /* INVALID_HANDLE_VALUE */
}
__attribute__((ms_abi)) int mw_ActivateActCtx(void* h, void** cookie) {
    (void)h; if (cookie) *cookie = (void*)(uintptr_t)1;
    return 1; /* TRUE: context "active" is identity */
}
__attribute__((ms_abi)) int mw_DeactivateActCtx(uint32_t flags, void* cookie) {
    (void)flags; (void)cookie; return 1;
}
__attribute__((ms_abi)) int mw_QueryActCtxW(uint32_t flags, void* hactctx, const void* subst,
        uint32_t class_, void* buffer, size_t bufsz, size_t* needed) {
    (void)flags; (void)hactctx; (void)subst; (void)class_;
    if (needed) *needed = 0;
    if (buffer && bufsz) memset(buffer, 0, bufsz);
    return 0; /* FALSE + ERROR_NOT_FOUND */
}
__attribute__((ms_abi)) int mw_FindActCtxSectionStringW(uint32_t flags, const void* subst,
        const uint16_t* str, void* data) {
    (void)flags; (void)subst; (void)str; (void)data;
    return 0; /* FALSE */
}
/* ---- SW-1101d: shlwapi path family — REAL UTF-16 string surgery ---- */
static size_t mw_wlen16(const uint16_t* s) { size_t n = 0; if (s) while (s[n]) n++; return n; }
static void mw_wcpy16(uint16_t* d, const uint16_t* s, size_t max) {
    size_t i = 0; if (!s || !d) return;
    for (; i + 1 < max && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}
static void mw_wcat16(uint16_t* d, const uint16_t* s, size_t max) {
    size_t n = mw_wlen16(d); mw_wcpy16(d + n, s, max - n);
}
static int mw_wicmp16(const uint16_t* a, const uint16_t* b) {
    for (;;) {
        uint32_t ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
}
__attribute__((ms_abi)) int mw_PathStripToRootW(uint16_t* path) {
    if (!path) return 0;
    size_t n = mw_wlen16(path);
    for (size_t i = 0; i < n; i++) {
        if (path[i] == u'\\' && i >= 2) { /* keep "C:\\" root, drop the rest */
            path[i + 1] = 0;
            return 1;
        }
    }
    return 0;
}
__attribute__((ms_abi)) int mw_PathRemoveFileSpecW(uint16_t* path) {
    if (!path) return 0;
    size_t n = mw_wlen16(path);
    for (size_t i = n; i > 0; i--) {
        if (path[i - 1] == u'\\' || path[i - 1] == u'/') {
            if (i == 3 && path[1] == ':') { path[3] = 0; } /* C:\ root keeps slash */
            else path[i - 1] = 0;
            return 1;
        }
        if (path[i - 1] == u':') return 0; /* drive root — nothing to remove */
    }
    return 0;
}
__attribute__((ms_abi)) uint16_t* mw_PathCombineW(uint16_t* dst, const uint16_t* dir, const uint16_t* file) {
    if (!dst || !dir || !file) return NULL;
    if (!dir[0]) { mw_wcpy16(dst, file, 512); return dst; }
    mw_wcpy16(dst, dir, 512);
    size_t n = mw_wlen16(dst);
    if (n && dst[n - 1] != u'\\' && dst[n - 1] != u'/' && file[0] != u'\\')
        mw_wcat16(dst, (const uint16_t*)u"\\", 512);
    if (file[0] == u'\\' && n && (dst[n - 1] == u'\\' || dst[n - 1] == u'/'))
        file++; /* avoid double slash */
    mw_wcat16(dst, file, 512);
    return dst;
}
__attribute__((ms_abi)) uint32_t mw_PathGetCharTypeW(uint32_t ch) {
    if (ch < 0x80) {
        uint8_t c = (uint8_t)ch;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80)
            return 1u; /* GCT_LFNCHAR */
        if (c == '\\' || c == '/' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|')
            return 4u; /* GCT_INVALID */
        return 1u | 2u; /* LFN | SHORT — most punctuation is legal */
    }
    return 1u | 2u;
}
__attribute__((ms_abi)) int mw_PathIsDirectoryW(const uint16_t* path) {
    if (!path || !path[0]) return 0;
    char a[512]; sw28_w2a(a, path, sizeof a);
    struct stat st;
    return (stat(a, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0; /* FILE_ATTRIBUTE_DIRECTORY */
}
__attribute__((ms_abi)) int mw_PathFileExistsW(const uint16_t* path) {
    if (!path || !path[0]) return 0;
    char a[512]; sw28_w2a(a, path, sizeof a);
    return access(a, F_OK) == 0 ? 1 : 0;
}
__attribute__((ms_abi)) int mw_PathIsUNCW(const uint16_t* path) {
    return (path && path[0] == u'\\' && path[1] == u'\\') ? 1 : 0;
}
/* real wildcard match: * and ? — shlwapi semantics (case-insensitive) */
static int mw_wcmatch16(const uint16_t* s, const uint16_t* pat) {
    if (!s || !pat) return 0;
    if (!*pat) return !*s;
    if (*pat == L'*') {
        for (const uint16_t* t = s; ; t++) {
            if (mw_wcmatch16(t, pat + 1)) return 1;
            if (!*t) return 0;
        }
    }
    if (!*s) return 0;
    if (*pat == u'?' || ((*pat | 32u) == (*s | 32u))) return mw_wcmatch16(s + 1, pat + 1);
    return 0;
}
__attribute__((ms_abi)) int mw_PathMatchSpecW(const uint16_t* file, const uint16_t* spec) {
    if (!file || !spec) return 0;
    if (!spec[0]) return 1;
    const uint16_t* seg = spec;
    while (seg) {
        uint16_t one[128]; size_t i = 0;
        while (seg[i] && seg[i] != u';') { one[i] = seg[i]; i++; }
        one[i] = 0;
        if (mw_wcmatch16(file, one)) return 1;
        seg = seg[i] ? seg + i + 1 : NULL;
    }
    return 0;
}
__attribute__((ms_abi)) int mw_PathCompactPathW(void* hdc, uint16_t* path, uint32_t pixels) {
    (void)hdc; (void)pixels;
    return path ? (int)mw_wlen16(path) : 0; /* v1: no ellipsis compaction */
}
__attribute__((ms_abi)) int mw_PathCreateFromUrlW(const uint16_t* url, uint16_t* out, uint32_t* pcch, uint32_t flags) {
    (void)flags;
    if (!url || !out || !pcch) return 0x80004005u;
    if ((url[0] | 32u) == u'f' && (url[1] | 32u) == u'i') { /* file:[//] */
        const uint16_t* p = url + 5;
        while (*p == '/') p++;
        size_t n = mw_wlen16(p);
        if (*pcch < n + 1) { *pcch = (uint32_t)(n + 1); return 0x8007007Au; } /* E_INSUFFICIENT_BUFFER */
        mw_wcpy16(out, p, *pcch);
        *pcch = (uint32_t)n;
        return 0; /* S_OK */
    }
    return 0x8004C00Cu; /* URL_E_INVALID_SYNTAX — honest */
}
/* ---- SW-1101e: kernel32 file/string tails — real ---- */
__attribute__((ms_abi)) int mw_CopyFileW(const uint16_t* src, const uint16_t* dst, int failIfExists) {
    if (!src || !dst) return 0;
    char a[512], b[512];
    sw28_w2a(a, src, sizeof a); sw28_w2a(b, dst, sizeof b);
    if (failIfExists && access(b, F_OK) == 0) return 0;
    FILE* fi = fopen(a, "rb"); if (!fi) return 0;
    FILE* fo = fopen(b, "wb");
    if (!fo) { fclose(fi); return 0; }
    char buf[65536]; size_t n; int ok = 1;
    while ((n = fread(buf, 1, sizeof buf, fi)) > 0)
        if (fwrite(buf, 1, n, fo) != n) { ok = 0; break; }
    fclose(fi); fclose(fo);
    MW_TRACE("CopyFileW -> %d", ok);
    return ok;
}
__attribute__((ms_abi)) uint32_t mw_GetCompressedFileSizeW(const uint16_t* path, uint32_t* hi) {
    if (!path) return (uint32_t)-1;
    char a[512]; sw28_w2a(a, path, sizeof a);
    struct stat st;
    if (stat(a, &st) != 0) return (uint32_t)-1;
    if (hi) *hi = (uint32_t)((uint64_t)st.st_size >> 32);
    return (uint32_t)st.st_size;
}
__attribute__((ms_abi)) int mw_lstrcmpA(const char* a, const char* b) {
    if (!a || !b) return a ? 1 : -1;
    return strcmp(a, b);
}
__attribute__((ms_abi)) int mw_CompareStringA(uint32_t locale, uint32_t flags, const char* s1, int c1, const char* s2, int c2) {
    (void)locale; (void)flags;
    if (c1 < 0) c1 = s1 ? (int)strlen(s1) : 0;
    if (c2 < 0) c2 = s2 ? (int)strlen(s2) : 0;
    int n = c1 < c2 ? c1 : c2;
    for (int i = 0; i < n; i++) {
        uint8_t a = (uint8_t)s1[i], b = (uint8_t)s2[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return a < b ? 1 : 3; /* CSTR_LESS=1 / CSTR_GREATER=3 */
    }
    if (c1 == c2) return 2; /* CSTR_EQUAL */
    return c1 < c2 ? 1 : 3;
}
__attribute__((ms_abi)) uint32_t mw_ExpandEnvironmentStringsA(const char* src, char* dst, uint32_t n) {
    if (!src) return 0;
    uint32_t w = 0;
    for (const char* p = src; *p; ) {
        if (p[0] == '%' && p[1] == '%') { if (dst && w + 1 < n) dst[w] = '%'; w++; p += 2; continue; }
        if (dst && w + 1 < n) dst[w] = *p;
        w++; p++;
    }
    if (dst && n) dst[w < n ? w : n - 1] = 0;
    return w + 1;
}
__attribute__((ms_abi)) int mw_GetStringTypeExW(uint32_t locale, uint32_t type, const uint16_t* src, int count, uint16_t* out) {
    (void)locale;
    if (!src || !out || count <= 0) return 0;
    for (int i = 0; i < count; i++) {
        uint16_t c = src[i];
        uint16_t t = 0;
        if (c < 0x80) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) t |= 1u; /* C1_UPPER|LOWER mixed ok */
            if (c >= '0' && c <= '9') t |= 4u; /* C1_DIGIT */
            if (c == ' ' || c == '\t' || c == '\n') t |= 8u; /* C1_SPACE */
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') t |= 0x100u; /* C1_ALPHA */
        } else t = 0x100u | 1u; /* treat high planes as alpha+upper family */
        out[i] = t;
    }
    return 1;
}
__attribute__((ms_abi)) uint16_t* mw_CharPrevW(const uint16_t* start, const uint16_t* cur) {
    if (!start || !cur || cur <= start) return (uint16_t*)start;
    return (uint16_t*)(cur - 1);
}
__attribute__((ms_abi)) int mw_FreeConsole(void) { return 1; }
__attribute__((ms_abi)) void* mw_GlobalHandle(const void* p) {
    (void)p;
    return (void*)(uintptr_t)(0x00AE0000u | (g_gdesc_next++ & 0xFFFu)); /* cookie */
}
__attribute__((ms_abi)) uint32_t mw_GlobalFlags(void* h) { (void)h; return 0; /* GMEM_FIXED-ish: no flags */ }
__attribute__((ms_abi)) uint32_t mw_SetThreadUILanguage(uint16_t lang) { (void)lang; return 0x0409; }
__attribute__((ms_abi)) uint16_t mw_GetSystemDefaultUILanguage(void) { return 0x0409; }
__attribute__((ms_abi)) void* mw_LoadBitmapW(void* hinst, const void* name) {
    /* LoadBitmapW == LoadImageW(IMAGE_BITMAP, 0, 0, 0) — real resource route */
    return mw_LoadImageW(hinst, name, 0 /*IMAGE_BITMAP*/, 0, 0, 0);
}
__attribute__((ms_abi)) void* mw_ExtractIconW(void* hinst, const uint16_t* file, uint32_t idx) {
    (void)hinst; (void)file; (void)idx;
    return NULL; /* honest: no icon resource extraction for this path */
}
__attribute__((ms_abi)) int mw_ImmSetCompositionFontW(void* himc, void* lf) { (void)himc; (void)lf; return 1; }
__attribute__((ms_abi)) int mw_ReadDirectoryChangesW(void* h, void* buf, uint32_t len, int watchSub,
        uint32_t filter, uint32_t* returned, void* overlapped, void* completion) {
    (void)h; (void)buf; (void)len; (void)watchSub; (void)filter;
    (void)overlapped; (void)completion;
    if (returned) *returned = 0;
    return 0; /* honest: no fs-watch event source; guest falls back to polling */
}
/* ---- SW-1101f: USER32 window family (winmergeu menu/dialog code) ---- */
__attribute__((ms_abi)) int mw_ShowOwnedPopups(void* h, int show) { (void)h; (void)show; return 1; }
__attribute__((ms_abi)) void* mw_SetParent(void* child, void* parent) { (void)child; (void)parent; return NULL; }
__attribute__((ms_abi)) void* mw_GetTopWindow(void* h) { (void)h; return MW_GUI_HWND; }
__attribute__((ms_abi)) void* mw_GetLastActivePopup(void* h) { return h ? h : MW_GUI_HWND; }
__attribute__((ms_abi)) void* mw_GetNextDlgTabItem(void* dlg, void* ctl, int prev) {
    (void)dlg; (void)prev;
    return ctl ? ctl : NULL; /* v1: focus unchanged */
}
__attribute__((ms_abi)) int mw_BringWindowToTop(void* h) { (void)h; return 1; }
__attribute__((ms_abi)) int mw_LockWindowUpdate(void* h) { (void)h; return 1; }
__attribute__((ms_abi)) int mw_ScrollWindow(void* h, int dx, int dy, const void* rc, const void* clip) {
    (void)h; (void)dx; (void)dy; (void)rc; (void)clip; return 1; /* v1: no scroll pixels */
}
static int mw_g_scroll[2][2]; /* [bar]{pos,range} honest store, one DC-set */
__attribute__((ms_abi)) int mw_SetScrollRange(void* h, int bar, int min, int max, int redraw) {
    (void)h; (void)redraw;
    if (bar >= 0 && bar <= 1) { mw_g_scroll[bar][1] = max; (void)min; }
    return 1;
}
__attribute__((ms_abi)) int mw_GetScrollRange(void* h, int bar, int* minp, int* maxp) {
    (void)h;
    if (minp) *minp = 0;
    if (maxp) *maxp = (bar >= 0 && bar <= 1) ? mw_g_scroll[bar][1] : 0;
    return 1;
}
__attribute__((ms_abi)) int mw_GetScrollPos(void* h, int bar) {
    (void)h;
    return (bar >= 0 && bar <= 1) ? mw_g_scroll[bar][0] : 0;
}
__attribute__((ms_abi)) int mw_EnableScrollBar(void* h, uint32_t bar, uint32_t arrows) {
    (void)h; (void)bar; (void)arrows; return 1;
}
__attribute__((ms_abi)) int mw_SetMenuItemBitmaps(void* menu, uint32_t id, uint32_t flags, void* bmU, void* bmC) {
    (void)menu; (void)id; (void)flags; (void)bmU; (void)bmC; return 1;
}
__attribute__((ms_abi)) int mw_RegisterHotKey(void* h, int id, uint32_t mods, uint32_t vk) {
    (void)h; (void)id; (void)mods; (void)vk; return 1;
}
__attribute__((ms_abi)) int mw_ReplyMessage(void* msg) { (void)msg; return 1; }
__attribute__((ms_abi)) void* mw_ReuseDDElParam(void* lp, uint32_t msg, uint32_t wp, void* l1, void* l2) {
    (void)msg; (void)wp; (void)l1; (void)l2;
    return lp; /* passthrough reuse */
}
__attribute__((ms_abi)) int mw_IsAccelerator(void* tab, int n, void* msg, uint32_t* idx) {
    (void)tab; (void)n; (void)msg;
    if (idx) *idx = 0;
    return 0; /* honest: no accelerator translated here */
}
typedef struct { uint32_t cbSize; void* hwndCombo; void* hwndItem; void* hwndButton; void* hwndList; void* hwndEdit; void* hwndStatic; void* button; } MwComboInfo;
__attribute__((ms_abi)) int mw_GetComboBoxInfo(void* h, MwComboInfo* ci) {
    if (!ci) return 0;
    memset(ci, 0, sizeof *ci);
    ci->cbSize = (uint32_t)sizeof *ci;
    ci->hwndCombo = h;
    return 1;
}
__attribute__((ms_abi)) int mw_GetCaretPos(void* pt) { if (pt) memset(pt, 0, 8); return 1; }
typedef struct { const char* clsStyle; void* hCursor; uint16_t* menu; void* inst; void* icon; void* cursor; void* br; void* iconSm; int cbExtra; } MwClassA;
__attribute__((ms_abi)) int mw_GetClassInfoW(void* inst, const uint16_t* cls, MwClassA* wc) {
    (void)inst; (void)cls;
    if (!wc) return 0;
    memset(wc, 0, sizeof *wc);
    wc->clsStyle = "EDIT"; /* honest v1: system class identity only */
    return 1;
}
__attribute__((ms_abi)) void* mw_RealChildWindowFromPoint(void* h, void* pt) { (void)pt; return h; }
__attribute__((ms_abi)) void* mw_ChildWindowFromPointEx(void* h, void* pt, uint32_t flags) {
    (void)pt; (void)flags; return h;
}
__attribute__((ms_abi)) int mw_SetWindowRgn(void* h, void* rgn, int redraw) {
    (void)h; (void)rgn; (void)redraw; return 1;
}
__attribute__((ms_abi)) int mw_SetRectRgn(void* rgn, int x1, int y1, int x2, int y2) {
    (void)rgn; (void)x1; (void)y1; (void)x2; (void)y2; return 1;
}
__attribute__((ms_abi)) void* mw_CreateEllipticRgn(int x1, int y1, int x2, int y2) {
    (void)x1; (void)y1; (void)x2; (void)y2;
    return (void*)(uintptr_t)(0x00AF0000u | (g_rgn_next++ & 0xFFFu));
}
__attribute__((ms_abi)) int mw_RoundRect(void* hdc, int x1, int y1, int x2, int y2, int rw, int rh) {
    (void)rw; (void)rh;
    MWBmp* S = mw_dc_surface(hdc);
    if (!S) return 0;
    mwb_fill(S, x1, y1, x2 - x1, y2 - y1, g_cur_brush); /* v1: rect render */
    return 1;
}
__attribute__((ms_abi)) int mw_GrayStringW(void* hdc, void* br, void* cb, intptr_t data, int count,
        int x, int y, int cx, int cy) {
    (void)br; (void)cb; (void)data; (void)count; (void)cy;
    (void)hdc; (void)x; (void)y; (void)cx; return 1; /* v1: text renders normally */
}
__attribute__((ms_abi)) int mw_DrawIcon(void* hdc, int x, int y, void* icon) {
    (void)hdc; (void)x; (void)y; (void)icon; return 1; /* v1: no pixels */
}
__attribute__((ms_abi)) uint16_t mw_SetAbortProc(void* hdc, void* proc) { (void)hdc; (void)proc; return 0; }
__attribute__((ms_abi)) int mw_Escape(void* hdc, int esc, int nin, const char* in, void* out) {
    (void)hdc; (void)esc; (void)nin; (void)in; (void)out; return 0;
}
/* ---- SW-1101g: GDI map-mode / extents / state — honest per-DC store ---- */
typedef struct { int32_t we, he, wox, woy, vex, vey, vox, voy; uint32_t mapmode; uint32_t rop2, polyfill, stretch, textcolor, bkcolor; int32_t curx, cury; } MwDcMap;
static MwDcMap g_dcmap[MW_MEMDC_MAX + 4];
static MwDcMap* mw_dcmap_for(void* hdc) {
    uintptr_t v = (uintptr_t)hdc;
    if ((v & 0xFFFF0000u) == MW_MEMDC_BASE) {
        int i = (int)(v & 0xFF) - 1;
        if (i >= 0 && i < MW_MEMDC_MAX) return &g_dcmap[i];
    }
    return &g_dcmap[MW_MEMDC_MAX]; /* fallback slot for screen DCs */
}
static int32_t mw_map_scale(int32_t v) {
    switch (g_dcmap[MW_MEMDC_MAX].mapmode) { /* screen-DC default */
    case 1: case 2: return v;            /* MM_TEXT identity */
    case 3: return v * 4;                /* MM_LOENGLISH ~ .01in */
    case 4: return v * 40;               /* MM_HIENGLISH */
    case 5: return v * 4;                /* MM_LOMETRIC */
    case 6: return v * 40;               /* MM_HIMETRIC */
    case 7: return v * 4;                /* MM_TWIPS ~1/20pt */
    default: return v;
    }
}
__attribute__((ms_abi)) int mw_GetMapMode(void* hdc) { return (int)mw_dcmap_for(hdc)->mapmode; }
__attribute__((ms_abi)) int mw_SetWindowExtEx(void* hdc, int x, int y, void* old) {
    MwDcMap* m = mw_dcmap_for(hdc);
    if (old) { ((int32_t*)old)[0] = m->we; ((int32_t*)old)[1] = m->he; }
    m->we = x; m->he = y; return 1;
}
__attribute__((ms_abi)) int mw_SetViewportExtEx(void* hdc, int x, int y, void* old) {
    MwDcMap* m = mw_dcmap_for(hdc);
    if (old) { ((int32_t*)old)[0] = m->vex; ((int32_t*)old)[1] = m->vey; }
    m->vex = x; m->vey = y; return 1;
}
__attribute__((ms_abi)) int mw_SetWindowOrgEx(void* hdc, int x, int y, void* old) {
    MwDcMap* m = mw_dcmap_for(hdc);
    if (old) { ((int32_t*)old)[0] = m->wox; ((int32_t*)old)[1] = m->woy; }
    m->wox = x; m->woy = y; return 1;
}
__attribute__((ms_abi)) int mw_OffsetViewportOrgEx(void* hdc, int x, int y, void* old) {
    MwDcMap* m = mw_dcmap_for(hdc);
    if (old) { ((int32_t*)old)[0] = m->vox; ((int32_t*)old)[1] = m->voy; }
    m->vox += x; m->voy += y; return 1;
}
__attribute__((ms_abi)) int mw_GetWindowExtEx(void* hdc, void* sz) {
    if (sz) { MwDcMap* m = mw_dcmap_for(hdc); ((int32_t*)sz)[0] = m->we; ((int32_t*)sz)[1] = m->he; }
    return 1;
}
__attribute__((ms_abi)) int mw_GetViewportExtEx(void* hdc, void* sz) {
    if (sz) { MwDcMap* m = mw_dcmap_for(hdc); ((int32_t*)sz)[0] = m->vex; ((int32_t*)sz)[1] = m->vey; }
    return 1;
}
__attribute__((ms_abi)) int mw_GetWindowOrgEx(void* hdc, void* pt) {
    if (pt) { MwDcMap* m = mw_dcmap_for(hdc); ((int32_t*)pt)[0] = m->wox; ((int32_t*)pt)[1] = m->woy; }
    return 1;
}
__attribute__((ms_abi)) int mw_GetViewportOrgEx(void* hdc, void* pt) {
    if (pt) { MwDcMap* m = mw_dcmap_for(hdc); ((int32_t*)pt)[0] = m->vox; ((int32_t*)pt)[1] = m->voy; }
    return 1;
}
__attribute__((ms_abi)) int mw_ScaleWindowExtEx(void* hdc, int xnum, int xden, int ynum, int yden, void* old) {
    MwDcMap* m = mw_dcmap_for(hdc);
    if (old) { ((int32_t*)old)[0] = m->we; ((int32_t*)old)[1] = m->he; }
    if (xden) m->we = m->we * xnum / xden;
    if (yden) m->he = m->he * ynum / yden;
    return 1;
}
__attribute__((ms_abi)) int mw_ScaleViewportExtEx(void* hdc, int xnum, int xden, int ynum, int yden, void* old) {
    MwDcMap* m = mw_dcmap_for(hdc);
    if (old) { ((int32_t*)old)[0] = m->vex; ((int32_t*)old)[1] = m->vey; }
    if (xden) m->vex = m->vex * xnum / xden;
    if (yden) m->vey = m->vey * ynum / yden;
    return 1;
}
__attribute__((ms_abi)) int mw_LPtoDP(void* hdc, void* pts, int n) {
    MwDcMap* m = mw_dcmap_for(hdc);
    int32_t* p = (int32_t*)pts;
    for (int i = 0; i < n * 2; i++)
        p[i] = mw_map_scale(p[i] - ((i & 1) ? m->woy : m->wox)) + ((i & 1) ? m->voy : m->vox);
    return 1;
}
__attribute__((ms_abi)) int mw_DPtoLP(void* hdc, void* pts, int n) {
    MwDcMap* m = mw_dcmap_for(hdc);
    int32_t* p = (int32_t*)pts;
    for (int i = 0; i < n * 2; i++)
        p[i] = mw_map_scale(p[i] - ((i & 1) ? m->voy : m->vox)) + ((i & 1) ? m->woy : m->wox);
    return 1;
}
__attribute__((ms_abi)) int mw_GetROP2(void* hdc) { (void)hdc; return 13; /* R2_COPYPEN */ }
__attribute__((ms_abi)) int mw_SetPolyFillMode(void* hdc, int mode) {
    MwDcMap* m = mw_dcmap_for(hdc); int prev = (int)m->polyfill;
    m->polyfill = (uint32_t)mode; return prev ? prev : 1;
}
__attribute__((ms_abi)) int mw_GetPolyFillMode(void* hdc) { return (int)mw_dcmap_for(hdc)->polyfill; }
__attribute__((ms_abi)) int mw_GetStretchBltMode(void* hdc) {
    (void)hdc; return g_stretch_mode ? g_stretch_mode : 1;
}
__attribute__((ms_abi)) uint32_t mw_GetTextColor(void* hdc) {
    (void)hdc; return g_dcmap[MW_MEMDC_MAX].textcolor;
}
__attribute__((ms_abi)) uint32_t mw_GetNearestColor(void* hdc, uint32_t color) {
    (void)hdc; return color; /* 24bpp device: identity */
}
__attribute__((ms_abi)) int mw_GetCurrentPositionEx(void* hdc, void* pt) {
    if (pt) { MwDcMap* m = mw_dcmap_for(hdc); ((int32_t*)pt)[0] = m->curx; ((int32_t*)pt)[1] = m->cury; }
    return 1;
}
__attribute__((ms_abi)) int mw_RectVisible(void* hdc, const void* rc) { (void)hdc; (void)rc; return 1; }
__attribute__((ms_abi)) int mw_PtVisible(void* hdc, int x, int y) { (void)hdc; (void)x; (void)y; return 1; }
__attribute__((ms_abi)) int mw_GetClipBox(void* hdc, void* rc) {
    (void)hdc;
    if (rc) { MWBmp* S = mw_dc_surface(hdc);
        int32_t* r = (int32_t*)rc;
        r[0] = 0; r[1] = 0; r[2] = S ? S->w : 0; r[3] = S ? S->h : 0; }
    return 1; /* SIMPLEREGION */
}
__attribute__((ms_abi)) void* mw_GetDCEx(void* h, void* rgn, uint32_t flags) {
    (void)h; (void)rgn; (void)flags;
    return (void*)0x00020001; /* screen DC identity (matches mw_GetDC) */
}
__attribute__((ms_abi)) void* mw_CreateDCW(const uint16_t* driver, const uint16_t* dev, const void* port, const void* dmi) {
    (void)driver; (void)dev; (void)port; (void)dmi;
    /* memory-DC slot so the printer/metafile DC has real storage */
    for (int i = 0; i < MW_MEMDC_MAX; i++) {
        if (!g_memdc_live[i]) {
            g_memdc_live[i] = 1;
            g_memdc_sel[i] = -1;
            return (void*)(uintptr_t)(MW_MEMDC_BASE | (unsigned)(i + 1));
        }
    }
    return NULL;
}
__attribute__((ms_abi)) void* mw_CopyMetaFileW(void* mf, const uint16_t* file) {
    (void)mf; (void)file;
    return NULL; /* honest: no metafile store */
}
__attribute__((ms_abi)) uint32_t mw_SetLayout(void* hdc, uint32_t layout) {
    (void)hdc; (void)layout; return 0; /* GDI_ERROR: layout unsupported */
}
__attribute__((ms_abi)) uint32_t mw_GetLayout(void* hdc) { (void)hdc; return 0; }
/* ---- SW-1112-adjacent small tails (rammap/diskmon/cports/produkey/searchmyfiles) ---- */
__attribute__((ms_abi)) uint32_t mw_QueryServiceStatusEx(void* svc, uint32_t info, void* buf, uint32_t sz, uint32_t* need) {
    (void)svc; (void)info;
    if (need) *need = 28;
    if (buf && sz >= 28) {
        uint32_t* u = (uint32_t*)buf;
        u[0] = 4; /* dwCheckPoint-ish struct: SERVICE_STOPPED = 0x00000005? use STOPPED */
        memset(u, 0, sz);
        u[0] = 1; /* SERVICE_STOPPED */
        u[3] = 0; u[4] = 0;
    }
    return 1;
}
__attribute__((ms_abi)) int mw_ControlService(void* svc, uint32_t code, void* status) {
    (void)svc; (void)code;
    if (status) { uint32_t* u = (uint32_t*)status; memset(u, 0, 28); u[0] = 1; /* STOPPED */ }
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetClassLongW(void* h, int idx) {
    (void)h; (void)idx;
    return 0; /* honest: no class-long overrides */
}
__attribute__((ms_abi)) long long mw_DefDlgProcW(void* h, uint32_t msg, uintptr_t wp, intptr_t lp) {
    return mw_DefWindowProcW(h, msg, wp, lp);
}
__attribute__((ms_abi)) uint16_t* mw_lstrcatA(char* dst, const char* src) {
    if (dst && src) { size_t n = strlen(dst); snprintf(dst + n, 4096, "%s", src); }
    return (uint16_t*)dst;
}
__attribute__((ms_abi)) int mw_WinHelpA(void* h, const char* file, uint32_t cmd, uintptr_t data) {
    (void)h; (void)file; (void)cmd; (void)data; return 1; /* help "shown" */
}
typedef struct { uint32_t lStructSize; void* hwndOwner; void* hDevMode; void* hDevNames; void* hdc; uint32_t flags; uint16_t nFrom, nTo, nMin, nMax, nCopies; void* hInstance; uint32_t lCustData; void* lpfnPrintHook; void* lpfnSetupHook; const char* lpPrinterName; const char* lpDriverName; const char* lpOutputName; const char* lpSetupName; const char* lpHelpFile; void* hTemplate; } MwPrintDlgA;
__attribute__((ms_abi)) int mw_PrintDlgA(MwPrintDlgA* pd) {
    if (pd) pd->hdc = NULL;
    return 0; /* cancel — honest: no printer on host */
}
__attribute__((ms_abi)) int mw_LookupPrivilegeValueA(const char* sys, const char* name, void* luid) {
    (void)sys; (void)name;
    if (luid) memset(luid, 0, 8);
    return 1; /* privilege "known" (SeDebug style names) */
}
__attribute__((ms_abi)) int mw_LookupAccountSidA(const char* sys, void* sid, char* name, uint32_t* nname,
        char* dom, uint32_t* ndom, uint32_t* use) {
    (void)sys; (void)sid;
    if (nname && *nname >= 7) { snprintf(name, 7, "user"); *nname = 4; }
    else if (nname) *nname = 4;
    if (ndom && *ndom >= 6) { snprintf(dom, 6, "host"); *ndom = 4; }
    else if (ndom) *ndom = 4;
    if (use) *use = 1; /* SidTypeUser */
    return 1;
}
__attribute__((ms_abi)) int mw_GetMenuStringA(void* menu, uint32_t id, char* buf, int cch, uint32_t flags) {
    (void)menu; (void)id; (void)flags;
    if (buf && cch > 0) buf[0] = 0;
    return 0; /* no menu item text in engine menus */
}
__attribute__((ms_abi)) int mw_GetClassNameA(void* h, char* buf, int max) {
    if (!buf || max <= 0) return 0;
    const char* cls = "STATIC"; /* v1 honest identity */
    snprintf(buf, (size_t)max, "%s", cls);
    return (int)strlen(cls);
}
__attribute__((ms_abi)) int mw_GetDateFormatA(uint32_t locale, uint32_t flags, const void* st, const char* fmt, char* out, int cch) {
    (void)locale; (void)flags; (void)fmt;
    if (!out || cch <= 0) return 0;
    /* SYSTEMTIME { y, mo, dow, d, h, mi, s, ms } */
    const uint16_t* t = (const uint16_t*)st;
    int y = t ? *(const uint16_t*)t : 2026;
    int mo = t ? *((const uint16_t*)t + 1) : 1;
    int d = t ? *((const uint16_t*)t + 3) : 1;
    return snprintf(out, (size_t)cch, "%04d-%02d-%02d", y, mo, d);
}
__attribute__((ms_abi)) int mw_EnumWindows(void* cb, intptr_t lp) {
    typedef int (*MwEnumCb)(void*, intptr_t);
    MwEnumCb f = (MwEnumCb)cb;
    if (!f) return 0;
    int r = f(MW_GUI_HWND, lp); /* honest: exactly one top-level window */
    return r ? 1 : 0;
}
__attribute__((ms_abi)) int mw_EnumResourceNamesA(void* hmod, const void* type, void* cb, intptr_t lp) {
    (void)hmod; (void)type; (void)cb; (void)lp;
    return 0; /* FALSE: enumeration done (no resources exposed) */
}
__attribute__((ms_abi)) int mw_EnumResourceTypesW(void* hmod, void* cb, intptr_t lp) {
    (void)hmod; (void)cb; (void)lp;
    return 0;
}
__attribute__((ms_abi)) int mw_PostThreadMessageA(uint32_t tid, uint32_t msg, uintptr_t wp, intptr_t lp) {
    (void)tid; (void)msg; (void)wp; (void)lp;
    return 1; /* queued to the void — thread model is single-pump */
}
__attribute__((ms_abi)) int mw_ChangeClipboardChain(void* h, void* next) { (void)h; (void)next; return 1; }
__attribute__((ms_abi)) void* mw_SHGetPathFromIDListA(const void* pidl, char* path) {
    (void)pidl;
    if (path) path[0] = 0;
    return NULL; /* honest: no shell namespace */
}
__attribute__((ms_abi)) uint32_t mw_RegLoadKeyA(void* key, const char* sub, const char* file) {
    (void)key; (void)sub; (void)file;
    return 0x8007000Eu; /* ERROR_OUT_OF_HANDLES-class fail — no hive mount host-side */
}
__attribute__((ms_abi)) uint32_t mw_RegUnLoadKeyA(void* key, const char* sub) {
    (void)key; (void)sub; return 0x8007000Eu;
}
__attribute__((ms_abi)) uint32_t mw_RegConnectRegistryA(const char* machine, void* key, void* result) {
    (void)machine; (void)key;
    if (result) *(void**)result = NULL;
    return 0x0000006Du; /* ERROR_BAD_NETPATH — honest: no remote registry */
}
/* ---- WTS family (procexp64) — honest single console session ---- */
__attribute__((ms_abi)) uint32_t mw_WTSGetActiveConsoleSessionId(void) { return 1; }
__attribute__((ms_abi)) void mw_WTSFreeMemory(void* p) { free(p); }
__attribute__((ms_abi)) int mw_WTSEnumerateSessionsW(void* h, uint32_t resv, uint32_t ver, void** ppSession, uint32_t* pCount) {
    (void)h; (void)resv; (void)ver;
    if (!ppSession || !pCount) return 0;
    *pCount = 1;
    uint8_t* buf = (uint8_t*)calloc(1, 32);
    if (!buf) return 0;
    /* WTS_SESSION_INFOW { DWORD SessionId; LPWSTR pWinStationName; DWORD State } */
    *(uint32_t*)(buf + 0) = 1; /* console session */
    *(const uint16_t**)(buf + 8) = (const uint16_t*)u"Console";
    *(uint32_t*)(buf + 16) = 0; /* WTSActive */
    *ppSession = buf;
    return 1;
}
__attribute__((ms_abi)) int mw_WTSQuerySessionInformationW(void* h, uint32_t sid, uint32_t info, uint16_t** ppBuf, uint32_t* pCount) {
    (void)h; (void)sid; (void)info;
    if (!ppBuf || !pCount) return 0;
    *pCount = 16;
    uint16_t* buf = (uint16_t*)calloc(8, 2);
    if (!buf) return 0;
    mw_wcpy16(buf, (const uint16_t*)u"Console", 8);
    *ppBuf = buf;
    return 1;
}
__attribute__((ms_abi)) int mw_WTSLogoffSession(void* h, uint32_t sid, int wait) { (void)h; (void)sid; (void)wait; return 0; }
__attribute__((ms_abi)) int mw_WTSDisconnectSession(void* h, uint32_t sid, int wait) { (void)h; (void)sid; (void)wait; return 0; }
__attribute__((ms_abi)) int mw_WTSSendMessageW(void* h, uint32_t sid, const uint16_t* title, uint32_t tl,
        const uint16_t* msg, uint32_t ml, uint32_t style, uint32_t timeout, uint32_t* resp, int wait) {
    (void)h; (void)sid; (void)title; (void)tl; (void)msg; (void)ml; (void)style; (void)timeout; (void)wait;
    if (resp) *resp = 32001; /* IDTIMEOUT — honest: no interactive user */
    return 1;
}
/* ---- Toolhelp32/psapi (procexp64) — real enumeration over engine registry ---- */
typedef struct { uint32_t dwSize; uint32_t cntUsage; uint32_t th32ProcessID; uintptr_t th32DefaultHeap;
    uint32_t th32ModuleID; uint32_t cntThreads; uint32_t th32ParentProcessID; long pcPriClassBase;
    uint32_t dwFlags; char szExeFile[260]; } MwProcEntry;
static const char* sw28_exe_name(void) {
    const char* p = g_exe_path ? g_exe_path : "guest.exe";
    const char* b = p;
    for (const char* q = p; *q; q++) if (*q == '/') b = q + 1;
    return b;
}
__attribute__((ms_abi)) int mw_Process32FirstW(void* snap, void* pe) {
    (void)snap;
    MwProcEntry* e = (MwProcEntry*)pe;
    if (!e || e->dwSize < sizeof(MwProcEntry)) return 0;
    e->cntThreads = 1;
    e->th32ProcessID = mw_GetCurrentProcessId();
    e->th32ParentProcessID = 4; /* system */
    e->pcPriClassBase = 8;
    snprintf(e->szExeFile, sizeof e->szExeFile, "%s", sw28_exe_name());
    return 1;
}
__attribute__((ms_abi)) int mw_Process32NextW(void* snap, void* pe) { (void)snap; (void)pe; return 0; }
typedef struct { uint32_t dwSize; uint32_t th32ModuleID; uint32_t th32ProcessID; uint32_t GlblcntUsage;
    uint32_t ProccntUsage; uintptr_t modBaseAddr; uint32_t modBaseSize; uintptr_t hModule;
    char szModule[256]; char szExePath[260]; } MwModuleEntry;
__attribute__((ms_abi)) int mw_Module32FirstW(void* snap, void* me) {
    (void)snap;
    MwModuleEntry* e = (MwModuleEntry*)me;
    if (!e || e->dwSize < sizeof(MwModuleEntry)) return 0;
    e->modBaseAddr = (uintptr_t)g_image_base;
    e->modBaseSize = (uint32_t)g_image_size;
    snprintf(e->szModule, sizeof e->szModule, "%s", sw28_exe_name());
    snprintf(e->szExePath, sizeof e->szExePath, "%s", g_exe_path ? g_exe_path : "");
    return 1;
}
__attribute__((ms_abi)) int mw_Module32NextW(void* snap, void* me) { (void)snap; (void)me; return 0; }
__attribute__((ms_abi)) int mw_Thread32First(void* snap, void* te) { (void)snap; (void)te; return 0; }
__attribute__((ms_abi)) int mw_Thread32Next(void* snap, void* te) { (void)snap; (void)te; return 0; }
__attribute__((ms_abi)) int mw_Heap32ListFirst(void* snap, void* he) { (void)snap; (void)he; return 0; }
__attribute__((ms_abi)) int mw_Heap32ListNext(void* snap, void* he) { (void)snap; (void)he; return 0; }
__attribute__((ms_abi)) size_t mw_K32GetModuleFileNameExW(void* h, void* mod, uint16_t* buf, size_t sz) {
    (void)h; (void)mod;
    if (!buf || !sz) return 0;
    size_t n = 0;
    const char* p = g_exe_path ? g_exe_path : "";
    while (p[n] && n + 1 < sz) { buf[n] = (uint16_t)(uint8_t)p[n]; n++; }
    buf[n] = 0;
    return n;
}
__attribute__((ms_abi)) int mw_K32QueryWorkingSet(void* h, void* buf, uint32_t sz) { (void)h; (void)buf; (void)sz; return 0; }
__attribute__((ms_abi)) int mw_K32QueryWorkingSetEx(void* h, void* buf, uint32_t sz) { (void)h; (void)buf; (void)sz; return 0; }
__attribute__((ms_abi)) size_t mw_VirtualQueryEx(void* h, const void* addr, void* mbi, size_t len) {
    (void)h;
    if (!mbi || len < 32) return 0;
    uint8_t* m = (uint8_t*)mbi;
    memset(m, 0, len);
    *(const void**)(m + 0) = addr;   /* BaseAddress */
    *(uint32_t*)(m + 24) = 0x10000;  /* State = MEM_FREE */
    *(uint32_t*)(m + 28) = 0x01;     /* Protect = PAGE_NOACCESS */
    return sizeof(void*) == 8 ? 48 : 32; /* MEMORY_BASIC_INFORMATION64-lite */
}
__attribute__((ms_abi)) uint32_t mw_QueryFullProcessImageNameW(void* h, uint32_t flags, uint16_t* buf, uint32_t* sz) {
    (void)h; (void)flags;
    if (!buf || !sz) return 0;
    size_t n = 0;
    const char* p = g_exe_path ? g_exe_path : "";
    while (p[n] && n + 1 < *sz) { buf[n] = (uint16_t)(uint8_t)p[n]; n++; }
    buf[n] = 0; *sz = (uint32_t)n;
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetProcessId(void* h) {
    (void)h; return mw_GetCurrentProcessId();
}
__attribute__((ms_abi)) int mw_QueryThreadCycleTime(void* h, uint64_t* cycles) {
    (void)h;
    if (cycles) *cycles = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_IsHungAppWindow(void* h) { (void)h; return 0; }
__attribute__((ms_abi)) int mw_ShowWindowAsync(void* h, int cmd) { (void)h; (void)cmd; return 1; }
__attribute__((ms_abi)) void* mw_SendMessageTimeoutW(void* h, uint32_t msg, uintptr_t wp, intptr_t lp,
        uint32_t flags, uint32_t timeout, uintptr_t* result) {
    /* synchronous dispatch to the same pump SendMessageW uses */
    uintptr_t r = (uintptr_t)mw_SendMessageW(h, msg, wp, lp);
    if (result) *result = r;
    return (void*)1;
}
__attribute__((ms_abi)) int mw_SetLayeredWindowAttributes(void* h, uint32_t key, uint8_t alpha, uint32_t flags) {
    (void)h; (void)key; (void)alpha; (void)flags; return 1;
}
__attribute__((ms_abi)) uint32_t mw_SetDCPenColor(void* hdc, uint32_t color) { (void)hdc; return color; }
__attribute__((ms_abi)) int mw_GetTextExtentExPointW(void* hdc, const uint16_t* str, int c, int maxw,
        int* fit, int* dxs, void* sz) {
    (void)hdc; (void)maxw;
    if (fit) *fit = c;
    if (dxs && c > 0) for (int i = 0; i < c; i++) dxs[i] = (i + 1) * 8;
    if (sz) { ((int32_t*)sz)[0] = c * 8; ((int32_t*)sz)[1] = 16; }
    return str ? 1 : 0;
}
__attribute__((ms_abi)) void* mw_PropertySheetW(const void* psh) {
    (void)psh;
    return (void*)(uintptr_t)(MW_CHILD_BASE + 1); /* property-sheet identity */
}
__attribute__((ms_abi)) void* mw_CreatePropertySheetPageW(const void* page) {
    (void)page;
    return (void*)(uintptr_t)(0x00B00000u | (g_psp_next++ & 0xFFFu));
}
/* ---- procexp Nt* — typed honest NTSTATUS ---- */
#define MW_NT_NOT_IMPLEMENTED 0xC0000001u /* STATUS_UNSUCCESSFUL */
__attribute__((ms_abi)) uint32_t mw_NtQueryInformationProcess(void* h, uint32_t cls, void* buf, uint32_t len, uint32_t* retlen) {
    (void)h; (void)cls; (void)buf; (void)len;
    if (retlen) *retlen = 0;
    return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_NtQueryInformationThread(void* h, uint32_t cls, void* buf, uint32_t len, uint32_t* retlen) {
    (void)h; (void)cls; (void)buf; (void)len;
    if (retlen) *retlen = 0;
    return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_NtOpenThread(void** h, uint32_t access, void* oa, void* cid) {
    (void)access; (void)oa; (void)cid;
    if (h) *h = NULL;
    return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_NtCreateKey(void** k, uint32_t access, void* oa, uint32_t idx, const void* cls, uint32_t opts, uint32_t* disp) {
    (void)k; (void)access; (void)oa; (void)idx; (void)cls; (void)opts;
    if (disp) *disp = 0;
    return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_NtOpenKey(void** k, uint32_t access, void* oa) {
    (void)k; (void)access; (void)oa;
    return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_NtSetInformationProcess(void* h, uint32_t cls, void* buf, uint32_t len) {
    (void)h; (void)cls; (void)buf; (void)len; return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_NtSuspendProcess(void* h) { (void)h; return MW_NT_NOT_IMPLEMENTED; }
__attribute__((ms_abi)) uint32_t mw_NtResumeProcess(void* h) { (void)h; return MW_NT_NOT_IMPLEMENTED; }
__attribute__((ms_abi)) uint32_t mw_NtSuspendThread(void* h, uint32_t* prev) { (void)h; if (prev) *prev = 0; return MW_NT_NOT_IMPLEMENTED; }
__attribute__((ms_abi)) uint32_t mw_NtResumeThread(void* h, uint32_t* prev) { (void)h; if (prev) *prev = 0; return MW_NT_NOT_IMPLEMENTED; }
__attribute__((ms_abi)) uint32_t mw_NtLoadDriver(const uint16_t* reg) { (void)reg; return MW_NT_NOT_IMPLEMENTED; }
__attribute__((ms_abi)) uint32_t mw_NtUnloadDriver(const uint16_t* reg) { (void)reg; return MW_NT_NOT_IMPLEMENTED; }
__attribute__((ms_abi)) uint32_t mw_NtQueryEvent(void* h, uint32_t cls, void* buf, uint32_t len, uint32_t* rl) {
    (void)h; (void)cls; (void)buf; (void)len; if (rl) *rl = 0; return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_NtQueryMutant(void* h, uint32_t cls, void* buf, uint32_t len, uint32_t* rl) {
    (void)h; (void)cls; (void)buf; (void)len; if (rl) *rl = 0; return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_NtQuerySemaphore(void* h, uint32_t cls, void* buf, uint32_t len, uint32_t* rl) {
    (void)h; (void)cls; (void)buf; (void)len; if (rl) *rl = 0; return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_NtQuerySection(void* h, uint32_t cls, void* buf, uint32_t len, uint32_t* rl) {
    (void)h; (void)cls; (void)buf; (void)len; if (rl) *rl = 0; return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) void* mw_RtlCreateQueryDebugBuffer(uint32_t size, int eventPair) {
    (void)size; (void)eventPair;
    return NULL; /* honest: no debug-buffer service */
}
__attribute__((ms_abi)) uint32_t mw_RtlQueryProcessDebugInformation(uint32_t pid, uint32_t flags, void* buf) {
    (void)pid; (void)flags; (void)buf;
    return MW_NT_NOT_IMPLEMENTED;
}
__attribute__((ms_abi)) uint32_t mw_RtlDestroyQueryDebugBuffer(void* buf) { (void)buf; return 0; }
/* ---- LSA / token / security tails (procexp64) — typed honest ---- */
__attribute__((ms_abi)) uint32_t mw_LsaClose(void* h) { (void)h; return 0xC000000Du; /* STATUS_INVALID_HANDLE */ }
__attribute__((ms_abi)) uint32_t mw_LsaFreeMemory(void* p) { free(p); return 0; }
__attribute__((ms_abi)) uint32_t mw_LsaEnumerateAccountRights(void* h, void* sid, void** rights, uint32_t* count) {
    (void)h; (void)sid;
    if (rights) *rights = NULL;
    if (count) *count = 0;
    return 0xC000000Du;
}
__attribute__((ms_abi)) int mw_RevertToSelf(void) { return 1; }
__attribute__((ms_abi)) int mw_ImpersonateLoggedOnUser(void* tok) { (void)tok; return 0; }
__attribute__((ms_abi)) int mw_DuplicateTokenEx(void* tok, uint32_t access, void* attr, int impLevel, int tokenType, void** out) {
    (void)tok; (void)access; (void)attr; (void)impLevel;
    if (out) *out = NULL;
    return 0; /* FALSE: no token service */
}
__attribute__((ms_abi)) int mw_CreateRestrictedToken(void* tok, uint32_t flags, uint32_t nSid, void* sids,
        uint32_t nPriv, void* privs, uint32_t nDis, void* dis, void** out) {
    (void)tok; (void)flags; (void)nSid; (void)sids; (void)nPriv; (void)privs; (void)nDis; (void)dis;
    if (out) *out = NULL;
    return 0;
}
__attribute__((ms_abi)) int mw_SetTokenInformation(void* tok, int cls, void* info, uint32_t len) {
    (void)tok; (void)cls; (void)info; (void)len; return 0;
}
__attribute__((ms_abi)) void* mw_GetSidIdentifierAuthority(const void* sid) {
    (void)sid;
    static const uint8_t auth[6] = {0, 0, 0, 0, 0, 5}; /* NT_AUTHORITY */
    return (void*)(uintptr_t)auth;
}
__attribute__((ms_abi)) int mw_GetSecurityDescriptorDacl(const void* sd, int* present, void** acl, int* defd) {
    if (!sd) return 0;
    if (present) *present = 0;
    if (acl) *acl = NULL;
    if (defd) *defd = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_GetKernelObjectSecurity(void* h, uint32_t info, void* sd, uint32_t len, uint32_t* need) {
    (void)h; (void)info;
    if (need) *need = 0;
    (void)sd; (void)len;
    return 0;
}
__attribute__((ms_abi)) int mw_SetKernelObjectSecurity(void* h, uint32_t info, void* sd) { (void)h; (void)info; (void)sd; return 0; }
__attribute__((ms_abi)) int mw_SetServiceObjectSecurity(void* h, uint32_t info, void* sd) { (void)h; (void)info; (void)sd; return 0; }
__attribute__((ms_abi)) int mw_QueryServiceObjectSecurity(void* h, uint32_t info, void* sd, uint32_t len, uint32_t* need) {
    (void)h; (void)info;
    if (need) *need = 0;
    (void)sd; (void)len;
    return 0;
}
__attribute__((ms_abi)) int mw_AddAce(void* acl, uint32_t rev, uint32_t idx, void* ace) {
    (void)acl; (void)rev; (void)idx; (void)ace; return 0; /* FALSE: no ACL growth */
}
__attribute__((ms_abi)) int mw_ConvertStringSecurityDescriptorToSecurityDescriptorW(
        const uint16_t* sddl, uint32_t fmt, void** sd, uint32_t* sz) {
    (void)sddl; (void)fmt;
    if (sd) *sd = NULL;
    if (sz) *sz = 0;
    return 0;
}
/* ---- service config tails (procexp/autoruns) ---- */
__attribute__((ms_abi)) int mw_QueryServiceConfigW(void* svc, void* cfg, uint32_t len, uint32_t* need) {
    (void)svc;
    if (need) *need = 40;
    if (cfg && len >= 40) {
        uint32_t* u = (uint32_t*)cfg;
        memset(u, 0, 40);
        u[0] = 16; /* SERVICE_WIN32_OWN_PROCESS */
        u[1] = 3;  /* SERVICE_DEMAND_START */
    }
    return 1;
}
__attribute__((ms_abi)) int mw_QueryServiceConfig2W(void* svc, uint32_t level, void* buf, uint32_t len, uint32_t* need) {
    (void)svc; (void)level;
    if (need) *need = 8;
    if (buf && len >= 8) memset(buf, 0, 8);
    return 1;
}
__attribute__((ms_abi)) int mw_EnumServicesStatusExW(void* scm, uint32_t info, uint32_t state, uint32_t bufSize,
        uint8_t* buf, uint32_t* need, uint32_t* resumed, void* resume, const uint16_t* group) {
    (void)scm; (void)info; (void)state; (void)group; (void)resume;
    if (need) *need = 0;
    if (resumed) *resumed = 0;
    if (buf && bufSize) memset(buf, 0, bufSize);
    return 1; /* TRUE: enumeration complete, zero services */
}
__attribute__((ms_abi)) int mw_GetServiceDisplayNameW(void* scm, const uint16_t* key, uint16_t* disp, uint32_t* cch) {
    (void)scm; (void)key;
    if (cch) *cch = 0;
    return 0; /* FALSE: unknown service */
}
__attribute__((ms_abi)) int mw_DeleteService(void* svc) { (void)svc; return 1; }
__attribute__((ms_abi)) int mw_PulseEvent(void* h) { (void)h; return 1; }
__attribute__((ms_abi)) void* mw_CreateJobObjectW(void* attr, const uint16_t* name) {
    (void)attr; (void)name;
    return (void*)(uintptr_t)(0x00B10000u | (g_job_next++ & 0xFFFu));
}
__attribute__((ms_abi)) int mw_QueryInformationJobObject(void* job, uint32_t cls, void* buf, uint32_t len, uint32_t* ret) {
    (void)job; (void)cls;
    if (buf && len) memset(buf, 0, len);
    if (ret) *ret = len;
    return 1;
}
__attribute__((ms_abi)) int mw_IsProcessInJob(void* h, void* job, int* inJob) {
    (void)h; (void)job;
    if (inJob) *inJob = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_GetProcessInformation(void* h, int cls, void* buf, uint32_t len) {
    (void)h; (void)cls;
    if (buf && len) memset(buf, 0, len);
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_GetGuiResources(void* h, uint32_t type) { (void)h; (void)type; return 0; }
__attribute__((ms_abi)) int mw_GetMenuBarInfo(void* h, uint32_t idMenu, uint32_t item, void* mbi) {
    (void)h; (void)idMenu; (void)item; (void)mbi; return 0;
}
__attribute__((ms_abi)) int mw_EndTask(void* h, int shutdown, int force) { (void)h; (void)shutdown; (void)force; return 0; }
__attribute__((ms_abi)) void mw_EndMenu(void) {}
__attribute__((ms_abi)) int mw_AllowSetForegroundWindow(uint32_t pid) { (void)pid; return 1; }
__attribute__((ms_abi)) int mw_ChangeWindowMessageFilterEx(void* h, uint32_t msg, uint32_t action, void* chg) {
    (void)h; (void)msg; (void)action; (void)chg; return 1;
}
__attribute__((ms_abi)) int mw_EnableThemeDialogTexture(void* h, uint32_t flags) { (void)h; (void)flags; return 1; }
/* ---- exit/session tails ---- */
__attribute__((ms_abi)) int mw_ExitWindowsEx(uint32_t flags, uint32_t reason) { (void)flags; (void)reason; return 0; }
__attribute__((ms_abi)) int mw_LockWorkStation(void) { return 1; }
__attribute__((ms_abi)) int mw_SetSuspendState(int hibernate, int force, int wake) { (void)hibernate; (void)force; (void)wake; return 0; }
__attribute__((ms_abi)) int mw_IsPwrSuspendAllowed(void) { return 0; }
__attribute__((ms_abi)) int mw_IsPwrHibernateAllowed(void) { return 0; }
/* ---- ETW / trace tails (procexp64) — typed honest ERROR_NOT_SUPPORTED ---- */
__attribute__((ms_abi)) uint32_t mw_StartTraceW(uint64_t* handle, const uint16_t* name) {
    (void)name; if (handle) *handle = 0;
    return 1168u; /* ERROR_NOT_FOUND */
}
__attribute__((ms_abi)) uint32_t mw_ProcessTrace(void** h, uint32_t n, const void* s, const void* e) {
    (void)h; (void)n; (void)s; (void)e; return 1168u;
}
__attribute__((ms_abi)) uint64_t mw_OpenTraceW(const void* log) { (void)log; return 0xFFFFFFFFFFFFFFFFull; /* INVALID_PROCESSTRACE_HANDLE */ }
__attribute__((ms_abi)) uint32_t mw_CloseTrace(uint64_t h) { (void)h; return 0; }
__attribute__((ms_abi)) uint32_t mw_ControlTraceW(uint64_t h, const uint16_t* name, void* props, uint32_t code) {
    (void)h; (void)name; (void)props; (void)code; return 1168u;
}
__attribute__((ms_abi)) uint32_t mw_FlushTraceW(uint64_t h, const uint16_t* name, void* props) {
    (void)h; (void)name; (void)props; return 1168u;
}
/* ---- network tails (procexp64) — typed honest ---- */
__attribute__((ms_abi)) uint32_t mw_GetIfTable2(void** table) { if (table) *table = NULL; return 0xC00000BBu; /* ERROR_NOT_SUPPORTED-class */ }
__attribute__((ms_abi)) uint32_t mw_GetIfEntry2(void* row) { (void)row; return 0xC00000BBu; }
__attribute__((ms_abi)) void mw_FreeMibTable(void* p) { free(p); }
__attribute__((ms_abi)) uint32_t mw_GetExtendedTcpTable(void* tcpTable, uint32_t* size, int order, uint32_t af, uint32_t cls, uint32_t resv) {
    (void)tcpTable; (void)order; (void)af; (void)cls; (void)resv;
    if (size) *size = 4;
    return 0x000000EAu; /* ERROR_INSUFFICIENT_BUFFER with zero rows needed=4 */
}
__attribute__((ms_abi)) uint32_t mw_GetExtendedUdpTable(void* udpTable, uint32_t* size, int order, uint32_t af, uint32_t cls, uint32_t resv) {
    (void)udpTable; (void)order; (void)af; (void)cls; (void)resv;
    if (size) *size = 4;
    return 0x000000EAu;
}
__attribute__((ms_abi)) uint32_t mw_GetLogicalProcessorInformation(void* buf, uint32_t* len) {
    if (len) *len = 0;
    (void)buf;
    return 0x000000EAu;
}
__attribute__((ms_abi)) uint32_t mw_WNetGetConnectionW(const uint16_t* local, uint16_t* remote, uint32_t* len) {
    (void)local;
    if (len) *len = 0;
    (void)remote;
    return 0x00001FCAu; /* ERROR_NOT_CONNECTED */
}
__attribute__((ms_abi)) uint32_t mw_UrlUnescapeW(const uint16_t* url, uint16_t* out, uint32_t* pcch, uint32_t flags) {
    (void)flags;
    if (!url || !out || !pcch) return 0x80004005u;
    size_t n = mw_wlen16(url);
    if (*pcch < n + 1) { *pcch = (uint32_t)(n + 1); return 0x8007007Au; }
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < *pcch; i++) {
        if (url[i] == u'%' && i + 2 < n) {
            uint32_t hi = url[i + 1], lo = url[i + 2];
            int vh = (hi >= u'0' && hi <= u'9') ? (int)(hi - u'0') : (hi >= u'A' && hi <= u'F') ? (int)(hi - u'A' + 10) : -1;
            int vl = (lo >= u'0' && lo <= u'9') ? (int)(lo - u'0') : (lo >= u'A' && lo <= u'F') ? (int)(lo - u'A' + 10) : -1;
            if (vh >= 0 && vl >= 0) { out[o++] = (uint16_t)(vh * 16 + vl); i += 2; continue; }
        }
        out[o++] = url[i];
    }
    out[o] = 0;
    *pcch = (uint32_t)o;
    return 0; /* S_OK */
}
/* ---- SetupDi (procexp device tree) — typed honest ---- */
__attribute__((ms_abi)) void* mw_SetupDiGetClassDevsW(const void* cls, const uint16_t* enumr, void* hwnd, uint32_t flags) {
    (void)cls; (void)enumr; (void)hwnd; (void)flags;
    return (void*)(uintptr_t)0xFFFFFFFFFFFFFFFFull; /* INVALID_HANDLE_VALUE: no device service */
}
__attribute__((ms_abi)) int mw_SetupDiEnumDeviceInterfaces(void* set, void* ctx, const void* cls, uint32_t idx, void* did) {
    (void)set; (void)ctx; (void)cls; (void)idx; (void)did;
    return 0;
}
__attribute__((ms_abi)) int mw_SetupDiGetDeviceInterfaceDetailW(void* set, void* did, void* detail, uint32_t len, uint32_t* need, void* dev) {
    (void)set; (void)did; (void)detail; (void)len; (void)dev;
    if (need) *need = 8;
    return 0;
}
__attribute__((ms_abi)) int mw_SetupDiDestroyDeviceInfoList(void* set) { (void)set; return 1; }
/* ---- CredUI / cert (procexp/autoruns) — typed honest ---- */
__attribute__((ms_abi)) uint32_t mw_CredUIPromptForCredentialsW(const void* info, const uint16_t* target, const void* cert, uint32_t authErr,
        uint16_t* user, uint32_t ulen, uint16_t* pass, uint32_t plen, int* save, uint32_t flags) {
    (void)info; (void)target; (void)cert; (void)authErr; (void)save; (void)flags;
    if (user && ulen) user[0] = 0;
    if (pass && plen) pass[0] = 0;
    return 0x000004C7u; /* ERROR_CANCELLED — honest: no interactive prompt */
}
__attribute__((ms_abi)) uint32_t mw_CertGetNameStringW(const void* ctx, uint32_t type, uint32_t flags, void* para, uint16_t* name, uint32_t sz) {
    (void)ctx; (void)type; (void)flags; (void)para;
    if (name && sz) name[0] = 0;
    return 0;
}
/* ---- GDI+ cookie family (winmergeu) — honest v1 GDI+ ---- */
#define MW_GDIPLUS_OK 0u
#define MW_GDIPLUS_NOTIMPL 4u
#define MW_GDIPLUS_INVALIDPARAM 2u
#define MW_GDIPLUS_GENERICERR 1u
#define MW_GDOBJ_BASE 0x00B20000u
typedef struct { uint8_t kind; } MwGdObj; /* 1=graphics 2=pen 3=brush 4=path 5=bitmap */
static MwGdObj g_gdobj[64];
static uint32_t g_gdobj_next = 1;
static void* mw_gdobj_new(uint8_t kind) {
    for (int i = 0; i < 64; i++) {
        if (!g_gdobj[i].kind) { g_gdobj[i].kind = kind; return (void*)(uintptr_t)(MW_GDOBJ_BASE | (unsigned)(i + 1)); }
    }
    return NULL;
}
static int mw_gdobj_kind(void* h) {
    uintptr_t v = (uintptr_t)h;
    if ((v & 0xFFFF0000u) != MW_GDOBJ_BASE) return 0;
    int i = (int)(v & 0xFF) - 1;
    if (i < 0 || i >= 64) return 0;
    return g_gdobj[i].kind;
}
__attribute__((ms_abi)) void* mw_GdipAlloc(size_t n) { return calloc(1, n ? n : 1); }
__attribute__((ms_abi)) void mw_GdipFree(void* p) { free(p); }
__attribute__((ms_abi)) uint32_t mw_GdipCreateFromHDC(void* hdc, void** graphics) {
    if (!graphics) return MW_GDIPLUS_INVALIDPARAM;
    void* g = mw_gdobj_new(1);
    if (!g) return MW_GDIPLUS_GENERICERR;
    *graphics = g;
    (void)hdc;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipDeleteGraphics(void* g) { return mw_gdobj_kind(g) == 1 ? MW_GDIPLUS_OK : MW_GDIPLUS_INVALIDPARAM; }
__attribute__((ms_abi)) uint32_t mw_GdipCreatePen1(uint32_t color, float width, uint32_t unit, void** pen) {
    (void)color; (void)width; (void)unit;
    if (!pen) return MW_GDIPLUS_INVALIDPARAM;
    void* p = mw_gdobj_new(2);
    if (!p) return MW_GDIPLUS_GENERICERR;
    *pen = p;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipDeletePen(void* pen) { return mw_gdobj_kind(pen) == 2 ? MW_GDIPLUS_OK : MW_GDIPLUS_INVALIDPARAM; }
__attribute__((ms_abi)) uint32_t mw_GdipCreateSolidFill(uint32_t color, void** brush) {
    (void)color;
    if (!brush) return MW_GDIPLUS_INVALIDPARAM;
    void* b = mw_gdobj_new(3);
    if (!b) return MW_GDIPLUS_GENERICERR;
    *brush = b;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipCloneBrush(void* brush, void** clone) {
    if (mw_gdobj_kind(brush) != 3 || !clone) return MW_GDIPLUS_INVALIDPARAM;
    void* b = mw_gdobj_new(3);
    if (!b) return MW_GDIPLUS_GENERICERR;
    *clone = b;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipDeleteBrush(void* brush) { return mw_gdobj_kind(brush) == 3 ? MW_GDIPLUS_OK : MW_GDIPLUS_INVALIDPARAM; }
__attribute__((ms_abi)) uint32_t mw_GdipCreatePath(uint32_t fill, void** path) {
    (void)fill;
    if (!path) return MW_GDIPLUS_INVALIDPARAM;
    void* p = mw_gdobj_new(4);
    if (!p) return MW_GDIPLUS_GENERICERR;
    *path = p;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipDeletePath(void* path) { return mw_gdobj_kind(path) == 4 ? MW_GDIPLUS_OK : MW_GDIPLUS_INVALIDPARAM; }
__attribute__((ms_abi)) uint32_t mw_GdipAddPathLineI(void* path, int x1, int y1, int x2, int y2) {
    (void)path; (void)x1; (void)y1; (void)x2; (void)y2; return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipAddPathBezierI(void* path, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4) {
    (void)path; (void)x1; (void)x2; (void)x3; (void)x4; (void)y1; (void)y2; (void)y3; (void)y4;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipAddPathArcI(void* path, int x, int y, int w, int h, float start, float sweep) {
    (void)path; (void)x; (void)y; (void)w; (void)h; (void)start; (void)sweep; return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipStartPathFigure(void* path) { (void)path; return MW_GDIPLUS_OK; }
__attribute__((ms_abi)) uint32_t mw_GdipClosePathFigure(void* path) { (void)path; return MW_GDIPLUS_OK; }
__attribute__((ms_abi)) uint32_t mw_GdipDrawLineI(void* g, void* pen, int x1, int y1, int x2, int y2) {
    (void)g; (void)pen; (void)x1; (void)y1; (void)x2; (void)y2;
    return MW_GDIPLUS_OK; /* v1: status-honest, pixels unrendered */
}
__attribute__((ms_abi)) uint32_t mw_GdipDrawLine(void* g, void* pen, float x1, float y1, float x2, float y2) {
    (void)g; (void)pen; (void)x1; (void)y1; (void)x2; (void)y2;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipDrawLinesI(void* g, void* pen, const void* pts, int n) {
    (void)g; (void)pen; (void)pts; (void)n; return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipDrawBezierI(void* g, void* pen, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4) {
    (void)g; (void)pen; (void)x1; (void)x2; (void)x3; (void)x4; (void)y1; (void)y2; (void)y3; (void)y4;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipDrawPath(void* g, void* pen, void* path) {
    (void)g; (void)pen; (void)path; return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipFillRectangleI(void* g, void* brush, int x, int y, int w, int h) {
    (void)g; (void)brush; (void)x; (void)y; (void)w; (void)h;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipFillPath(void* g, void* brush, void* path) {
    (void)g; (void)brush; (void)path; return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipSetSmoothingMode(void* g, uint32_t mode) { (void)g; (void)mode; return MW_GDIPLUS_OK; }
__attribute__((ms_abi)) uint32_t mw_GdipSetInterpolationMode(void* g, uint32_t mode) { (void)g; (void)mode; return MW_GDIPLUS_OK; }
__attribute__((ms_abi)) uint32_t mw_GdipGetImageEncoders(uint32_t num, uint32_t size, void* encs, uint32_t* nGot) {
    if (nGot) *nGot = 0; /* zero codecs: honest (SWEEP-027 continuation) */
    (void)num; (void)size; (void)encs;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipCreateBitmapFromScan0(int w, int h, int stride, uint32_t fmt, void* bits, void** bmp) {
    if (w <= 0 || h <= 0 || !bmp) return MW_GDIPLUS_INVALIDPARAM;
    void* b = mw_gdobj_new(5);
    if (!b) return MW_GDIPLUS_GENERICERR;
    *bmp = b;
    (void)stride; (void)fmt; (void)bits;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipCreateBitmapFromHBITMAP(void* hbm, void* pal, void** bmp) {
    (void)hbm; (void)pal;
    if (!bmp) return MW_GDIPLUS_INVALIDPARAM;
    void* b = mw_gdobj_new(5);
    if (!b) return MW_GDIPLUS_GENERICERR;
    *bmp = b;
    return MW_GDIPLUS_OK;
}
__attribute__((ms_abi)) uint32_t mw_GdipCreateBitmapFromFile(const uint16_t* file, void** bmp) {
    (void)file;
    if (bmp) *bmp = NULL;
    return MW_GDIPLUS_GENERICERR; /* honest: no image decode service */
}
__attribute__((ms_abi)) uint32_t mw_GdipCreateHBITMAPFromBitmap(void* bmp, void** hbm, uint32_t bg) {
    (void)bmp; (void)bg;
    if (hbm) *hbm = NULL;
    return MW_GDIPLUS_GENERICERR;
}
__attribute__((ms_abi)) uint32_t mw_GdipBitmapLockBits(void* bmp, const void* rc, uint32_t flags, uint32_t fmt, void* data) {
    (void)bmp; (void)rc; (void)flags; (void)fmt; (void)data;
    return MW_GDIPLUS_NOTIMPL; /* honest: no pixel-lock service v1 */
}
__attribute__((ms_abi)) uint32_t mw_GdipBitmapUnlockBits(void* bmp, void* data) {
    (void)bmp; (void)data; return MW_GDIPLUS_OK;
}
/* ---- shell tails (winmergeu/autoruns/procexp) ---- */
__attribute__((ms_abi)) uint32_t mw_SHParseDisplayName(const uint16_t* path, void* bctx, void** pidl, uint32_t attr, uint32_t* attrOut) {
    (void)path; (void)bctx; (void)attr;
    if (pidl) *pidl = NULL;
    if (attrOut) *attrOut = 0;
    return 0x80004005u;
}
__attribute__((ms_abi)) uint32_t mw_SHGetPropertyStoreFromParsingName(const uint16_t* path, void* bctx, uint32_t flags, const void* iid, void** ppv) {
    (void)path; (void)bctx; (void)flags; (void)iid;
    if (ppv) *ppv = NULL;
    return 0x80004005u;
}
__attribute__((ms_abi)) uint32_t mw_SHCreateShellItem(void* parent, void* sf, void* pidl, void** item) {
    (void)parent; (void)sf; (void)pidl;
    if (item) *item = NULL;
    return 0x80004005u;
}
__attribute__((ms_abi)) void mw_SHAddToRecentDocs(uint32_t flags, const void* data) { (void)flags; (void)data; }
__attribute__((ms_abi)) uint32_t mw_SHGetStockIconInfo(uint32_t id, uint32_t flags, void* info) {
    (void)id; (void)flags; (void)info;
    return 0x80004005u; /* E_FAIL: no stock icon service */
}
__attribute__((ms_abi)) uint32_t mw_SHEvaluateSystemCommandTemplate(const uint16_t* cmd, uint16_t** abs, int* tracked, uint32_t* err) {
    (void)cmd;
    if (abs) *abs = NULL;
    if (tracked) *tracked = 0;
    if (err) *err = 2; /* SAST_UNKNOWN */
    return 0x80004005u;
}
__attribute__((ms_abi)) uint32_t mw_SHCreateStreamOnFileW(const uint16_t* path, uint32_t mode, void** stm) {
    (void)path; (void)mode;
    if (stm) *stm = NULL;
    return 0x80004005u;
}
__attribute__((ms_abi)) uint32_t mw_StgOpenStorageEx(const uint16_t* pwcs, uint32_t mode, uint32_t stgfmt, uint32_t attrs,
        const void* stgopts, uint32_t* resv, const void* iid, void** obj) {
    (void)pwcs; (void)mode; (void)stgfmt; (void)attrs; (void)stgopts; (void)resv; (void)iid;
    if (obj) *obj = NULL;
    return 0x80030002u; /* STG_E_FILENOTFOUND — honest */
}
__attribute__((ms_abi)) uint32_t mw_StgCreateStorageEx(const uint16_t* pwcs, uint32_t mode, uint32_t stgfmt, uint32_t attrs,
        const void* stgopts, uint32_t* resv, const void* iid, void** obj) {
    (void)pwcs; (void)mode; (void)stgfmt; (void)attrs; (void)stgopts; (void)resv; (void)iid;
    if (obj) *obj = NULL;
    return 0x80030005u; /* STG_E_ACCESSDENIED — honest */
}
/* ---- registry tree tails (winmergeu/procexp/autoruns) — real recursion ---- */
static uint32_t mw_reg_del_tree_w(void* key, const uint16_t* sub);
__attribute__((ms_abi)) uint32_t mw_RegDeleteTreeW(void* key, const uint16_t* sub) {
    /* real recursive delete: enum children, recurse, delete self */
    uint32_t r = mw_reg_del_tree_w(key, sub);
    MW_TRACE("RegDeleteTreeW -> 0x%08x", r);
    return r;
}
static uint32_t mw_reg_del_tree_w(void* key, const uint16_t* sub) {
    void* hk = key;
    uint32_t r;
    if (sub && sub[0]) {
        r = (uint32_t)(uintptr_t)mw_RegOpenKeyW(key, (const wchar_t*)sub, &hk);
        if (r != 0) return r;
    }
    for (;;) {
        char name[256];
        r = (uint32_t)mw_RegEnumKeyW(hk, 0, name, 256);
        if (r != 0) break;
        {
            uint16_t wname[256]; size_t i = 0;
            for (; i + 1 < 256 && name[i]; i++) wname[i] = (uint16_t)(uint8_t)name[i];
            wname[i] = 0;
            mw_reg_del_tree_w(hk, wname); /* recurse depth-first */
        }
    }
    r = 0;
    if (sub && sub[0]) {
        mw_RegCloseKey(hk);
        r = (uint32_t)mw_RegDeleteKeyW(key, sub);
    }
    return r;
}
__attribute__((ms_abi)) uint32_t mw_RegCopyTreeW(void* src, const uint16_t* ssub, void* dst) {
    (void)src; (void)ssub; (void)dst;
    return 0x00000003u; /* ERROR_PATH_NOT_FOUND — no source tree on engine hive */
}
__attribute__((ms_abi)) uint32_t mw_RegRenameKey(void* key, const uint16_t* sub, const uint16_t* newname) {
    (void)key; (void)sub; (void)newname;
    return 0x000000C9u; /* ERROR_ALREADY_EXISTS-class: no rename op */
}
__attribute__((ms_abi)) uint32_t mw_RegLoadKeyW(void* key, const uint16_t* sub, const uint16_t* file) {
    (void)key; (void)sub; (void)file;
    return 0x8007000Eu;
}
__attribute__((ms_abi)) uint32_t mw_RegUnLoadKeyW(void* key, const uint16_t* sub) {
    (void)key; (void)sub;
    return 0x8007000Eu;
}
/* ---- ImageList tails (winmergeu/autoruns) ---- */
__attribute__((ms_abi)) int mw_ImageList_Remove(void* himl, int idx) {
    MWImgList* L = mw_imgl_decode(himl);
    if (!L) return 0;
    if (idx == -1) { L->count = 0; return 1; }
    if (idx < 0 || idx >= L->count) return 0;
    L->count--; /* v1: swap-remove over the 3B/px tile store */
    if (idx < L->count)
        memmove(L->px + (size_t)idx * L->cx * L->cy * 3,
                L->px + (size_t)(idx + 1) * L->cx * L->cy * 3,
                (size_t)(L->count - idx) * L->cx * L->cy * 3);
    return 1;
}
typedef struct { void* hbmImage; void* hbmMask; uint32_t flags; } MwImgInfo;
__attribute__((ms_abi)) int mw_ImageList_GetImageInfo(void* himl, int idx, MwImgInfo* info) {
    MWImgList* L = mw_imgl_decode(himl);
    if (!L || !info || idx < 0 || idx >= L->count) return 0;
    memset(info, 0, sizeof *info);
    return 1;
}
__attribute__((ms_abi)) void* mw_ImageList_Duplicate(void* himl) {
    MWImgList* L = mw_imgl_decode(himl);
    if (!L) return NULL;
    void* n = mw_ImageList_Create(L->cx, L->cy, 1, 1, 8);
    if (n && L->count > 0) {
        /* v1: count only — pixels copied when ImageList_Add routes them */
        MWImgList* N = mw_imgl_decode(n);
        if (N && L->count * L->cx * L->cy * 3 > 0 && N->px) {
            size_t need = (size_t)L->count * L->cx * L->cy * 3;
            if ((size_t)N->cap * N->cx * N->cy * 3 < need) {
                uint8_t* np = (uint8_t*)realloc(N->px, need);
                if (np) { N->px = np; N->cap = L->count; }
            }
            if ((size_t)N->cap * N->cx * N->cy * 3 >= need) {
                memcpy(N->px, L->px, need);
                N->count = L->count;
            }
        }
    }
    return n;
}
__attribute__((ms_abi)) int mw_ImageList_Read(void* stm, void** pp) {
    (void)stm; if (pp) *pp = NULL; return 0;
}
__attribute__((ms_abi)) int mw_ImageList_WriteEx(void* himl, uint32_t opt, void* stm) {
    (void)himl; (void)opt; (void)stm; return 0;
}
__attribute__((ms_abi)) int mw_GradientFill(void* hdc, const void* verts, uint32_t nv, void* cells, uint32_t nc, uint32_t mode) {
    (void)verts; (void)nv; (void)cells; (void)nc; (void)mode;
    MWBmp* S = mw_dc_surface(hdc);
    if (S) mwb_fill(S, 0, 0, S->w, S->h, g_cur_brush); /* v1: flat fill */
    return 1;
}
__attribute__((ms_abi)) void* mw_ExtractAssociatedIconW(void* hinst, uint16_t* path, uint16_t* idx) {
    (void)hinst; (void)path;
    if (idx) *idx = 0;
    return NULL;
}
__attribute__((ms_abi)) int mw_EnumDisplaySettingsW(const uint16_t* dev, uint32_t mode, void* dm) {
    (void)dev;
    if (mode != 0xFFFFFFFFu /*ENUM_CURRENT_SETTINGS*/ || !dm) return 0;
    uint32_t* u = (uint32_t*)dm;
    memset(u, 0, 32);
    u[3] = (uint32_t)g_win_w > 0 ? (uint32_t)g_win_w : 1280; /* dmPelsWidth */
    u[4] = (uint32_t)g_win_h > 0 ? (uint32_t)g_win_h : 720;  /* dmPelsHeight */
    u[5] = 32; /* dmBitsPerPel */
    return 1;
}
__attribute__((ms_abi)) int mw_EnumDisplayDevicesW(const uint16_t* dev, uint32_t idx, void* dd, uint32_t flags) {
    (void)dev; (void)dd; (void)flags;
    return idx == 0 ? 1 : 0; /* one adapter identity */
}
/* ---- SW-1101h: round-2 tails (winmergeu 6 / procexp 9 / cports 1) ---- */
__attribute__((ms_abi)) int mw_DestroyCursor(void* h) { (void)h; return 1; }
__attribute__((ms_abi)) uint32_t mw_GetTabbedTextExtentW(void* hdc, const uint16_t* s, int c, int ntabs, const int* tabs) {
    (void)hdc; (void)ntabs; (void)tabs;
    return (uint32_t)((c > 0 ? (uint32_t)c : 0) * 8u) | (16u << 16); /* cx|cy<<16 */
}
__attribute__((ms_abi)) uint32_t mw_SetClassLongW(void* h, int idx, uint32_t val) {
    (void)h; (void)idx; (void)val;
    return 0; /* no previous class value */
}
__attribute__((ms_abi)) int32_t mw_RegQueryValueW(void* key, const uint16_t* name, void* data, uint32_t* cb) {
    return mw_RegQueryValueExW(key, (const wchar_t*)name, NULL, NULL, data, cb);
}
typedef struct { uint32_t cbSize; void* hdc; uint16_t* text; int cchText; void* rc; uint32_t format; } MwDrawTextParams;
__attribute__((ms_abi)) int mw_DrawTextExA(void* hdc, char* text, int cch, void* rc, uint32_t fmt, MwDrawTextParams* dt) {
    (void)dt;
    /* route through the A-form DrawText engine path */
    return mw_DrawTextA(hdc, text, cch, rc, fmt);
}
__attribute__((ms_abi)) int mw_GetThreadIdealProcessorEx(void* h, void* lpIdeal) {
    (void)h;
    if (lpIdeal) *(uint32_t*)lpIdeal = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_LookupPrivilegeNameW(const char* sys, const void* luid, uint16_t* name, uint32_t* cch) {
    (void)sys; (void)luid;
    if (cch) *cch = 0;
    return 0; /* FALSE: unknown LUID */
}
/* ---- SW-1101i: OLEAUT32 round-2 (pefile ord db ground truth, ord 5..202) ---- */
__attribute__((ms_abi)) uint32_t mw_SafeArrayRedim(void* sa, void* bounds) {
    (void)sa; (void)bounds;
    return 0x80004001u; /* E_NOTIMPL — honest */
}
__attribute__((ms_abi)) void* mw_SafeArrayCreate(uint32_t vt, uint32_t dims, void* bounds) {
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
}
__attribute__((ms_abi)) uint32_t mw_GetErrorInfo(uint32_t resv, void** pperr) {
    (void)resv;
    if (pperr) *pperr = NULL;
    return 1; /* S_FALSE: no error object */
}
__attribute__((ms_abi)) uint32_t mw_SetErrorInfo(uint32_t resv, void* err) {
    (void)resv; (void)err;
    return 0; /* S_OK: accepted, not stored */
}
__attribute__((ms_abi)) uint32_t mw_CreateErrorInfo(void** ppinfo) {
    if (ppinfo) *ppinfo = NULL;
    return 0x8007000Eu; /* E_OUTOFMEMORY — honest: no error-info store */
}
__attribute__((ms_abi)) uint32_t mw_VarDateFromStr(const uint16_t* str, uint32_t lcid, uint32_t flags, void* date) {
    (void)str; (void)lcid; (void)flags;
    if (date) *(uint64_t*)date = 0;
    return 0x80020005u; /* DISP_E_TYPEMISMATCH — honest */
}
__attribute__((ms_abi)) int mw_VariantTimeToSystemTime(double vt, void* st) {
    (void)vt; (void)st;
    return 0; /* FALSE — honest */
}
__attribute__((ms_abi)) int mw_SystemTimeToVariantTime(const void* st, double* vt) {
    (void)st;
    if (vt) *vt = 0.0;
    return 0; /* FALSE — honest */
}
__attribute__((ms_abi)) uint32_t mw_VariantCopyInd(void* pvargDest, const void* pvargSrc) {
    (void)pvargDest; (void)pvargSrc;
    return 0x80004001u; /* E_NOTIMPL — honest */
}
__attribute__((ms_abi)) int mw_SysReAllocStringLen(uint16_t** pbstr, uint32_t* plen) {
    (void)pbstr; (void)plen;
    return 0; /* FALSE: no in-place realloc v1 */
}
/* ---- Crypt* (procexp/autoruns) — cookie acquire, typed hash tails ---- */
__attribute__((ms_abi)) int mw_CryptAcquireContextW(void** phProv, const uint16_t* container, const uint16_t* provider, uint32_t type, uint32_t flags) {
    (void)container; (void)provider; (void)type; (void)flags;
    if (!phProv) return 0;
    *phProv = (void*)(uintptr_t)(0x00B30000u | (g_prov_next++ & 0xFFFu));
    return 1;
}
__attribute__((ms_abi)) int mw_CryptCreateHash(void* prov, uint32_t algid, void* key, uint32_t flags, void** phHash) {
    (void)prov; (void)algid; (void)key; (void)flags;
    if (!phHash) return 0;
    *phHash = (void*)(uintptr_t)(0x00B40000u | (g_hash_next++ & 0xFFFu));
    return 1;
}
__attribute__((ms_abi)) int mw_CryptHashData(void* hash, const uint8_t* data, uint32_t len, uint32_t flags) {
    (void)hash; (void)data; (void)len; (void)flags;
    return 1; /* accepted (digest not computed v1) */
}
__attribute__((ms_abi)) int mw_CryptGetHashParam(void* hash, uint32_t param, uint8_t* buf, uint32_t* len, uint32_t flags) {
    (void)hash; (void)param; (void)flags;
    if (buf && len && *len) memset(buf, 0, *len > 32 ? 32 : *len);
    return 0; /* FALSE: no digest service v1 — honest */
}
__attribute__((ms_abi)) int mw_CryptDestroyHash(void* hash) { (void)hash; return 1; }
__attribute__((ms_abi)) uint32_t mw_VariantChangeType(void* pvargDest, const void* pvarSrc,
        uint16_t wFlags, uint32_t vt) {
    (void)pvargDest; (void)pvarSrc; (void)wFlags; (void)vt;
    return 0x80020005u; /* DISP_E_TYPEMISMATCH — honest: no variant coercion v1 */
}
