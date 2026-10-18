#!/usr/bin/env python3
"""SWEEP-008b part 2: decode stub-call guest RIPs to exact import names.
guest_rip = address AFTER the `call qword ptr [rip+disp32]` (FF 15 ...) —
so slot_va = guest_rip + disp32 with disp32 read from file bytes at rip-4.
Map slot_va -> dll!import via the candidates JSON (SWEEP-008b part 1).
"""
import json, struct
import pefile

REPO = "/home/z/my-project/workspace/miniwin-engineering"
CAND = json.load(open(f"{REPO}/artifacts/sweep50/sweep008b_stub_candidates.json"))

RUNS = {
    "autoruns64": (f"{REPO}/artifacts/sweep50/runs_stubrip9/autoruns64/run1/stderr.txt",
                   f"{REPO}/artifacts/sweep50/dl/autoruns64/Autoruns64.exe"),
    "rammap64":   (f"{REPO}/artifacts/sweep50/runs_stubrip9/rammap64/run1/stderr.txt",
                   f"{REPO}/artifacts/sweep50/dl/rammap64/RAMMap64.exe"),
    "procexp64":  (f"{REPO}/artifacts/sweep50/runs_stubrip9/procexp64/run1/stderr.txt",
                   f"{REPO}/artifacts/sweep50/dl/procexp64/procexp64.exe"),
}

out = {}
for name, (stderr_path, exe_path) in RUNS.items():
    slots = {int(c["iat_va"], 16): f"{c['dll']}!{c['name']}"
             for c in CAND[name]["candidates"]}
    pe = pefile.PE(exe_path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    raw = open(exe_path, "rb").read()
    rips = []
    for line in open(stderr_path):
        if "guest_rip=" not in line:
            continue
        rips.append(int(line.split("guest_rip=")[1].strip(), 16))
    rows = []
    for n, rip in enumerate(rips, 1):
        rva = rip - base
        off = pe.get_offset_from_rva(rva)
        disp = struct.unpack_from("<i", raw, off - 4)[0]
        slot = rip + disp
        opcode = raw[off - 6:off - 4]
        imp = slots.get(slot, "??UNMAPPED??")
        ok = opcode == b"\xff\x15"
        rows.append({"call": n, "guest_rip": hex(rip), "opcode": opcode.hex(),
                     "ff15": ok, "iat_slot": hex(slot), "import": imp})
    out[name] = rows
    print(f"=== {name} ({len(rows)} stub calls) ===")
    for r in rows:
        mark = "" if (r["ff15"] and r["import"] != "??UNMAPPED??") else "  <-- CHECK"
        print(f"  #{r['call']:2d} rip={r['guest_rip']} {r['opcode']} "
              f"slot={r['iat_slot']} -> {r['import']}{mark}")

with open(f"{REPO}/artifacts/sweep50/sweep008b_stub_callers.json", "w") as f:
    json.dump(out, f, indent=1)
print("\nwrote artifacts/sweep50/sweep008b_stub_callers.json")
