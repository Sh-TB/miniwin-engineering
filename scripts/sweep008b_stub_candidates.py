#!/usr/bin/env python3
"""SWEEP-008b (SW-555 prerequisite, static half): named-stub identity without
runtime logging. Compute, per gate exe, the set of imports that MiniWin does
NOT implement (= resolved to mw_unresolved_import_stub, the exit-gate calls),
by diffing the PE import table against loader.c g_import_table/g_data_imports.
Also emit each import's IAT thunk VA so `rea xrefs` can find guest callers.
"""
import re, json, sys
import pefile

REPO = "/home/z/my-project/workspace/miniwin-engineering"
LOADER = f"{REPO}/src/loader/loader.c"

EXES = {
    "autoruns64": f"{REPO}/artifacts/sweep50/dl/autoruns64/Autoruns64.exe",
    "rammap64":   f"{REPO}/artifacts/sweep50/dl/rammap64/RAMMap64.exe",
    "procexp64":  f"{REPO}/artifacts/sweep50/dl/procexp64/procexp64.exe",
}

def table_entries():
    txt = open(LOADER, encoding="utf-8", errors="replace").read()
    funcs = set()
    for m in re.finditer(r'ENTRY\(\s*"([^"]+)"\s*,\s*"([^"]+)"', txt):
        funcs.add((m.group(1).lower(), m.group(2)))
    data = set()
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,', txt):
        data.add((m.group(1).lower(), m.group(2)))
    return funcs, data

def main():
    funcs, data = table_entries()
    print(f"[table] function entries={len(funcs)} data entries={len(data)}")
    out = {}
    for name, path in EXES.items():
        pe = pefile.PE(path, fast_load=False)
        base = pe.OPTIONAL_HEADER.ImageBase
        rows = []
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode().lower()
            for imp in entry.imports:
                if imp.name is None:
                    continue
                iname = imp.name.decode()
                thunk_va = imp.address  # IAT slot VA (where the guest calls through)
                in_tbl = (dll, iname) in funcs or (dll, iname) in data
                rows.append({"dll": dll, "name": iname, "iat_va": hex(thunk_va),
                             "in_table": in_tbl})
        cands = [r for r in rows if not r["in_table"]]
        out[name] = {"exe": path, "total_imports": len(rows),
                     "candidates": cands}
        print(f"\n[{name}] imports={len(rows)} STUB CANDIDATES={len(cands)}")
        for c in cands:
            print(f"  {c['dll']:28s} {c['name']:42s} IAT@{c['iat_va']}")
    with open(f"{REPO}/artifacts/sweep50/sweep008b_stub_candidates.json", "w") as f:
        json.dump(out, f, indent=1)
    print("\nwrote artifacts/sweep50/sweep008b_stub_candidates.json")

if __name__ == "__main__":
    main()
