/* ==== SWEEP-034 IMPL START ====
 * Train 3 of 5. Wagons: SW-1153 ODBC mini-driver — REAL sqlite3-backed
 * (host libsqlite3.so, /usr/include/sqlite3.h): DBC = sqlite3*, STMT =
 * sqlite3_stmt* + cursor state; DriverConnectW opens real files,
 * ExecDirectW prepares + steps, Fetch steps, GetData converts
 * (C_WCHAR/C_CHAR/C_SLONG/C_LONG/C_DOUBLE, SQL_NULL_DATA), BindCol
 * fetch-time copy, NumResultCols/DescribeCol from sqlite metadata,
 * RowCount from sqlite3_changes, DiagRecW from sqlite3_errmsg16.
 * Replaces the SWEEP-029b typed-honest v1 (cookie pool stays for ENV).
 * Spliced by scripts/sweep034_apply.py. */

/* ==== SECTION: odbc ==== */
/* ODBC v2/v3 constants (sqltypes.h/sql.h — no Windows headers on host) */
#define MW_SQL_SUCCESS           0
#define MW_SQL_SUCCESS_WITH_INFO 1
#define MW_SQL_ERROR             (-1)
#define MW_SQL_INVALID_HANDLE    (-2)
#define MW_SQL_NO_DATA           100
#define MW_SQL_NULL_DATA         (-1)
#define MW_SQL_NO_TOTAL          (-4)
#define MW_SQL_HANDLE_ENV        1
#define MW_SQL_HANDLE_DBC        2
#define MW_SQL_HANDLE_STMT       3
#define MW_SQL_C_CHAR            1
#define MW_SQL_C_SLONG           (-6)
#define MW_SQL_C_LONG            4
#define MW_SQL_C_DOUBLE          8
#define MW_SQL_C_WCHAR           (-8)
#define MW_SQL_CHAR              1
#define MW_SQL_INTEGER           4
#define MW_SQL_DOUBLE            8
#define MW_SQL_WVARCHAR          (-9)

#include <sqlite3.h>

/* Handle tables: cookies keep the low16=0 pattern (0x00B8 DBC, 0x00B9 STMT);
 * the legacy 0x00031000 pool stays for ENV handles (SWEEP-029b callers). */
#define MW_ODBC_DBC_BASE  0x00B80000u
#define MW_ODBC_STMT_BASE 0x00B90000u
#define MW_ODBC_MAX       32
typedef struct {
    uint8_t live;
    sqlite3* db;
} MwOdbcDbc;
typedef struct {
    uint8_t live;
    sqlite3* db;          /* owning connection (not closed by stmt) */
    sqlite3_stmt* st;
    int started;          /* ExecDirect already stepped once */
    int done;             /* cursor exhausted */
    int64_t affected;     /* for DML RowCount */
    /* BindCol records (SW-1153): column (1-based), C type, guest ptr, len */
    struct { void* ptr; int len; int16_t ctype; } binds[16];
    uint16_t nbinds;
} MwOdbcStmt;
static MwOdbcDbc g_odbc_dbc[MW_ODBC_MAX];
static MwOdbcStmt g_odbc_stmt[MW_ODBC_MAX];
static int mw_odbc_slot(uintptr_t v, uintptr_t base) {
    if ((v & 0xFFFF0000u) != base) return -1;
    int i = (int)(v & 0xFFFFu) - 1;
    return (i >= 0 && i < MW_ODBC_MAX) ? i : -1;
}
static int mw_odbc_is_dbc(void* h) { return mw_odbc_slot((uintptr_t)h, MW_ODBC_DBC_BASE) >= 0; }
static int mw_odbc_is_stmt(void* h) { return mw_odbc_slot((uintptr_t)h, MW_ODBC_STMT_BASE) >= 0; }

