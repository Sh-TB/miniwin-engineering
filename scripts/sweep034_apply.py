#!/usr/bin/env python3
"""SWEEP-034 splice: apply sweep034_impl.c into src/loader/loader.c.
Train 3 of 5 — every replacement must match EXACTLY ONCE."""
import sys, re

LOADER = "src/loader/loader.c"
IMPL = "scripts/sweep034_impl.c"

with open(LOADER) as f:
    src = f.read()
with open(IMPL) as f:
    impl = f.read()

def section(name):
    pat = r"/\* ==== SECTION: %s ==== \*/\n(.*?)(?=/\* ==== SECTION:|/\* ==== SWEEP-034 IMPL END ==== \*/)" % re.escape(name)
    m = re.search(pat, impl, re.S)
    if not m:
        raise SystemExit(f"impl section '{name}' not found")
    return m.group(1).rstrip() + "\n"

fails = []
def replace(old, new, label):
    global src
    n = src.count(old)
    if n != 1:
        fails.append(f"FAILED [{label}]: {n} occurrences (need exactly 1)")
        return
    src = src.replace(old, new)
    print(f"  ok: {label}")

print("== SWEEP-034 splice ==")

# ---- SW-1153: the whole v1 ODBC block -> real sqlite3 mini-driver ----
old_block = """#define MW29_SQL_SUCCESS   0u
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
}"""
new_block = (section("odbc").rstrip("\n") + "\n" + section("odbc_core").rstrip("\n"))
replace(old_block, new_block, "SW-1153-odbc-sqlite")

# ---- Makefile: link sqlite3 ----
with open("Makefile") as f:
    mk = f.read()
if "-lsqlite3" not in mk:
    old_mk = "LDFLAGS = -no-pie -ldl -lm -Wl,-Ttext-segment=0x2000000"
    if mk.count(old_mk) != 1:
        fails.append("FAILED [makefile]: LDFLAGS anchor not unique")
    else:
        mk = mk.replace(old_mk, "LDFLAGS = -no-pie -ldl -lm -lsqlite3 -Wl,-Ttext-segment=0x2000000")
        with open("Makefile", "w") as f:
            f.write(mk)
        print("  ok: Makefile -lsqlite3")

# ---- ENTRY lines for the new surface ----
replace('    ENTRY("ODBC32.DLL", "SQLSetConnectAttrW",                   mw_SQLSetConnectAttrW),',
"""    ENTRY("ODBC32.DLL", "SQLSetConnectAttrW",                   mw_SQLSetConnectAttrW),
    /* SWEEP-034 (SW-1153): real sqlite3-backed mini-driver surface */
    ENTRY("ODBC32.DLL", "SQLDriverConnectW",                    mw_SQLDriverConnectW),
    ENTRY("ODBC32.DLL", "SQLExecDirectW",                       mw_SQLExecDirectW),
    ENTRY("ODBC32.DLL", "SQLFetch",                             mw_SQLFetch),
    ENTRY("ODBC32.DLL", "SQLGetData",                           mw_SQLGetData),
    ENTRY("ODBC32.DLL", "SQLBindCol",                           mw_SQLBindCol),
    ENTRY("ODBC32.DLL", "SQLNumResultCols",                     mw_SQLNumResultCols),
    ENTRY("ODBC32.DLL", "SQLRowCount",                          mw_SQLRowCount),
    ENTRY("ODBC32.DLL", "SQLDisconnect",                        mw_SQLDisconnect),""", "ENTRY-odbc")

if fails:
    print("\n".join(fails))
    raise SystemExit(1)

with open(LOADER, "w") as f:
    f.write(src)
print("SWEEP-034 splice complete.")
