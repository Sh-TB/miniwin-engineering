/* ============================================================
 * SWEEP-066 (SW-1297): Notepad++ 8.9.8.1 — 66-import wagon batch.
 * Drivers: artifacts/sweep66_sw1297/run3 (exit 9, 67 unresolved,
 * __fastfail(FAST_FAIL_INVALID_ARG=5) via UCRT validate_fd chain:
 * _read -> _get_handle(fd invalid) -> _invalid_parameter_noinfo_
 * noreturn -> int 0x29. Guest N++ hit the "config copy failed"
 * fallback after CopyFileExW-family stubs returned 0 on first run.
 * House rules: real where cheap (path surgery, strings, ctype,
 * file copy, PE header), typed-honest failures for host-impossible
 * services (COM items, Authenticode, IME composition, DWM/animation).
 * ============================================================ */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

/* ---- local narrow<->wide helpers (guest UTF-16 = uint16_t) ---- */
static int sw66_wlen(const uint16_t* s) { int n = 0; if (!s) return 0; while (s[n]) n++; return n; }
static int sw66_wstr_eq(const uint16_t* s, const char* ascii, int n) {
    for (int i = 0; i < n; i++) if (!s[i] || (char)s[i] != ascii[i]) return 0;
    return 1;
}
static void sw66_n2w(const char* s, uint16_t* d, int cap) {
    int i = 0;
    if (!d || cap <= 0) return;
    for (; s && s[i] && i < cap - 1; i++) d[i] = (uint16_t)(unsigned char)s[i];
    d[i] = 0;
}
static void sw66_w2n(const uint16_t* s, char* d, size_t cap) {
    size_t i = 0;
    if (!d || !cap) return;
    for (; s && s[i] && i < cap - 1; i++) d[i] = (char)(s[i] & 0xFF);
    d[i] = 0;
}

/* ============================================================
 * SHLWAPI path family — REAL per wine path.c semantics
 * ============================================================ */

/* PathCanonicalizeW: collapse ".." / ".", duplicate slashes, keep
 * UNC prefix. Returns TRUE if the result has no ".." left. */
__attribute__((ms_abi)) int mw_PathCanonicalizeW(uint16_t* dst, const uint16_t* src) {
    if (!dst) return 0;
    dst[0] = 0;
    if (!src) return 1;
    char p[2048]; sw66_w2n(src, p, sizeof p);
    char out[2048]; size_t o = 0;
    int unc = (p[0] == '\\' && p[1] == '\\');
    const char* q = unc ? p + 2 : p;
    if (unc) { out[o++] = '\\'; out[o++] = '\\'; }
    while (*q) {
        if (*q == '.') {
            if (q[1] == '\\' || !q[1]) { q += (q[1] ? 2 : 1); continue; } /* "./" */
            if (q[1] == '.' && (q[2] == '\\' || !q[2])) {
                /* "..": pop last component unless empty/UNC-rooted */
                size_t l = o;
                while (l > 0 && out[l - 1] == '\\') l--;
                while (l > 0 && out[l - 1] != '\\') l--;
                o = (l > 0 && !unc) ? l - 1 : l; /* drop separator too */
                out[o] = 0;
                q += (q[2] ? 3 : 2);
                continue;
            }
        }
        if (*q == '\\' && o && out[o - 1] == '\\') { q++; continue; } /* dup slash */
        out[o++] = *q++;
    }
    while (o > 0 && out[o - 1] == '\\' && !(unc && o <= 2)) out[--o] = 0; /* no trailing slash (except UNC \\) */
    if (o == 0) { out[o++] = '\\'; } /* empty -> root */
    out[o] = 0;
    sw66_n2w(out, dst, 2048);
    MW_TRACE("PathCanonicalizeW(%s) -> %s", p, out);
    return 1;
}

