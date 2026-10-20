/* ============================================================
 * SWEEP-029b — bginfo64 ODBC32 ordinal tail (9 -> 0)
 * Evidence: bginfo64 binds ODBC32 BY ORDINAL (pefile + loader trace
 * miniwin-results/api_trace.json): 9/24/31/75/136/141. Names via
 * wine-mirror odbc32.spec (2026-10-20):
 *   9 = SQLDescribeCol, 24 = SQLAllocHandle, 31 = SQLFreeHandle,
 *   75 = SQLSetEnvAttr, 136 = SQLGetDiagRecW, 141 = SQLSetConnectAttrW
 * OLEAUT32 raw ordinals from winmergeu (loader trace): 114/25/418
 *   via wine-mirror oleaut32.spec:
 *   25 = SafeArrayPutElement, 114 = VarBstrFromDate,
 *   418 = DllUnregisterServer
 * V1: typed-honest ODBC (no SQL engine) — SQL_SUCCESS on attribute/
 * handle paths, SQL_NO_DATA on diagnostics (no error source exists).
 * ============================================================ */

#define MW29_SQL_SUCCESS   0u
#define MW29_SQL_ERROR     (-1)
#define MW29_SQL_NO_DATA   100u

static uint32_t g_mw29_odbc_h = 0x00031000u; /* SQLH cookie pool */
__attribute__((ms_abi)) int16_t mw_SQLAllocHandle(int16_t type, void* parent, void** out) {
    (void)parent;
    if (!out) return (int16_t)MW29_SQL_ERROR;
    *out = (void*)(uintptr_t)(g_mw29_odbc_h++);
    return (int16_t)MW29_SQL_SUCCESS;
}
__attribute__((ms_abi)) int16_t mw_SQLFreeHandle(int16_t type, void* h) {
    (void)type; (void)h;
    return (int16_t)MW29_SQL_SUCCESS;
}
__attribute__((ms_abi)) int16_t mw_SQLSetEnvAttr(void* env, uint32_t attr, void* val, uint32_t len) {
    (void)env; (void)attr; (void)val; (void)len;
    return (int16_t)MW29_SQL_SUCCESS; /* v1: attributes accepted, not stored */
}
__attribute__((ms_abi)) int16_t mw_SQLSetConnectAttrW(void* conn, uint32_t attr, void* val, uint32_t len) {
    (void)conn; (void)attr; (void)val; (void)len;
    return (int16_t)MW29_SQL_SUCCESS; /* v1: attributes accepted, not stored */
}
__attribute__((ms_abi)) int16_t mw_SQLDescribeCol(void* stmt, uint16_t col, uint16_t* name,
        int16_t namelen, int16_t* namelenOut, int16_t* dtype, uint32_t* size,
        int16_t* digits, int16_t* nullable) {
    (void)stmt;
    if (namelenOut) *namelenOut = 0;
    if (name && namelen > 0) name[0] = 0; /* empty column name */
    if (dtype) *dtype = 1;                /* SQL_CHAR */
    if (size) *size = 0;
    if (digits) *digits = 0;
    if (nullable) *nullable = 1;          /* SQL_NULLABLE */
    (void)col;
    return (int16_t)MW29_SQL_SUCCESS;
}
__attribute__((ms_abi)) int16_t mw_SQLGetDiagRecW(int16_t type, void* h, int16_t rec,
        uint16_t* state, int32_t* native, uint16_t* msg, int16_t msglen, int16_t* retlen) {
    (void)type; (void)h; (void)state; (void)native; (void)msg; (void)msglen; (void)retlen;
    return (int16_t)MW29_SQL_NO_DATA; /* honest: no diagnostic source */
}

/* ---- OLEAUT32 raw-ordinal tail (winmergeu) ---- */
__attribute__((ms_abi)) uint32_t mw_SafeArrayPutElement(void* sa, int32_t* idx, void* data) {
    (void)sa; (void)idx; (void)data;
    return 0; /* S_OK — v1: element writes accepted, no store */
}
__attribute__((ms_abi)) uint32_t mw_VarBstrFromDate(double date, uint32_t lcid, uint32_t flags, void** bstrOut) {
    (void)lcid; (void)flags;
    if (!bstrOut) return 0x80070057u; /* E_INVALIDARG */
    char buf[64];
    snprintf(buf, sizeof buf, "%.6f", date);
    uint16_t w[64];
    size_t n = 0;
    for (; buf[n] && n < 63; n++) w[n] = (uint16_t)(uint8_t)buf[n];
    w[n] = 0;
    *bstrOut = bstr_alloc_len(w, (uint32_t)n); /* v1: invariant numeric form */
    return 0; /* S_OK */
}
__attribute__((ms_abi)) uint32_t mw_DllUnregisterServer(void) {
    return 0; /* S_OK — self-registration no-op */
}

/* ==== SWEEP-029b IMPL END ==== */