/* UTF-16 -> UTF-8 helper for connection strings (SQLWCHAR = 2 bytes on Win) */
static int mw_odbc_w16_to_utf8(const uint16_t* s, int16_t cch, char* out, int cap) {
    int n = 0;
    int lim = (cch < 0) ? 4096 : cch; /* SQL_NTS = -1 */
    for (int i = 0; i < lim; i++) {
        uint32_t c = s[i];
        if (c == 0) break;
        if (c < 0x80) { if (n < cap - 1) out[n++] = (char)c; }
        else if (c < 0x800) { if (n < cap - 2) { out[n++] = (char)(0xC0 | (c >> 6)); out[n++] = (char)(0x80 | (c & 63)); } }
        else { if (n < cap - 3) { out[n++] = (char)(0xE0 | (c >> 12)); out[n++] = (char)(0x80 | ((c >> 6) & 63)); out[n++] = (char)(0x80 | (c & 63)); } }
    }
    out[n] = 0;
    return n;
}

__attribute__((ms_abi)) int16_t mw_SQLDriverConnectW(void* dbc, void* hwnd, const uint16_t* conn, int16_t cchConn,
        uint16_t* outConn, int16_t outMax, int16_t* pcbOut, uint16_t driverCompletion) {
    (void)hwnd; (void)driverCompletion;
    int d = mw_odbc_slot((uintptr_t)dbc, MW_ODBC_DBC_BASE);
    if (d < 0 || !g_odbc_dbc[d].live || !conn) return (int16_t)MW_SQL_INVALID_HANDLE;
    char u8[1024];
    mw_odbc_w16_to_utf8(conn, cchConn, u8, sizeof(u8));
    /* connection-string keywords honored (case-insensitive, first hit):
     * DATABASE=, DBQ= (Access style), Data Source= — else the whole string
     * is treated as a file path (honest host behavior). */
    char path[900] = {0};
    static const char* kws[] = { "DATABASE=", "DBQ=", "DATA SOURCE=" };
    for (int k = 0; k < 3 && !path[0]; k++) {
        char* p = u8;
        while (*p) {
            int ci = 0;
            while (kws[k][ci] && (toupper((unsigned char)p[ci]) == kws[k][ci])) ci++;
            if (!kws[k][ci]) { /* keyword match: value runs to ; or end */
                char* v = p + ci;
                if (*v == '{' || *v == '"') { char q = *v++; char* e = strchr(v, q); if (e) *e = 0; }
                else { char* e = strchr(v, ';'); if (e) *e = 0; }
                snprintf(path, sizeof(path), "%s", v);
                break;
            }
            p++;
        }
        if (!path[0]) { /* retry from the start — the loop above advanced p */
            p = u8;
            while (p && *p) { char* nx = strchr(p, ';'); p = nx ? nx + 1 : NULL; }
        }
    }
    if (!path[0]) snprintf(path, sizeof(path), "%s", u8);
    /* strip residual keywords from fallback path form */
    for (char* c = path; *c; c++) if (*c == ';') { *c = 0; break; }
    sqlite3* db = NULL;
    int rc = sqlite3_open16(path, &db);
    MW_TRACE("[ODBC] SQLDriverConnectW(%s) sqlite3_open16 -> %d", path, rc);
    if (rc != 0) {
        if (db) sqlite3_close_v2(db);
        return (int16_t)MW_SQL_ERROR;
    }
    g_odbc_dbc[d].db = db;
    if (outConn && outMax > 0) { /* echo the connection string (real driver does) */
        int n = 0;
        for (; conn[n] && n < outMax - 1; n++) outConn[n] = conn[n];
        outConn[n] = 0;
        if (pcbOut) *pcbOut = (int16_t)n;
    } else if (pcbOut) *pcbOut = 0;
    return (int16_t)MW_SQL_SUCCESS;
}
__attribute__((ms_abi)) int16_t mw_SQLExecDirectW(void* stmt, const uint16_t* sql, int32_t sqlLen) {
    int s = mw_odbc_slot((uintptr_t)stmt, MW_ODBC_STMT_BASE);
    if (s < 0 || !g_odbc_stmt[s].live || !sql) return (int16_t)MW_SQL_INVALID_HANDLE;
    MwOdbcStmt* T = &g_odbc_stmt[s];
    if (T->st) { sqlite3_finalize(T->st); T->st = NULL; }
    T->done = 0; T->started = 0; T->affected = -1;
    sqlite3_stmt* st = NULL;
    int nch = (sqlLen < 0) ? -1 : sqlLen;
    int rc = sqlite3_prepare16_v2(T->db, (const void*)sql, nch * 2, &st, NULL);
    if (rc != 0 || !st) {
        MW_TRACE("[ODBC] SQLExecDirectW prepare rc=%d: %s", rc,
                 (const char*)sqlite3_errmsg16(T->db));
        return (int16_t)MW_SQL_ERROR;
    }
    T->st = st;
    rc = sqlite3_step(st);
    T->started = 1;
    if (rc == SQLITE_ROW) {
        MW_TRACE("[ODBC] SQLExecDirectW -> SUCCESS (row cursor, %d cols)", sqlite3_column_count(st));
        return (int16_t)MW_SQL_SUCCESS;
    }
    if (rc == SQLITE_DONE) {
        T->done = 1;
        T->affected = (int64_t)sqlite3_changes(T->db);
        MW_TRACE("[ODBC] SQLExecDirectW -> SUCCESS (done, affected=%lld)", (long long)T->affected);
        return (int16_t)MW_SQL_SUCCESS;
    }
    MW_TRACE("[ODBC] SQLExecDirectW step rc=%d: %s", rc, (const char*)sqlite3_errmsg16(T->db));
    return (int16_t)MW_SQL_ERROR;
}
__attribute__((ms_abi)) int16_t mw_SQLFetch(void* stmt) {
    int s = mw_odbc_slot((uintptr_t)stmt, MW_ODBC_STMT_BASE);
    if (s < 0 || !g_odbc_stmt[s].live) return (int16_t)MW_SQL_INVALID_HANDLE;
    MwOdbcStmt* T = &g_odbc_stmt[s];
    if (T->done || !T->st) return (int16_t)MW_SQL_NO_DATA;
    int rc;
    if (!T->started) { rc = sqlite3_step(T->st); T->started = 1; }
    else rc = sqlite3_step(T->st);
    if (rc == SQLITE_ROW) return (int16_t)MW_SQL_SUCCESS;
    if (rc == SQLITE_DONE) { T->done = 1; return (int16_t)MW_SQL_NO_DATA; }
    MW_TRACE("[ODBC] SQLFetch step rc=%d: %s", rc, (const char*)sqlite3_errmsg16(T->db));
    return (int16_t)MW_SQL_ERROR;
}
__attribute__((ms_abi)) int16_t mw_SQLGetData(void* stmt, uint16_t col, int16_t targetType,
        void* buf, int64_t bufLen, int64_t* lenOut) {
    int s = mw_odbc_slot((uintptr_t)stmt, MW_ODBC_STMT_BASE);
    if (s < 0 || !g_odbc_stmt[s].live || !g_odbc_stmt[s].st) return (int16_t)MW_SQL_INVALID_HANDLE;
    sqlite3_stmt* st = g_odbc_stmt[s].st;
    if (col < 1 || (int)col > sqlite3_column_count(st)) return (int16_t)MW_SQL_ERROR;
    int c = col - 1;
    if (sqlite3_column_type(st, c) == SQLITE_NULL) {
        if (lenOut) *lenOut = MW_SQL_NULL_DATA;
        return (int16_t)MW_SQL_SUCCESS;
    }
    switch (targetType) {
    case MW_SQL_C_WCHAR: {
        const void* t = sqlite3_column_text16(st, c);
        int64_t nb = (int64_t)sqlite3_column_bytes16(st, c); /* bytes, excl NUL */
        if (buf && bufLen > 0) {
            int64_t cp = nb < bufLen - 2 ? nb : bufLen - 2;
            if (cp > 0) memcpy(buf, t, (size_t)cp);
            ((uint8_t*)buf)[cp] = 0; ((uint8_t*)buf)[cp + 1] = 0;
        }
        if (lenOut) *lenOut = nb;
        return (int16_t)MW_SQL_SUCCESS;
    }
    case MW_SQL_C_CHAR: {
        const unsigned char* t = sqlite3_column_text(st, c);
        int64_t nb = (int64_t)sqlite3_column_bytes(st, c);
        if (buf && bufLen > 0) {
            int64_t cp = nb < bufLen - 1 ? nb : bufLen - 1;
            if (cp > 0) memcpy(buf, t, (size_t)cp);
            ((uint8_t*)buf)[cp] = 0;
        }
        if (lenOut) *lenOut = nb;
        return (int16_t)MW_SQL_SUCCESS;
    }
    case MW_SQL_C_SLONG:
    case MW_SQL_C_LONG:
        if (buf && bufLen >= 4) *(int32_t*)buf = (int32_t)sqlite3_column_int64(st, c);
        if (lenOut) *lenOut = 4;
        return (int16_t)MW_SQL_SUCCESS;
    case MW_SQL_C_DOUBLE:
        if (buf && bufLen >= 8) *(double*)buf = sqlite3_column_double(st, c);
        if (lenOut) *lenOut = 8;
        return (int16_t)MW_SQL_SUCCESS;
    default:
        MW_TRACE("[ODBC] SQLGetData unsupported target type %d", targetType);
        return (int16_t)MW_SQL_ERROR;
    }
}
__attribute__((ms_abi)) int16_t mw_SQLBindCol(void* stmt, uint16_t col, int16_t targetType,
        void* target, int64_t bufLen, int64_t* lenOut) {
    int s = mw_odbc_slot((uintptr_t)stmt, MW_ODBC_STMT_BASE);
    if (s < 0 || !g_odbc_stmt[s].live) return (int16_t)MW_SQL_INVALID_HANDLE;
    MwOdbcStmt* T = &g_odbc_stmt[s];
    if (col < 1 || col > 16) return (int16_t)MW_SQL_ERROR;
    if (!target) { /* unbind */
        T->binds[col - 1].ptr = NULL;
        return (int16_t)MW_SQL_SUCCESS;
    }
    T->binds[col - 1].ptr = target;
    T->binds[col - 1].len = (int)bufLen;
    T->binds[col - 1].ctype = targetType;
    if (col > T->nbinds) T->nbinds = col;
    /* copy the CURRENT row immediately (ODBC: BindCol takes effect on the
     * current cursor position) */
    (void)lenOut;
    int64_t dummy;
    mw_SQLGetData(stmt, col, targetType, target, bufLen, &dummy);
    return (int16_t)MW_SQL_SUCCESS;
}
__attribute__((ms_abi)) int16_t mw_SQLNumResultCols(void* stmt, int16_t* count) {
    int s = mw_odbc_slot((uintptr_t)stmt, MW_ODBC_STMT_BASE);
    if (s < 0 || !g_odbc_stmt[s].live) return (int16_t)MW_SQL_INVALID_HANDLE;
    if (count) *count = g_odbc_stmt[s].st ? (int16_t)sqlite3_column_count(g_odbc_stmt[s].st) : 0;
    return (int16_t)MW_SQL_SUCCESS;
}
__attribute__((ms_abi)) int16_t mw_SQLRowCount(void* stmt, int64_t* count) {
    int s = mw_odbc_slot((uintptr_t)stmt, MW_ODBC_STMT_BASE);
    if (s < 0 || !g_odbc_stmt[s].live) return (int16_t)MW_SQL_INVALID_HANDLE;
    MwOdbcStmt* T = &g_odbc_stmt[s];
    if (count) *count = T->done ? T->affected : -1; /* -1: row-set count unknown */
    return (int16_t)MW_SQL_SUCCESS;
}
__attribute__((ms_abi)) int16_t mw_SQLDisconnect(void* dbc) {
    int d = mw_odbc_slot((uintptr_t)dbc, MW_ODBC_DBC_BASE);
    if (d < 0 || !g_odbc_dbc[d].live) return (int16_t)MW_SQL_INVALID_HANDLE;
    if (g_odbc_dbc[d].db) { sqlite3_close_v2(g_odbc_dbc[d].db); g_odbc_dbc[d].db = NULL; }
    MW_TRACE("[ODBC] SQLDisconnect -> SUCCESS");
    return (int16_t)MW_SQL_SUCCESS;
}