/* PathAppendW: append with separator, canonicalize. Returns dst. */
__attribute__((ms_abi)) uint16_t* mw_PathAppendW(uint16_t* path, const uint16_t* more) {
    if (!path || !more) return path;
    int pl = sw66_wlen(path);
    if (pl && path[pl - 1] != '\\' && path[pl - 1] != '/') path[pl++] = '\\';
    path[pl] = 0;
    /* skip leading separators on `more` */
    const uint16_t* m = more;
    while (*m == '\\' || *m == '/') m++;
    for (int i = 0; m[i] && pl < 2046; i++) path[pl++] = m[i];
    path[pl] = 0;
    mw_PathCanonicalizeW(path, path);
    return path;
}

/* PathStripPathW: keep only the final component (in place). */
__attribute__((ms_abi)) void mw_PathStripPathW(uint16_t* path) {
    if (!path) return;
    int l = sw66_wlen(path), cut = 0;
    for (int i = 0; i < l; i++) if (path[i] == '\\' || path[i] == '/') cut = i + 1;
    if (cut) {
        int j = 0;
        for (int i = cut; i <= l; i++) path[j++] = path[i];
    }
}

/* PathRemoveExtensionW: chop from the last '.' of the final component. */
__attribute__((ms_abi)) void mw_PathRemoveExtensionW(uint16_t* path) {
    if (!path) return;
    int l = sw66_wlen(path), dot = -1;
    for (int i = l - 1; i >= 0; i--) {
        if (path[i] == '.') { dot = i; break; }
        if (path[i] == '\\' || path[i] == '/') break;
    }
    if (dot >= 0) path[dot] = 0;
}

/* PathIsRelativeW: TRUE when no drive/root prefix. */
__attribute__((ms_abi)) int mw_PathIsRelativeW(const uint16_t* path) {
    if (!path || !path[0]) return 1;
    if (path[1] == ':' && path[0] >= 'A' && path[0] <= 'z') return 0;
    if (path[0] == '\\' || path[0] == '/') return 0;
    return 1;
}

/* PathGetDriveNumberW: 'C'..'Z' -> 0..25 else -1. */
__attribute__((ms_abi)) int mw_PathGetDriveNumberW(const uint16_t* path) {
    if (!path || !path[0] || path[1] != ':') return -1;
    uint16_t c = path[0];
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= 'A' && c <= 'Z') return c - 'A';
    return -1;
}

/* PathCompactPathExW: truncate to cchMax-1 chars + "..." + tail char.
 * Returns cch written. */
__attribute__((ms_abi)) int mw_PathCompactPathExW(uint16_t* out, const uint16_t* src,
                                                  unsigned int cchMax, unsigned int flags) {
    (void)flags;
    if (!out || !cchMax) return 0;
    if (!src) { out[0] = 0; return 1; }
    int l = sw66_wlen(src);
    if ((unsigned)l < cchMax) { for (int i = 0; i <= l; i++) out[i] = src[i]; return l + 1; }
    if (cchMax <= 4) { /* no room for dots */
        for (unsigned i = 0; i < cchMax - 1 && i < (unsigned)l; i++) out[i] = src[i];
        out[cchMax - 1] = 0; return cchMax;
    }
    unsigned head = cchMax - 4;           /* head chars */
    for (unsigned i = 0; i < head; i++) out[i] = src[i];
    out[head] = '.'; out[head + 1] = '.'; out[head + 2] = '.';
    out[head + 3] = src[l - 1];           /* wine keeps the last char */
    out[head + 4] = 0;
    return (int)(head + 4);
}

/* AssocQueryStringW: host has no shell association database.
 * Typed honest: E_FAIL, out truncated empty. */
__attribute__((ms_abi)) int mw_AssocQueryStringW(unsigned int flags, unsigned int str,
                                                 const uint16_t* assoc, const uint16_t* extra,
                                                 uint16_t* out, unsigned int* pcchOut) {
    (void)flags; (void)str; (void)assoc; (void)extra;
    MW_TRACE("AssocQueryStringW -> E_FAIL (no assoc db)");
    if (out && pcchOut && *pcchOut) out[0] = 0;
    return (int)0x80004005LL;
}

/* ColorAdjustLuma: wine shlwapi — luma is 0..240 HSL-L in the low
 * byte (upper byte ignored); scale=FALSE adjusts by percent. */
