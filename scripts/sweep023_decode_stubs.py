#!/usr/bin/env python3
"""SWEEP-023 SW-1000: name the procexp64 stub at guest_rip 0x14006c328.

Reads the [STUB] guest_rip lines from the fresh artifacts/sweep023/sweep runs,
decodes the FF 15 disp32 back to the IAT slot VA, and maps it to the exact
dll!import via sweep008b_stub_candidates.json (same contract as SWEEP-008b).
"""
import json, struct, re, sys
import pefile

REPO = "/home/z/my-project/workspace/miniwin-engineering"
CAND = json.load(open(f"{REPO}/artifacts/sweep50/sweep008b_stub_candidates.json"))

RUNS = {
    "procexp64": (f"{REPO}/artifacts/sweep023/sweep/procexp64/run1/stderr.txt",
                  f"{REPO}/artifacts/sweep50/dl/procexp64/procexp64.exe"),
    "puttygen":  (f"{REPO}/artifacts/sweep023/sweep/puttygen/run1/stderr.txt",
                  f"{REPO}/artifacts/sweep50/dl/puttygen/puttygen.exe"),
    "searchmyfiles": (f"{REPO}/artifacts/sweep023/sweep/searchmyfiles/run1/stderr.txt",
                  f"{REPO}/artifacts/sweep50/dl/searchmyfiles/SearchMyFiles.exe"),
}

out = {}
for name, (stderr_path, exe_path) in RUNS.items():
    cands = CAND.get(name, {}).get("candidates", [])
    slots = {int(c["iat_va"], 16): f"{c['dll']}!{c['name']}" for c in cands}
    pe = pefile.PE(exe_path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    raw = open(exe_path, "rb").read()
    rips = sorted(set(int(m, 16) for m in
        re.findall(r"guest_rip=0x([0-9a-f]+)", open(stderr_path).read())))
    dec = []
    for rip in rips:
        off = rip - base
        # guest_rip points AFTER the FF 15 rr rr rr rr (6 bytes); disp32 at -4
        if off < 6 or off + 4 > len(raw):
            dec.append((hex(rip), "rip-outside-image", None)); continue
        op = raw[off - 6: off - 4]
        if op != b"\xff\x15":
            dec.append((hex(rip), f"not-FF15 ({op.hex()})", None)); continue
        disp = struct.unpack("<i", raw[off - 4: off])[0]
        slot = rip + disp
        dec.append((hex(rip), hex(slot), slots.get(slot, "UNMAPPED-SLOT")))
    out[name] = dec
    print(f"== {name} ==")
    for d in dec:
        print(f"  guest_rip={d[0]} slot={d[1]} -> {d[2]}")

json.dump(out, open(f"{REPO}/artifacts/sweep023/sweep023_stub_decode.json", "w"), indent=1)
