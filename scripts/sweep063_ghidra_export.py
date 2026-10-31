# SWEEP-063 Ghidra headless export ("use the tools" mandate).
# @category SWEEP063
# Exports per-binary analysis artifacts that used to take hours by hand:
#   functions.tsv    - all functions (name, entry, size, thunk, convention)
#   imports.tsv      - external symbols w/ library + ordinal flag (SW-1204)
#   exports.tsv      - exported entry points
#   entrypoints.txt  - EXE entry + TLS callbacks (SW-1293 PE32 checklist)
#   resources.tsv    - defined data inside .rsrc (winmine bomb bitmaps etc.)
#   strings.txt      - capped string dump
#   decomp/<sym>.c   - decompiled C for requested addresses (SW-1292 sol chain)
# Usage: -postScript sweep063_ghidra_export.py <out_dir> [target_addr ...]
# Addresses accept raw VA, miniwin-convention 0x1_XXXXXXXX (base 0x100000000)
# or RVA; all are normalized against the program image base.
import os

from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor


def out_path(out_dir, name):
    return os.path.join(out_dir, name)


def w(path, lines):
    f = open(path, "w")
    try:
        for ln in lines:
            f.write(ln + "\n")
    finally:
        f.close()
    print("WROTE %s (%d lines)" % (path, len(lines)))


def main():
    args = getScriptArgs()
    if not args:
        print("usage: sweep063_ghidra_export.py <out_dir> [addr ...]")
        return
    out_dir = args[0]
    targets = args[1:]
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(os.path.join(out_dir, "decomp"), exist_ok=True)
    prog = currentProgram
    fm = prog.getFunctionManager()
    st = prog.getSymbolTable()
    listing = prog.getListing()
    base = prog.getImageBase()
    mon = ConsoleTaskMonitor()
    pname = prog.getName().replace(".", "_")
    print("== program %s base=%s lang=%s" % (pname, base, prog.getLanguage().getLanguageID()))

    # ---------- functions ----------
    lines = ["name\tentry\tsize\tthunk\tconvention\tnparams"]
    for fn in fm.getFunctions(True):
        body = fn.getBody().getNumAddresses()
        params = fn.getParameterCount()
        lines.append("%s\t%s\t%d\t%s\t%s\t%d" % (
            fn.getName(), fn.getEntryPoint(), body,
            "y" if fn.isThunk() else "n",
            fn.getCallingConventionName(), params))
    w(out_path(out_dir, "functions.tsv"), lines)

    # ---------- imports (SW-1204 ordinal evidence) ----------
    lines = ["library\tname\tordinal_flag\taddress"]
    for sym in st.getExternalSymbols():
        lib = sym.getParentNamespace().getName() if sym.getParentNamespace() else ""
        nm = sym.getName()
        flag = "ordinal" if nm.lower().startswith("ordinal") or nm.isdigit() else ""
        lines.append("%s\t%s\t%s\t%s" % (lib, nm, flag, sym.getAddress()))
    w(out_path(out_dir, "imports.tsv"), lines)

    # ---------- exports ----------
    lines = ["address\tname"]
    for addr in prog.getExternalEntryPointIterator():
        sym = st.getPrimarySymbol(addr)
        lines.append("%s\t%s" % (addr, sym.getName() if sym else "?"))
    w(out_path(out_dir, "exports.tsv"), lines)

    # ---------- entry points incl TLS (SW-1293) ----------
    lines = ["%s" % a for a in prog.getExternalEntryPointIterator()]
    w(out_path(out_dir, "entrypoints.txt"), lines)

    # ---------- resources (.rsrc defined data; winmine bombs) ----------
    lines = ["address\ttype\tname\tsize"]
    for block in prog.getMemory().getBlocks():
        if ".rsrc" not in block.getName().lower():
            continue
        addr = block.getStart()
        end = block.getEnd()
        while addr is not None and addr.compareTo(end) <= 0:
            data = listing.getDataAt(addr)
            if data is not None:
                size = data.getLength()
                dtype = str(data.getType()).replace("\t", " ")
                nm = (data.getLabel() or "").replace("\t", " ")
                lines.append("%s\t%s\t%s\t%d" % (addr, dtype, nm, size))
                addr = addr.add(size)
            else:
                addr = addr.add(1)
    w(out_path(out_dir, "resources.tsv"), lines)

    # ---------- strings (capped) ----------
    lines = ["address\tvalue"]
    n = 0
    for data in listing.getDefinedData(True):
        if n >= 500:
            break
        t = str(data.getType())
        if "string" in t.lower() or "char" in t.lower():
            v = str(data.getValue()).replace("\t", " ").replace("\n", " ")
            if len(v) >= 4:
                lines.append("%s\t%s" % (data.getAddress(), v[:200]))
                n += 1
    w(out_path(out_dir, "strings.txt"), lines)

    # ---------- decompile targets (SW-1292 etc.) ----------
    if not targets:
        # default: entry points
        targets = []
        for a in prog.getExternalEntryPointIterator():
            fn = fm.getFunctionContaining(a)
            if fn is not None:
                targets.append(str(fn.getEntryPoint()))
    ifc = DecompInterface()
    ifc.setOptions(DecompileOptions())
    ifc.openProgram(prog)
    for t in targets:
        cands = []
        t = t.strip()
        try:
            v = int(t, 16) if not t.lower().startswith("0x") else int(t, 16)
        except ValueError:
            print("SKIP bad addr %s" % t)
            continue
        cands.append(v)
        if v > 0x100000000 and v < 0x200000000:
            cands.append(v - 0x100000000)  # miniwin convention -> raw VA
        hit = None
        for c in cands:
            try:
                addr = base.getAddressFactory().getDefaultAddressSpace().getAddress(c)
            except Exception:
                continue
            fn = fm.getFunctionContaining(addr)
            if fn is None:
                fn = fm.getFunctionAt(addr)
            if fn is not None:
                hit = fn
                break
        if hit is None:
            print("DECOMP MISS %s" % t)
            continue
        res = ifc.decompileFunction(hit, 90, mon)
        if res.decompileCompleted():
            code = res.getDecompiledFunction().getC()
            safe = str(hit.getEntryPoint()).replace(":", "")
            p = out_path(out_dir, "decomp/%s_%s.c" % (safe, hit.getName().replace("/", "_")))
            w(p, code.split("\n"))
            print("DECOMP OK %s -> %s" % (t, p))
        else:
            print("DECOMP FAIL %s: %s" % (t, res.getErrorMessage()))
    print("== DONE %s" % pname)


main()