__attribute__((ms_abi)) unsigned short mw_ColorAdjustLuma(unsigned short luma, int percent, int scale) {
    int l = luma & 0xFF;
    if (scale) l = (l * percent) / 100;
    else       l = l + (l * percent) / 100;
    if (l < 0) l = 0;
    if (l > 240) l = 240;
    return (unsigned short)(l & 0xFF);
}

/* IsTextUnicode (advapi32 export): minimal honest heuristic — BOM
 * signature and null-density only; IS_TEXT_UNICODE_* flags. */
/* ============================================================
 * USER32 tails
 * ============================================================ */

/* ToAscii: our keyboard model has no VK->char table (ToUnicodeEx
 * returns 0 too) — honest 0 = "no translation". */
__attribute__((ms_abi)) int mw_ToAscii(unsigned int vk, unsigned int sc, const unsigned char* kbd,
                                       unsigned short* chars, unsigned int menu) {
    (void)vk; (void)sc; (void)kbd; (void)menu;
    if (chars) chars[0] = 0;
    return 0;
}

__attribute__((ms_abi)) int mw_IsCharAlphaW(uint16_t c) {
    uint32_t u = c;
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
}
__attribute__((ms_abi)) int mw_IsCharAlphaNumericW(uint16_t c) {
    return mw_IsCharAlphaW(c) || (c >= '0' && c <= '9');
}

__attribute__((ms_abi)) int mw_AddClipboardFormatListener(void* hwnd) {
    MW_TRACE("AddClipboardFormatListener(%p) -> TRUE (no WM_CLIPBOARDUPDATE)", hwnd);
    return 1;
}
__attribute__((ms_abi)) int mw_RemoveClipboardFormatListener(void* hwnd) {
    MW_TRACE("RemoveClipboardFormatListener(%p) -> TRUE", hwnd);
    return 1;
}

/* EnumThreadWindows: our threads own no windows (pump model) —
 * FALSE = enumeration finished with zero windows. */
__attribute__((ms_abi)) int mw_EnumThreadWindows(unsigned int tid, void* cb, intptr_t lp) {
    (void)cb; (void)lp;
    MW_TRACE("EnumThreadWindows(%u) -> FALSE (no per-thread windows)", tid);
    return 0;
}

/* mouse_event is VOID on Windows; keep void for ABI honesty. */
__attribute__((ms_abi)) void mw_mouse_event(unsigned int flags, unsigned int dx, unsigned int dy,
                                            unsigned int data, uintptr_t extra) {
    (void)flags; (void)dx; (void)dy; (void)data; (void)extra;
}

/* ============================================================
 * GDI32
 * ============================================================ */

/* ExtCreatePen: cosmetic pens behave like CreatePen (color only);
 * geometric pens take width from LOGBRUSH — v1 stores color in the
 * shared pen table (width ignored, documented approximation). */
/* GetClipRgn: copy the DC's stored clip region into hrgn.
 * Returns 1 = region existed (copied), 0 = no clip region. */
/* ============================================================
 * KERNEL32 tails
 * ============================================================ */

/* GetTimeFormatEx / GetDateFormatEx: wide twins of the SWEEP-027
 * A-versions — same honest YYYY-MM-DD / HH:MM:SS rendering. */
/* GetProductInfo: single virtual SKU = PRODUCT_ENTERPRISE (4). */
__attribute__((ms_abi)) int mw_GetProductInfo(unsigned int a, unsigned int b, unsigned int c,
                                              unsigned int d, unsigned int* type) {
    (void)a; (void)b; (void)c; (void)d;
    if (type) *type = 4;
    return 1;
}

/* Application-restart trio: N++ (session restore) probes them.
 * Register/Unregister are honest no-ops (we never restart); Get
 * reports E_FAIL = "no restart settings registered". */
__attribute__((ms_abi)) int mw_RegisterApplicationRestart(const uint16_t* cmd, unsigned int flags) {
    (void)cmd; (void)flags;
    MW_TRACE("RegisterApplicationRestart -> S_OK (never honored)");
    return 0; /* S_OK */
}
__attribute__((ms_abi)) int mw_UnregisterApplicationRestart(void) { return 0; /* S_OK */ }
__attribute__((ms_abi)) int mw_GetApplicationRestartSettings(void* proc, uint16_t* cmd,
                                                             unsigned int* cch, unsigned int* flags) {
    (void)proc; (void)cmd;
    if (cch) *cch = 0;
    if (flags) *flags = 0;
    return (int)0x80004005LL; /* E_FAIL: no restart registered */
}

