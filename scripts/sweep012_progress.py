#!/usr/bin/env python3
"""SWEEP-012 progress analyzer: turn raw sweep runs into PROGRESS metrics.

For every run directory under artifacts/sweep012/runs-<tag>/:
  1. decode every [STUB] guest_rip -> exact `dll!import` (FF 15 disp32 -> IAT
     slot -> PE import map vs loader table diff, same method as SWEEP-008b)
  2. compute a per-program progress score:
       LOAD-REJECT=0  CRASH-LOADER=1  CRASH-GUEST=2  STUB-GATE=3
       RAN-SILENT=4   RAN-OUTPUT=5    HANG=4 (ran indefinitely, GUI loop etc.)
     tie-broken by stub_calls (higher = more work done), api_events, out_bytes
  3. cluster the *named* gates: which missing imports block the most programs
     (the highest-value fix list), each with evidence (call site count)

Usage:
  python3 scripts/sweep012_progress.py BASELINE
  python3 scripts/sweep012_progress.py AFTER BASELINE     # diff mode
"""
import json, os, re, struct, sys, collections

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOADER = f"{REPO}/src/loader/loader.c"

SCORE = {"LOAD-REJECT": 0, "CRASH-LOADER": 1, "CRASH-GUEST": 2,
         "STUB-GATE": 3, "RAN-SILENT": 4, "HANG": 4, "RAN-OUTPUT": 5}

