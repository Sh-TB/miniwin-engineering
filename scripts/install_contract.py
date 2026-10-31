#!/usr/bin/env python3
"""install_contract.py — extract a program's install/run contract from sandbox ground truth.

Answers the two contract questions for any installed guest:
  1. WHERE do files land?          (directories created, files per root dir,
                                    extension histogram, exe/dll payloads)
  2. WHAT goes into the registry,  (registry_tree final state: hives, keys,
     and what the program READS     values; notable keys)
     to run?                        (api_trace RegOpen* / RegQuery* calls)

Usage: python3 scripts/install_contract.py <sandbox_dir> <label>
Output: <label> contract TSV on stdout (sections: FILES, REGISTRY-WRITE,
        REGISTRY-READ).
"""
import os, sys, json, re
from collections import Counter

WIN_ROOTS = [
    ("Program Files (x86)", "Program Files (x86)"),
    ("Program Files", "Program Files"),
    ("Windows", "Windows"),
    ("ProgramData", "ProgramData"),
    ("Users", "User profile (AppData/...)"),
    ("WinSxS", "WinSxS"),
    ("Temp", "Temp"),
]
API_RE = re.compile(r"\[API\]\s+(\w+)\((.*)\)")


def classify_root(rel):
    rel = rel.replace("\\", "/").strip("/")
    for prefix, label in WIN_ROOTS:
        if rel == prefix or rel.startswith(prefix + "/") or rel.startswith(prefix + "\\"):
            return label
    top = rel.split("/")[0] if "/" in rel else rel
    return top or "(drive root)"


def files_section(sb):
    log = os.path.join(sb, "install_log.tsv")
    dirs, files = Counter(), Counter()
    exts = Counter()
    notable = []
    if os.path.exists(log):
        for line in open(log, encoding="utf-8", errors="replace"):
            parts = line.rstrip("\n").split("\t")
            if not parts or not parts[0]:
                continue
            act, name = parts[0], parts[1] if len(parts) > 1 else ""
            if act in ("CreateDirectoryW", "CreateDirectoryExW"):
                dirs[name] += 1
            elif act in ("CreateFile", "CreateFileW", "CopyFileW", "MoveFileW"):
                if name.startswith("__tmp"):
                    continue
                files[name] += 1
                e = os.path.splitext(name)[1].lower() or "(none)"
                exts[e] += 1
                if e in (".exe", ".dll") and len(notable) < 30:
                    notable.append(name)
    return dirs, files, exts, notable


def reg_tree_section(sb):
    """Walk the registry_tree the loader captured: hive -> key/value counts."""
    tree = os.path.join(sb, "registry_tree")
    hives, keys, vals = Counter(), set(), 0
    notable_values = []
    if os.path.isdir(tree):
        for root, _dirs, fnames in os.walk(tree):
            rel = os.path.relpath(root, tree)
            for f in fnames:
                if f == "_REG_MANIFEST.tsv":
                    continue
                hive = rel.split(os.sep)[0] if rel != "." else "(root)"
                hives[hive] += 1
                if f.startswith("@"):
                    vals += 1
                    full = os.path.join(rel, f)
                    key = os.path.dirname(full).replace(os.sep, "\\")
                    vname = f[1:]
                    for pat in ("Uninstall", "Run", "App Paths", "ShellIntegration",
                                "WinRAR", "shell", "CLSID", "ProgramFilesDir"):
                        if pat.lower() in (key + vname).lower():
                            notable_values.append(f"{key} @ {vname}")
                            break
                else:
                    keys.add(rel.replace(os.sep, "\\"))
    return hives, len(keys), vals, notable_values[:24]


def reg_reads_section(sb):
    """Registry API calls observed while the installed program RAN."""
    reads, writes, misses = Counter(), Counter(), Counter()
    trace_dir = None
    for cand in ("miniwin-results",):
        d = os.path.join(sb, cand)
        if os.path.isdir(d):
            trace_dir = d
    if trace_dir:
        for prog in os.listdir(trace_dir):
            t = os.path.join(trace_dir, prog, "api_trace.json")
            if not os.path.exists(t):
                continue
            for line in open(t, encoding="utf-8", errors="replace"):
                m = API_RE.search(line)
                if not m:
                    continue
                api, args = m.group(1), m.group(2)
                key = args.split(",")[0].strip().strip('"')[:90]
                if api.startswith("RegOpenKey"):
                    reads[key] += 1
                    if "NOT_FOUND" in line or "miss" in line.lower():
                        misses[key] += 1
                elif api.startswith(("RegCreateKey", "RegSetValue")):
                    writes[key] += 1
                elif api.startswith("RegQueryValue"):
                    reads[key] += 1
    return reads, writes, misses


def main():
    sb, label = sys.argv[1], sys.argv[2]
    dirs, files, exts, notable = files_section(sb)
    print(f"# install contract: {label}")
    print(f"section\titem\tcount_or_value")
    print(f"FILES\tdirectories created\t{sum(dirs.values())}")
    print(f"FILES\tfiles created (non-temp)\t{sum(files.values())}")
    roots = Counter()
    for name in files:
        roots[classify_root(name)] += 1
    for r, c in roots.most_common():
        print(f"FILES\troot: {r}\t{c}")
    for e, c in exts.most_common(10):
        print(f"FILES\textension {e}\t{c}")
    for n in notable[:15]:
        print(f"FILES\tnotable payload\t{n}")
    hives, nkeys, nvals, notable_vals = reg_tree_section(sb)
    for h, c in hives.most_common():
        print(f"REGISTRY-WRITE\thive {h}: values\t{c}")
    print(f"REGISTRY-WRITE\ttotal keys\t{nkeys}")
    print(f"REGISTRY-WRITE\ttotal values\t{nvals}")
    for v in notable_vals:
        print(f"REGISTRY-WRITE\tnotable\t{v}")
    reads, writes, misses = reg_reads_section(sb)
    print(f"REGISTRY-READ\tdistinct keys opened at run\t{len(reads)}")
    for k, c in reads.most_common(14):
        print(f"REGISTRY-READ\t{c}x\t{k}")
    print(f"REGISTRY-READ\topen misses (NOT_FOUND)\t{sum(misses.values())}")

if __name__ == "__main__":
    main()