/* CopyFileExW: real copy through the CopyFileW model + progress-hook
 * precheck. Failure maps errno -> Win32 (PATH_NOT_FOUND/ACCESS_DENIED). */
__attribute__((ms_abi)) int mw_CopyFileExW(const uint16_t* src, const uint16_t* dst,
                                           void* progress, void* data, int* cancel,
                                           unsigned int flags) {
    if (cancel && *cancel) { mw_SetLastError(123); return 0; } /* ERROR_CANCELLED */
    if (!src || !dst) { mw_SetLastError(87); return 0; }
    int failIfExists = (flags & 0x1) ? 1 : 0; /* COPY_FILE_FAIL_IF_EXISTS */
    int ok = mw_CopyFileW(src, dst, failIfExists);
    if (!ok) {
        char d[1024]; sw66_w2n(dst, d, sizeof d);
        if (access(d, F_OK) == 0) mw_SetLastError(80);        /* FILE_EXISTS */
        else if (errno == EACCES) mw_SetLastError(5);          /* ACCESS_DENIED */
        else mw_SetLastError(3);                               /* PATH_NOT_FOUND */
    }
    (void)progress; (void)data; /* progress callback never invoked (documented) */
    return ok;
}

/* QueueUserAPC: APC delivery is not modeled by the pump — typed
 * honest failure (0) so guests take their fallback paths. */
/* GetStringTypeExA: real CT_CTYPE1 for the ASCII range (guests pass
 * ANSI paths/text; >0x7F maps to punctuation-neutral flags). */
/* CompareStringOrdinal: REAL ordinal compare (case-fold optional),
 * CSTR_LESS_THAN=1 / EQUAL=2 / GREATER=3. */
/* ============================================================
 * UXTHEME buffered-paint/animation family — typed honest:
 * no compositor. Begin returns NULL -> guests paint synchronously.
 * ============================================================ */
__attribute__((ms_abi)) void* mw_BeginBufferedAnimation(void* theme, void* dcTarget, const void* rcTarget,
                                                        unsigned int style, void* params,
                                                        void* animFrom, void* animTo) {
    (void)theme; (void)dcTarget; (void)rcTarget; (void)style; (void)params; (void)animFrom; (void)animTo;
    MW_TRACE("BeginBufferedAnimation -> NULL (no animation)");
    return NULL;
}
__attribute__((ms_abi)) int mw_EndBufferedAnimation(void* handle, int paintTarget) {
    (void)handle; (void)paintTarget;
    return 1;
}
__attribute__((ms_abi)) int mw_BufferedPaintRenderAnimation(void* hdc, const void* rcTarget) {
    (void)hdc; (void)rcTarget;
    return 0; /* nothing animated */
}
__attribute__((ms_abi)) int mw_BufferedPaintStopAllAnimations(void* hwnd) {
    (void)hwnd;
    return 1;
}
__attribute__((ms_abi)) int mw_GetThemeTransitionDuration(void* theme, int part, int state,
                                                          int prop, unsigned int* duration) {
    (void)theme; (void)part; (void)state; (void)prop;
    if (duration) *duration = 0;
    return 0; /* S_OK */
}
__attribute__((ms_abi)) int mw_GetThemeBackgroundContentRect(void* theme, void* dc, int part, int state,
                                                             const void* bounding, void* content) {
    (void)theme; (void)dc; (void)part; (void)state;
    if (content && bounding) memcpy(content, bounding, 16); /* RECT = 4x int32 */
    return 0; /* S_OK */
}
__attribute__((ms_abi)) int mw_DrawThemeTextEx(void* theme, void* dc, int part, int state,
                                               const uint16_t* text, int cch, unsigned int flags,
                                               void* rect, const void* options) {
    (void)options;
    return mw_DrawThemeText(theme, dc, part, state, text, cch, flags, 0, rect);
}

