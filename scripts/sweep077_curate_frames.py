#!/usr/bin/env python3
"""SWEEP-077: curate frame evidence — per puzzle copy frame_001 (initial)
plus the first post-input frame that differs, into evidence/sweep077/frames/."""
import os, shutil, hashlib

BASE = "/home/z/my-project/workspace/miniwin-engineering"
DL = f"{BASE}/artifacts/sweep074/dl"
OUT = f"{BASE}/evidence/sweep077/frames"
os.makedirs(OUT, exist_ok=True)

puzzles = ["blackbox","bridges","cube","dominosa","fifteen","flood","guess",
           "lightup","map","mines","netgame","pattern","pegs","range","rect",
           "samegame","signpost","slant","solo","towers"]

manifest = []
for p in puzzles:
    d = f"{BASE}/miniwin-results/{p}.exe/screenshots"
    try:
        fs = sorted(os.listdir(d))
    except FileNotFoundError:
        manifest.append(f"{p}\tMISSING")
        continue
    h0 = hashlib.md5(open(f"{d}/{fs[0]}", "rb").read()).hexdigest()
    diff = next((f for f in fs[1:]
                 if hashlib.md5(open(f"{d}/{f}", "rb").read()).hexdigest() != h0), None)
    od = f"{OUT}/{p}"
    os.makedirs(od, exist_ok=True)
    shutil.copy(f"{d}/{fs[0]}", f"{od}/before_input.bmp")
    if diff:
        shutil.copy(f"{d}/{diff}", f"{od}/after_input.bmp")
        same = "DIFFERS" if hashlib.md5(open(f"{od}/after_input.bmp","rb").read()).hexdigest() != \
                          hashlib.md5(open(f"{od}/before_input.bmp","rb").read()).hexdigest() else "SAME"
        manifest.append(f"{p}\tbefore={fs[0]}\tafter={diff}\tverdict={same}\tframes_total={len(fs)}")
    else:
        manifest.append(f"{p}\tbefore={fs[0]}\tafter=NONE\tframes_total={len(fs)}")

with open(f"{BASE}/evidence/sweep077/frames/MANIFEST.tsv", "w") as f:
    f.write("\n".join(manifest) + "\n")
print("\n".join(manifest))
