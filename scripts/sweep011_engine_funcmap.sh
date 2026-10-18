#!/usr/bin/env bash
# SWEEP-011: engine function map for MINWIN_FUNC_MAP — built from the
# loader's OWN ELF symbols (built -g, no-pie => static text addresses ARE
# runtime RIPs). No Ghidra needed. Instant symbolization of engine-side
# profiler samples ([PROF] top RIPs 0x20352xx...).
set -euo pipefail
REPO="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
OUT="${2:-$REPO/artifacts/sweep011_perf/funcmaps/minwin_loader.funcs.json}"
mkdir -p "$(dirname "$OUT")"

nm --defined-only -S "$REPO/minwin_loader" \
  | awk 'NF>=4 && $3 ~ /^[tT]$/ { print $1, $2, $4 }' \
  | sort > /tmp/mw11_syms.txt

python3 - "$OUT" <<'EOF'
import json, sys
out_path = sys.argv[1]
funcs = {}
for line in open("/tmp/mw11_syms.txt"):
    parts = line.split()
    if len(parts) < 3: continue
    addr, size, name = parts[0], parts[1], parts[2]
    try:
        s = int(addr, 16); sz = int(size, 16)
    except ValueError:
        continue
    if sz <= 0: continue
    e = s + sz
    # keep the LARGEST symbol covering each start (nested labels)
    if s not in funcs or e > funcs[s]:
        funcs[s] = e
entries = [{"name": "?", "start": s, "end": e} for s, e in sorted(funcs.items())]
# second pass: attach names (re-read for ordering)
names = []
for line in open("/tmp/mw11_syms.txt"):
    parts = line.split()
    if len(parts) < 3: continue
    try:
        s = int(parts[0], 16)
    except ValueError:
        continue
    names.append((s, parts[2]))
names.sort()
byname = dict(names)
doc = {"exe": "minwin_loader", "image_base": 0x2000000,
       "functions": [{"name": byname.get(e["start"], "sub_%x" % e["start"]),
                      "start": e["start"], "end": e["end"]} for e in entries]}
with open(out_path, "w") as f:
    json.dump(doc, f, separators=(",", ":"))  # compact: loader parser-friendly
print("engine funcmap: %d functions -> %s" % (len(entries), out_path))
EOF