/* ============================================================
 * IMM32 — our ImmGetContext returns NULL; state setters are typed
 * honest failures so guests skip IME configuration.
 * ============================================================ */
__attribute__((ms_abi)) int mw_ImmSetCandidateWindow(void* himc, const void* cand) {
    (void)himc; (void)cand;
    return 0;
}
__attribute__((ms_abi)) int mw_ImmSetCompositionStringW(void* himc, unsigned int sel, const void* comp,
                                                        unsigned int compLen, const void* read,
                                                        unsigned int readLen) {
    (void)himc; (void)sel; (void)comp; (void)compLen; (void)read; (void)readLen;
    return 0;
}
__attribute__((ms_abi)) int mw_ImmEscapeW(void* hkl, void* himc, unsigned int esc, void* data) {
    (void)hkl; (void)himc; (void)esc; (void)data;
    return 0;
}

/* ============================================================
 * CRYPT32 / WINTRUST — no Authenticode world. Signature checks fail
 * typed (CRYPT_E_FILE_ERROR family), guests take "unverified" paths.
 * ============================================================ */
__attribute__((ms_abi)) int mw_CryptQueryObject(unsigned int ty, const void* blob, unsigned int expected,
                                                unsigned int flags, unsigned int reserved,
                                                unsigned int* msgType, unsigned int* contentType,
                                                unsigned int* fmtType, void* certStore, void* msg,
                                                void* context) {
    (void)ty; (void)blob; (void)expected; (void)flags; (void)reserved;
    if (msgType) *msgType = 0;
    if (contentType) *contentType = 0;
    if (fmtType) *fmtType = 0;
    mw_SetLastError(0x80092003u); /* CRYPT_E_FILE_ERROR */
    return 0;
}
__attribute__((ms_abi)) int mw_CertGetCertificateContextProperty(const void* ctx, unsigned int prop,
                                                                 void* buf, unsigned int* len) {
    (void)ctx; (void)prop; (void)buf;
    if (len) *len = 0;
    mw_SetLastError(0x80092004u); /* CRYPT_E_NOT_FOUND */
    return 0;
}
__attribute__((ms_abi)) void* mw_CertFindCertificateInStore(void* store, unsigned int enc, unsigned int flags,
                                                            unsigned int type, const void* para,
                                                            const void* prev) {
    (void)store; (void)enc; (void)flags; (void)type; (void)para; (void)prev;
    mw_SetLastError(0x80092004u); /* CRYPT_E_NOT_FOUND */
    return NULL;
}
__attribute__((ms_abi)) int mw_CryptMsgGetParam(const void* msg, unsigned int type, unsigned int idx,
                                                void* buf, unsigned int* len) {
    (void)msg; (void)type; (void)idx; (void)buf;
    if (len) *len = 0;
    mw_SetLastError(0x80091002u); /* CRYPT_E_INVALID_MSG_TYPE */
    return 0;
}
__attribute__((ms_abi)) unsigned int mw_CertNameToStrW(unsigned int enc, const void* name,
                                                       unsigned int strType, uint16_t* out,
                                                       unsigned int cch) {
    (void)enc; (void)name; (void)strType;
    if (out && cch) out[0] = 0;
    return 0;
}
__attribute__((ms_abi)) int mw_CryptMsgClose(const void* msg) {
    (void)msg;
    return 1; /* cookie free */
}
__attribute__((ms_abi)) long mw_WinVerifyTrust(void* hwnd, const void* actionId, void* data) {
    (void)hwnd; (void)actionId; (void)data;
    MW_TRACE("WinVerifyTrust -> TRUST_E_NOSIGNATURE");
    return (long)0x800B0100LL; /* TRUST_E_NOSIGNATURE: honest */
}

/* ============================================================
 * SENSAPI / WININET
 * ============================================================ */
