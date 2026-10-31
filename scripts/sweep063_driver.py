#!/usr/bin/env python3
# SWEEP-063 driver: hours -> minutes. Runs Ghidra headless analysis over the
# corpus via the pyghidra API (no CLI/TTY pitfalls) and exports:
#   functions.tsv / imports.tsv / exports.tsv / entrypoints.txt /
#   resources.tsv / strings.txt / decomp/<addr>_<fn>.c
# usage: sweep063_driver.py SPEC.json   (list of {binary,out,targets[]})
import json
import os
import sys
import time

import pyghidra

GHIDRA = "/home/z/my-project/tools/ghidra_12.2_PUBLIC"


def w(path, lines):
    with open(path, "w") as f:
        for ln in lines:
            f.write(ln + "\n")
    print("WROTE %s (%d lines)" % (path, len(lines)))


def export_program(prog, out_dir, targets):
    from ghidra.app.decompiler import DecompInterface, DecompileOptions
    from ghidra.util.task import ConsoleTaskMonitor

    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(os.path.join(out_dir, "decomp"), exist_ok=True)
    fm = prog.getFunctionManager()
    st = prog.getSymbolTable()
    listing = prog.getListing()
    base = prog.getImageBase()
    pname = prog.getName().replace(".", "_")

    # functions
    lines = ["name\tentry\tsize\tthunk\tconvention\tnparams"]
    for fn in fm.getFunctions(True):
        lines.append("%s\t%s\t%d\t%s\t%s\t%d" % (
            fn.getName(), fn.getEntryPoint(), fn.getBody().getNumAddresses(),
            "y" if fn.isThunk() else "n", fn.getCallingConventionName(),
            fn.getParameterCount()))
    w(os.path.join(out_dir, "functions.tsv"), lines)

    # imports (ordinal evidence for SW-1204)
    lines = ["library\tname\tordinal_flag\taddress"]
    for sym in st.getExternalSymbols():
        lib = sym.getParentNamespace().getName() if sym.getParentNamespace() else ""
        nm = sym.getName()
        flag = "ordinal" if nm.lower().startswith("ordinal") or nm.isdigit() else ""
        lines.append("%s\t%s\t%s\t%s" % (lib, nm, flag, sym.getAddress()))
    w(os.path.join(out_dir, "imports.tsv"), lines)

    # exports
    lines = ["address\tname"]
    for addr in st.getExternalEntryPointIterator():
        sym = st.getPrimarySymbol(addr)
        lines.append("%s\t%s" % (addr, sym.getName() if sym else "?"))
    w(os.path.join(out_dir, "exports.tsv"), lines)

    # entry points incl TLS (SW-1293 PE32 checklist)
    w(os.path.join(out_dir, "entrypoints.txt"),
      [str(a) for a in st.getExternalEntryPointIterator()])

    # resources (.rsrc defined data — winmine bombs audit)
    lines = ["address\ttype\tname\tsize"]
    for block in prog.getMemory().getBlocks():
        if ".rsrc" not in block.getName().lower():
            continue
        addr = block.getStart()
        end = block.getEnd()
        while addr is not None and addr.compareTo(end) <= 0:
            data = listing.getDataAt(addr)
            if data is not None:
                lines.append("%s\t%s\t%s\t%d" % (
                    addr, str(data.getDataType().getName()).replace("\t", " "),
                    (data.getLabel() or "").replace("\t", " "),
                    data.getLength()))
                addr = addr.add(data.getLength())
            else:
                addr = addr.add(1)
    w(os.path.join(out_dir, "resources.tsv"), lines)

    # strings (capped)
    lines = ["address\tvalue"]
    n = 0
    for data in listing.getDefinedData(True):
        if n >= 500:
            break
        t = str(data.getDataType().getName())
        if "string" in t.lower():
            v = str(data.getValue()).replace("\t", " ").replace("\n", " ")
            if len(v) >= 4:
                lines.append("%s\t%s" % (data.getAddress(), v[:200]))
                n += 1
    w(os.path.join(out_dir, "strings.txt"), lines)

    # decompile requested targets
    ifc = DecompInterface()
    ifc.setOptions(DecompileOptions())
    ifc.openProgram(prog)
    mon = ConsoleTaskMonitor()
    af = prog.getAddressFactory()
    sp = af.getDefaultAddressSpace()
    hits = []
    for t in targets:
        v = int(t, 16)
        cands = [v]
        if 0x100000000 <= v < 0x200000000:
            cands.append(base.getOffset() + (v - 0x100000000))
        hit = None
        last_exc = None
        for c in cands:
            try:
                addr = sp.getAddress(c)
                fn = fm.getFunctionContaining(addr)
                if fn is None:
                    fn = fm.getFunctionAt(addr)
                if fn is not None:
                    hit = fn
                    break
            except Exception as e:
                last_exc = e
                continue
        if hit is None:
            print("DECOMP MISS %s (fm funcs=%d, last_exc=%s)" % (
                t, fm.getFunctionCount(), last_exc))
            continue
        hits.append(t)
        res = ifc.decompileFunction(hit, 90, mon)
        if res.decompileCompleted():
            code = res.getDecompiledFunction().getC()
            safe = str(hit.getEntryPoint()).replace(":", "")
            p = os.path.join(out_dir, "decomp", "%s_%s.c" % (
                safe, hit.getName().replace("/", "_")))
            w(p, code.split("\n"))
            print("DECOMP OK %s -> %s" % (t, p))
        else:
            print("DECOMP FAIL %s: %s" % (t, res.getErrorMessage()))
    return hits


def main():
    spec_path = sys.argv[1]
    with open(spec_path) as f:
        spec = json.load(f)
    t0 = time.time()
    pyghidra.start()
    print("pyghidra started (JVM up) t=%.1fs" % (time.time() - t0))
    for job in spec:
        binary = job["binary"]
        out = job["out"]
        targets = job.get("targets", [])
        b0 = time.time()
        print("=== ANALYZE %s targets=%s" % (binary, targets))
        try:
            with pyghidra.open_program(binary, project_location="/home/z/my-project/tmp/ghidraproj",
                                       project_name="sweep063", analyze=True) as flat:
                prog = flat.getCurrentProgram()
                hits = export_program(prog, out, targets)
                print("=== DONE %s in %.1fs hits=%s" % (binary, time.time() - b0, hits))
        except Exception as e:
            print("=== FAIL %s: %s" % (binary, e))
    print("TOTAL %.1fs" % (time.time() - t0))


if __name__ == "__main__":
    main()
