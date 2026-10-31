#!/usr/bin/env python3
"""SWEEP-033 splice: apply sweep033_impl.c sections into src/loader/loader.c.
Train 2 of 5 — every replacement must match EXACTLY ONCE."""
import sys, re

LOADER = "src/loader/loader.c"
IMPL = "scripts/sweep033_impl.c"

with open(LOADER) as f:
    src = f.read()
with open(IMPL) as f:
    impl = f.read()

def section(name):
    pat = r"/\* ==== SECTION: %s ==== \*/\n(.*?)(?=/\* ==== SECTION:|/\* ==== SWEEP-033 IMPL END ==== \*/)" % re.escape(name)
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

print("== SWEEP-033 splice ==")

# ---- SW-1156: ExtFloodFill real seed fill ----
replace("""__attribute__((ms_abi)) int mw_ExtFloodFill(void* hdc, int x, int y, uint32_t col, uint32_t type) {
    (void)hdc; (void)x; (void)y; (void)col; (void)type;
    MW_TRACE("[GDI] ExtFloodFill(%d,%d) -> no-op v1", x, y);
    return 1; /* v1: no seed-fill raster; callers repaint explicitly */
}""", section("floodfill").rstrip("\n"), "SW-1156-floodfill")

# ---- SW-1102 ordinal wagons: insert before QISearch (TRAIN-1 block) ----
anchor = """__attribute__((ms_abi)) uint32_t mw_QISearch(void* punk, const void* iidtbl, const void* riid, void** out) {"""
replace(anchor, section("ordnames").rstrip("\n") + "\n" + anchor, "ordinal-wagons")

# ---- ole_ord entries (wine spec + SO/MS-Learn evidence) ----
replace("""                    { "SHLWAPI", 219, "QISearch" },
                };""", """                    { "SHLWAPI", 219, "QISearch" },
                    /* SWEEP-033 (SW-1102): procexp64 raw ordinals —
                     * SHLWAPI.176 via wine shlwapi.spec; COMCTL32.345 =
                     * TaskDialogIndirect (MS Learn Q45569/SO 73210968
                     * "ordinal 345" = TaskDialogIndirect callers);
                     * ACLUI.1 via wine aclui.spec (auto-ordinal 1).
                     * ole32.277/16 remain KNOWN-UNKNOWN (no credible
                     * name source — Rule 1: no guess-based naming). */
                    { "SHLWAPI", 176, "IUnknown_QueryService" },
                    { "COMCTL32", 345, "TaskDialogIndirect" },
                    { "ACLUI", 1, "CreateSecurityPage" },
                };""", "ole_ord-3")

# ---- ENTRY lines ----
replace('    ENTRY("COMCTL32.dll", "DefSubclassProc",                    mw_DefSubclassProc),',
"""    ENTRY("COMCTL32.dll", "DefSubclassProc",                    mw_DefSubclassProc),
    /* SWEEP-033 (SW-1102 procexp64 ordinal wagons) */
    ENTRY("COMCTL32.dll", "TaskDialogIndirect",                 mw_TaskDialogIndirect),
    ENTRY("SHLWAPI.dll", "IUnknown_QueryService",               mw_IUnknown_QueryService),
    ENTRY("ACLUI.dll", "CreateSecurityPage",                    mw_CreateSecurityPage),""", "ENTRY-ordinals")

if fails:
    print("\n".join(fails))
    raise SystemExit(1)

with open(LOADER, "w") as f:
    f.write(src)
print("SWEEP-033 splice complete.")