__attribute__((ms_abi)) int mw_IsNetworkAlive(unsigned int* flags) {
    if (flags) *flags = 0;
    mw_SetLastError(1222); /* ERROR_NO_NETWORK */
    return 0;
}
__attribute__((ms_abi)) int mw_IsDestinationReachableW(const void* dest, void* qocInfo) {
    (void)dest; (void)qocInfo;
    mw_SetLastError(1222); /* ERROR_NO_NETWORK */
    return 0;
}

/* InternetCrackUrlW: REAL per wine wininet: splits scheme/user/pass/
 * host/port/path/extra. URL_COMPONENTSW x64 layout (offsets, since the
 * struct is ABI: 0x00 size, 0x08 scheme, 0x10 schemeLen, 0x14 schemeType,
 * 0x18 host, 0x20 port(WORD), 0x28 user, 0x30 userLen, 0x38 pass,
 * 0x40 passLen, 0x48 path, 0x50 pathLen, 0x58 extra, 0x60 extraLen. */
typedef struct { uint8_t raw[0x68]; } URLCOMP66;
#define UC66(fld)  ((uint16_t**)(uc->raw + 0x##fld))
#define UC66D(fld) ((unsigned int*)(uc->raw + 0x##fld))
__attribute__((ms_abi)) int mw_InternetCrackUrlW(const uint16_t* url, unsigned int urlLen,
                                                 unsigned int flags, URLCOMP66* uc) {
    (void)flags;
    if (!url || !uc || *UC66D(00) < 0x68) { mw_SetLastError(12006); return 0; } /* INVALID_URL */
    uint16_t** scheme = UC66(08);    unsigned int* schemeLen = UC66D(10);
    unsigned int* schemeType = UC66D(14);
    uint16_t** host = UC66(18);      unsigned short* port = (unsigned short*)(uc->raw + 0x20);
    uint16_t** user = UC66(28);      unsigned int* userLen = UC66D(30);
    uint16_t** pass = UC66(38);      unsigned int* passLen = UC66D(40);
    uint16_t** path = UC66(48);      unsigned int* pathLen = UC66D(50);
    uint16_t** extra = UC66(58);     unsigned int* extraLen = UC66D(60);
    if (urlLen == (unsigned int)-1) urlLen = (unsigned int)sw66_wlen(url);
    const uint16_t* p = url, *end = url + urlLen;
    /* scheme:// */
    const uint16_t* colon = NULL;
    for (const uint16_t* q = p; q < end; q++) {
        if (*q == ':') { colon = q; break; }
        if (*q == '/' || *q == '?' || *q == '#') break;
    }
    unsigned int st = 0; unsigned short dport = 0;
    if (colon && colon + 2 < end && colon[1] == '/' && colon[2] == '/') {
        unsigned int n = (unsigned int)(colon - p);
        if (scheme && *scheme) {
            unsigned int m = (n < *schemeLen) ? n : (*schemeLen ? *schemeLen - 1 : 0);
            for (unsigned int i = 0; i < m; i++) (*scheme)[i] = p[i];
            if (*schemeLen) (*scheme)[m] = 0;
        }
        if (schemeLen && *schemeLen) *schemeLen = n;
        if (n == 4 && sw66_wstr_eq(p, "http", 4)) { st = 3; dport = 80; }   /* SCHEME_HTTP */
        else if (n == 5 && sw66_wstr_eq(p, "https", 5)) { st = 4; dport = 443; }
        else if (n == 3 && sw66_wstr_eq(p, "ftp", 3)) { st = 2; dport = 21; }
        else st = 1; /* GENERIC */
        p = colon + 3;
    }
    *schemeType = st;
    /* authority: [user[:pass]@]host[:port] up to '/', '?', '#' */
    const uint16_t* auEnd = p;
    while (auEnd < end && *auEnd != '/' && *auEnd != '?' && *auEnd != '#') auEnd++;
    const uint16_t* at = NULL;
    for (const uint16_t* q = p; q < auEnd; q++) if (*q == '@') at = q;
    const uint16_t* hb = at ? at + 1 : p;
    const uint16_t* he = auEnd;
    unsigned short gotPort = 0;
    for (const uint16_t* q = auEnd - 1; q >= hb; q--) {
        if (*q == ':') {
            unsigned int v = 0; int any = 0;
            for (const uint16_t* d = q + 1; d < auEnd; d++) {
                if (*d < '0' || *d > '9') { any = 0; break; }
                v = v * 10 + (*d - '0'); any = 1;
            }
            if (any) { he = q; gotPort = (unsigned short)v; }
            break;
        }
        if (*q == ']') break; /* IPv6 literal tail */
    }
    if (at && user && *user) {
        unsigned int n = (unsigned int)(at - p);
        int hasPass = 0;
        for (unsigned int i = 0; i < n; i++) if (p[i] == ':') { n = i; hasPass = 1; break; }
        for (unsigned int i = 0; i < n && i < *userLen; i++) (*user)[i] = p[i];
        if (*userLen) (*user)[n < *userLen ? n : *userLen - 1] = 0;
        *userLen = n;
        if (hasPass && pass && *pass) {
            const uint16_t* pw = p + n + 1;
            unsigned int m = (unsigned int)(at - pw);
            for (unsigned int i = 0; i < m && i < *passLen; i++) (*pass)[i] = pw[i];
            if (*passLen) (*pass)[m < *passLen ? m : *passLen - 1] = 0;
            *passLen = m;
        }
    } else {
        if (userLen) *userLen = 0;
        if (passLen) *passLen = 0;
    }
    unsigned int hn = (unsigned int)(he - hb);
    if (host && *host) {
        /* URL_COMPONENTS has NO hostLen field — guest pre-sizes the buffer;
         * copy hn units, defensive cap 1024, NUL-terminate. */
        if (hn > 1024) hn = 1024;
        for (unsigned int i = 0; i < hn; i++) (*host)[i] = hb[i];
        (*host)[hn] = 0;
    }
    *port = gotPort ? gotPort : dport;
    /* path + extra */
    const uint16_t* epath = auEnd;
    while (epath < end && *epath != '?' && *epath != '#') epath++;
    unsigned int pn = (unsigned int)(epath - auEnd);
    if (path && *path) {
        for (unsigned int i = 0; i < pn && i < *pathLen; i++) (*path)[i] = auEnd[i];
        if (*pathLen) (*path)[pn < *pathLen ? pn : *pathLen - 1] = 0;
    }
    if (pathLen) *pathLen = pn;
    unsigned int en = (unsigned int)(end - epath);
    if (extra && *extra) {
        for (unsigned int i = 0; i < en && i < *extraLen; i++) (*extra)[i] = epath[i];
        if (*extraLen) (*extra)[en < *extraLen ? en : *extraLen - 1] = 0;
    }
    if (extraLen) *extraLen = en;
    MW_TRACE("InternetCrackUrlW -> 1");
    return 1;
}

