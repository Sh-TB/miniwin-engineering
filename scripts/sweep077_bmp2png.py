#!/usr/bin/env python3
"""Convert SWEEP-077 evidence BMPs to PNG (smaller repo footprint) and drop
the BMPs; PNG is lossless so the pixel evidence is identical."""
import os
from PIL import Image

ROOT = "/home/z/my-project/workspace/miniwin-engineering/evidence/sweep077/frames"
total = 0
for d in sorted(os.listdir(ROOT)):
    dd = os.path.join(ROOT, d)
    if not os.path.isdir(dd):
        continue
    for f in os.listdir(dd):
        if f.endswith(".bmp"):
            p = os.path.join(dd, f)
            Image.open(p).save(p[:-4] + ".png", optimize=True)
            os.remove(p)
            total += 1
print("converted", total)
