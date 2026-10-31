#!/usr/bin/env python3
# SWEEP-073 phase 5: structured stub/unimplemented-API report.
# Post-processes MiniWin api_trace.json captures into a per-guest TSV:
#   guest | api | class | count | evidence
# Classes: UNRESOLVED (import-time or gate), PLACEHOLDER (typed-honest stubs).
# Only what a stub itself declares is counted - no false labels.
import sys, os, re, collections

UNRESOLVED = re.compile(r"UNRESOLVED (?:call ')?'?([A-Za-z0-9_@?]+)'?(?: \(gate\))?")
GATE_MISS = re.compile(r"\[\s*\d+\]\s*UNRESOLVED:\s*(\S+)")
PLACEHOLDER = re.compile(r"\]\s*\[API\]\s+([A-Za-z_][A-Za-z0-9_]{2,})\(")
STUBTXT = re.compile(r"typed-honest|stub|not supported|refused \(NULL\)|unsupported|-> FALSE", re.I)

def analyze(path):
    counts = collections.Counter()
    cls = {}
    ev = {}
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = UNRESOLVED.search(line) or GATE_MISS.search(line)
            if m:
                k = m.group(1)
                counts[k] += 1
                cls[k] = "UNRESOLVED"
                ev.setdefault(k, line.strip()[:160])
                continue
            m = PLACEHOLDER.search(line)
            if m and STUBTXT.search(line):
                k = m.group(1)
                counts[k] += 1
                cls.setdefault(k, "PLACEHOLDER")
                ev.setdefault(k, line.strip()[:160])
    return counts, cls, ev

def main():
    args = sys.argv[1:]
    out = None
    if args and args[-1].endswith(".tsv"):
        out = args[-1]; args = args[:-1]
    rows = []
    for p in args:
        targets = []
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                targets += [os.path.join(root, f) for f in files if f == "api_trace.json"]
        else:
            targets.append(p)
        for t in sorted(targets):
            guest = os.path.basename(os.path.dirname(t))
            try:
                counts, cls, ev = analyze(t)
            except OSError:
                continue
            for k, c in counts.most_common():
                rows.append((guest, k, cls.get(k, "IMPLEMENTED"), c, ev.get(k, "").replace("\t", " ")))
    lines = ["guest\tapi\tclass\tcount\tevidence"]
    for r in rows:
        lines.append("\t".join(str(x) for x in r))
    text = "\n".join(lines) + "\n"
    if out:
        with open(out, "w") as f:
            f.write(text)
        print("wrote", out, len(rows), "rows")
    else:
        print(text)

if __name__ == "__main__":
    main()