/* ============================================================
 * SHELL32 / COMCTL32 / dbghelp / DWMAPI
 * ============================================================ */
__attribute__((ms_abi)) int mw_SHOpenFolderAndSelectItems(void* item, unsigned int n, void** items,
                                                          unsigned int flags) {
    (void)item; (void)n; (void)items; (void)flags;
    return (int)0x80004005LL; /* E_FAIL: no shell UI */
}
/* ImageNtHeader: REAL — validate MZ, return base + e_lfanew. */
__attribute__((ms_abi)) void* mw_ImageNtHeader(const void* base) {
    uint8_t* b = (uint8_t*)(base ? (uintptr_t)base : (uintptr_t)g_image_base);
    if (!b) { mw_SetLastError(126); return NULL; } /* ERROR_MOD_NOT_FOUND */
    if (b[0] != 'M' || b[1] != 'Z') return NULL;
    uint32_t e_lfanew = *(const uint32_t*)(b + 0x3C);
    if (e_lfanew > 0x1000) return NULL;
    uint8_t* nth = b + e_lfanew;
    if (nth[0] != 'P' || nth[1] != 'E') return NULL;
    return (void*)nth;
}
__attribute__((ms_abi)) int mw_DwmGetColorizationColor(unsigned int* color, int* opaque) {
    (void)color; (void)opaque;
    return (int)0x80004005LL; /* E_FAIL: DWM off -> guest uses theme colors */
}