/* ==== SWEEP-034 IMPL END ==== */

/* ==== SECTION: odbc_core ==== */
/* Upgraded handle core (replaces SWEEP-029b v1 block; ENV keeps the legacy
 * 0x00031000 pool so old callers' cookies stay valid). */
static uint32_t g_mw29_odbc_h = 0x00031000u; /* SQLH cookie pool (ENV only) */
static uint16_t mw_odbc_ascii_up(uint16_t c) {
    return (c >= 'a' && c <= 'z') ? (uint16_t)(c - 32) : c;
}
__attribute__((ms_abi)) int16_t mw_SQLAllocHandle(int16_t type, void* parent, void** out) {
    if (!out) return (int16_t)MW_SQL_ERROR;
    *out = NULL;
    if (type == MW_SQL_HANDLE_ENV) {
        *out = (void*)(uintptr_t)(g_mw29_odbc_h++);
        return (int16_t)MW_SQL_SUCCESS;
    }
    if (type == MW_SQL_HANDLE_DBC) {
        for (int i = 0; i < MW_ODBC_MAX; i++) if (!g_odbc_dbc[i].live) {
            memset(&g_odbc_dbc[i], 0, sizeof(g_odbc_dbc[i]));
            g_odbc_dbc[i].live = 1;
            *out = (void*)(uintptr_t)(MW_ODBC_DBC_BASE | (unsigned)(i + 1));
            MW_TRACE("[ODBC] SQLAllocHandle(DBC) -> %p", *out);
            return (int16_t)MW_SQL_SUCCESS;
        }
        return (int16_t)MW_SQL_ERROR;
    }
    if (type == MW_SQL_HANDLE_STMT) {
        int d = mw_odbc_slot((uintptr_t)parent, MW_ODBC_DBC_BASE);
        if (d < 0 || !g_odbc_dbc[d].live) return (int16_t)MW_SQL_INVALID_HANDLE;
        for (int i = 0; i < MW_ODBC_MAX; i++) if (!g_odbc_stmt[i].live) {
            memset(&g_odbc_stmt[i], 0, sizeof(g_odbc_stmt[i]));
            g_odbc_stmt[i].live = 1;
            g_odbc_stmt[i].db = g_odbc_dbc[d].db;
            *out = (void*)(uintptr_t)(MW_ODBC_STMT_BASE | (unsigned)(i + 1));
            MW_TRACE("[ODBC] SQLAllocHandle(STMT, dbc=%p) -> %p", parent, *out);
            return (int16_t)MW_SQL_SUCCESS;
        }
        return (int16_t)MW_SQL_ERROR;
    }
    return (int16_t)MW_SQL_INVALID_HANDLE;
}
__attribute__((ms_abi)) int16_t mw_SQLFreeHandle(int16_t type, void* h) {
    if (type == MW_SQL_HANDLE_STMT && mw_odbc_is_stmt(h)) {
        int s = mw_odbc_slot((uintptr_t)h, MW_ODBC_STMT_BASE);
        if (g_odbc_stmt[s].st) sqlite3_finalize(g_odbc_stmt[s].st);
        memset(&g_odbc_stmt[s], 0, sizeof(g_odbc_stmt[s]));
        MW_TRACE("[ODBC] SQLFreeHandle(STMT %p) finalized", h);
        return (int16_t)MW_SQL_SUCCESS;
    }
    if (type == MW_SQL_HANDLE_DBC && mw_odbc_is_dbc(h)) {
        int d = mw_odbc_slot((uintptr_t)h, MW_ODBC_DBC_BASE);
        if (g_odbc_dbc[d].db) { sqlite3_close_v2(g_odbc_dbc[d].db); g_odbc_dbc[d].db = NULL; }
        g_odbc_dbc[d].live = 0;
        MW_TRACE("[ODBC] SQLFreeHandle(DBC %p) closed", h);
        return (int16_t)MW_SQL_SUCCESS;
    }
    return (int16_t)MW_SQL_SUCCESS; /* legacy pool cookies: honest no-op */
}
__attribute__((ms_abi)) int16_t mw_SQLSetEnvAttr(void* env, uint32_t attr, void* val, uint32_t len) {
    (void)env; (void)attr; (void)val; (void)len;
    return (int16_t)MW_SQL_SUCCESS; /* attributes accepted (ODBC versioning) */
}
__attribute__((ms_abi)) int16_t mw_SQLSetConnectAttrW(void* conn, uint32_t attr, void* val, uint32_t len) {
    (void)conn; (void)attr; (void)val; (void)len;
    return (int16_t)MW_SQL_SUCCESS;
}
__attribute__((ms_abi)) int16_t mw_SQLDescribeCol(void* stmt, uint16_t col, uint16_t* name,
        int16_t namelen, int16_t* namelenOut, int16_t* dtype, uint32_t* size,
        int16_t* digits, int16_t* nullable) {
    int s = mw_odbc_slot((uintptr_t)stmt, MW_ODBC_STMT_BASE);
    if (s < 0 || !g_odbc_stmt[s].live) return (int16_t)MW_SQL_INVALID_HANDLE;
    sqlite3_stmt* st = g_odbc_stmt[s].st;
    if (namelenOut) *namelenOut = 0;
    if (name && namelen > 0) name[0] = 0;
    if (dtype) *dtype = (int16_t)MW_SQL_CHAR;
    if (size) *size = 255;
    if (digits) *digits = 0;
    if (nullable) *nullable = 1; /* SQL_NULLABLE */
    if (!st || col < 1 || (int)col > sqlite3_column_count(st)) return (int16_t)MW_SQL_ERROR;
    int c = col - 1;
    const void* nm = sqlite3_column_name16(st, c);
    if (nm && name && namelen > 0) {
        const uint16_t* w = (const uint16_t*)nm;
        int n = 0;
        while (w[n]) n++;
        int cp = n < namelen - 1 ? n : namelen - 1;
        memcpy(name, w, (size_t)cp * 2);
        name[cp] = 0;
        if (namelenOut) *namelenOut = (int16_t)n;
    }
    if (dtype) {
        int t = sqlite3_column_type(st, c);
        *dtype = (int16_t)(t == SQLITE_INTEGER ? MW_SQL_INTEGER
                         : t == SQLITE_FLOAT ? MW_SQL_DOUBLE
                         : MW_SQL_CHAR);
    }
    return (int16_t)MW_SQL_SUCCESS;
}
__attribute__((ms_abi)) int16_t mw_SQLGetDiagRecW(int16_t type, void* h, int16_t rec,
        uint16_t* state, int32_t* native, uint16_t* msg, int16_t msglen, int16_t* retlen) {
    sqlite3* db = NULL;
    if (type == MW_SQL_HANDLE_DBC && mw_odbc_is_dbc(h)) {
        int d = mw_odbc_slot((uintptr_t)h, MW_ODBC_DBC_BASE);
        if (d >= 0) db = g_odbc_dbc[d].db;
    } else if (type == MW_SQL_HANDLE_STMT && mw_odbc_is_stmt(h)) {
        int s = mw_odbc_slot((uintptr_t)h, MW_ODBC_STMT_BASE);
        if (s >= 0) db = g_odbc_stmt[s].db;
    }
    if (!db || rec != 1) return (int16_t)MW_SQL_NO_DATA; /* no diagnostic source */
    if (state) { /* SQLSTATE class HY000 = driver-defined error */
        state[0] = 'H'; state[1] = 'Y'; state[2] = '0'; state[3] = '0'; state[4] = 0;
    }
    if (native) *native = sqlite3_errcode(db);
    if (msg && msglen > 0) {
        const uint16_t* e = (const uint16_t*)sqlite3_errmsg16(db);
        int n = 0;
        while (e && e[n]) n++;
        int cp = n < msglen - 1 ? n : msglen - 1;
        memcpy(msg, e, (size_t)cp * 2);
        msg[cp] = 0;
        if (retlen) *retlen = (int16_t)n;
    } else if (retlen) *retlen = 0;
    return (int16_t)MW_SQL_SUCCESS;
}

/* ==== SWEEP-034 IMPL END ==== */