def table_entries():
    txt = open(LOADER, encoding="utf-8", errors="replace").read()
    funcs = {(m.group(1).lower(), m.group(2))
             for m in re.finditer(r'ENTRY\(\s*"([^"]+)"\s*,\s*"([^"]+)"', txt)}
    data = {(m.group(1).lower(), m.group(2))
            for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,', txt)}
    return funcs, data

_FUNCS, _DATA = table_entries()

_iat_cache = {}
def iat_slot_map(exe_path):
    """slot_va -> dll!import for imports NOT implemented by the loader."""
    if exe_path in _iat_cache:
        return _iat_cache[exe_path]
    slots = {}
    try:
        import pefile
        pe = pefile.PE(exe_path, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        for entry in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
            dll = entry.dll.decode().lower()
            for imp in entry.imports:
                if imp.name is None:
                    continue
                iname = imp.name.decode()
                if (dll, iname) in _FUNCS or (dll, iname) in _DATA:
                    continue
                nm = f"{dll}!{iname}" if imp.name else f"{dll}!#{imp.ordinal}"
                slots[imp.address] = nm
    except Exception as e:
        print(f"  [warn] pefile {exe_path}: {e}")
    _iat_cache[exe_path] = slots
    return slots

def decode_stub_imports(exe_path, stderr_path):
    """Return list of import names for each [STUB] guest_rip in stderr."""
    if not os.path.exists(exe_path):
        return []
    rips = []
    for line in open(stderr_path, errors="replace"):
        if "guest_rip=" not in line:
            continue
        try:
            rips.append(int(line.split("guest_rip=")[1].split()[0], 16))
        except ValueError:
            pass
    if not rips:
        return []
    slots = iat_slot_map(exe_path)
    names = []
    try:
        import pefile
        pe = pefile.PE(exe_path, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        base = pe.OPTIONAL_HEADER.ImageBase
        raw = open(exe_path, "rb").read()
        for rip in rips:
            rva = rip - base
            try:
                off = pe.get_offset_from_rva(rva)
                disp = struct.unpack_from("<i", raw, off - 4)[0]
                opcode = raw[off - 6:off - 4]
                if opcode == b"\xff\x15":
                    names.append(slots.get(rip + disp, "??UNMAPPED??"))
                elif raw[off - 5:off - 4] == b"\xe8":
                    # BUG-069 fix (SWEEP-014): call rel32 -> FF25 jmp-thunk
                    tgt = rip + disp
                    toff = pe.get_offset_from_rva(tgt - base)
                    if raw[toff:toff + 2] == b"\xff\x25":
                        d2 = struct.unpack_from("<i", raw, toff + 2)[0]
                        names.append(slots.get(tgt + 6 + d2, "??UNMAPPED??"))
                    else:
                        names.append("??THUNK-OP??")
                else:
                    names.append("??NOT-FF15??")
            except Exception:
                names.append("??DECODE-ERR??")
    except Exception:
        names = ["??PE-ERR??"] * len(rips)
    return names

def analyze(tag):
    runs = f"{REPO}/artifacts/sweep012/runs-{tag}"
    summary = f"{runs}/summary.tsv"
    if not os.path.exists(summary):
        sys.exit(f"no summary.tsv under {runs}")
    rows = [l.rstrip("\n").split("\t") for l in open(summary)][1:]
    meta = {}
    mpath = f"{runs}/meta.tsv"
    if os.path.exists(mpath):
        for l in open(mpath).readlines()[1:]:
            n, x = l.rstrip("\n").split("\t", 1)
            meta[n] = x

    gate_counter = collections.Counter()      # import -> #programs gated
    gate_programs = collections.defaultdict(set)
    per_prog = []
    for r in rows:
        (name, code, cls, outb, stubs, _nu, _ls, aev, lastapi, to) = r
        stubs = int(stubs); aev = int(aev); outb = int(outb)
        imports = []
        if stubs > 0:
            stderr = f"{runs}/{name}/stderr.txt"
            imports = decode_stub_imports(meta.get(name, ""), stderr)
            named = [i for i in imports if not i.startswith("??")]
            uniq = set(named)
            for imp in uniq:
                gate_counter[imp] += 1
                gate_programs[imp].add(name)
        else:
            uniq = set()
        score = SCORE.get(cls, 0)
        per_prog.append({
            "name": name, "exit": int(code), "class": cls, "score": score,
            "out_bytes": outb, "stub_calls": stubs,
            "n_unres_called": len(uniq), "imports_called": sorted(uniq),
            "api_events": aev, "last_api": lastapi, "timeout": int(to),
        })

    result = {"tag": tag, "n_programs": len(per_prog), "programs": per_prog,
              "class_histogram": dict(collections.Counter(p["class"] for p in per_prog)),
              "mean_score": round(sum(p["score"] for p in per_prog) / max(1, len(per_prog)), 2),
              "gate_imports": [{"import": imp, "programs": sorted(gate_programs[imp]),
                                "n": gate_counter[imp]}
                               for imp, _ in gate_counter.most_common()]}
    with open(f"{runs}/progress.json", "w") as f:
        json.dump(result, f, indent=1)
    return result

def show(res):
    print(f"== tag={res['tag']} programs={res['n_programs']} mean_score={res['mean_score']}")
    print("   histogram:", res["class_histogram"])
    print("\n-- progress ladder (lowest first = where the work is) --")
    for p in sorted(res["programs"], key=lambda p: (p["score"], p["name"])):
        print(f"  {p['score']} {p['class']:13s} exit={p['exit']:<4} stubs={p['stub_calls']:<4} "
              f"apis={p['api_events']:<6} out={p['out_bytes']:<7} {p['name']}")
    print("\n-- TOP gate imports (named, highest value first) --")
    for g in res["gate_imports"][:25]:
        print(f"  {g['n']:2d}x  {g['import']:44s} blocks: {', '.join(g['programs'][:6])}"
              + (" ..." if len(g["programs"]) > 6 else ""))

def diff(after, before):
    bp = {p["name"]: p for p in before["programs"]}
    print(f"== PROGRESS DIFF {before['tag']} -> {after['tag']} ==")
    print(f"mean_score {before['mean_score']} -> {after['mean_score']}")
    hb, ha = before["class_histogram"], after["class_histogram"]
    for k in sorted(set(hb) | set(ha)):
        d = ha.get(k, 0) - hb.get(k, 0)
        if d:
            print(f"  {k:13s} {hb.get(k,0):3d} -> {ha.get(k,0):3d}  ({'+' if d>0 else ''}{d})")
    print("\n-- per-program deltas --")
    for p in after["programs"]:
        b = bp.get(p["name"])
        if not b:
            print(f"  NEW   {p['name']}: {p['class']} (score {p['score']})")
            continue
        if p["score"] != b["score"] or p["stub_calls"] != b["stub_calls"] \
           or p["out_bytes"] != b["out_bytes"] or p["exit"] != b["exit"]:
            print(f"  {b['class']:12s}({b['score']},exit{b['exit']},stubs{b['stub_calls']},out{b['out_bytes']})"
                  f" -> {p['class']:12s}({p['score']},exit{p['exit']},stubs{p['stub_calls']},out{p['out_bytes']})"
                  f"  {p['name']}")
    print("\n-- top gates AFTER (remaining work) --")
    for g in after["gate_imports"][:15]:
        print(f"  {g['n']:2d}x  {g['import']:44s} blocks: {', '.join(g['programs'][:6])}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    tag1 = sys.argv[1]
    r1 = analyze(tag1)
    show(r1)
    if len(sys.argv) > 2:
        r0 = analyze(sys.argv[2])
        print("\n" + "=" * 70)
        diff(r1, r0)
