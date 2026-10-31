#!/usr/bin/env python3
"""SWEEP-078: copy the battery tools' captured BMP frames into
evidence/sweep078/frames/<tool>/ as PNG (lossless)."""
import os, shutil, tempfile
from PIL import Image

ROOT = "/home/z/my-project/workspace/miniwin-engineering"
OUT = os.path.join(ROOT, "evidence/sweep078/frames")
MAP = {
    "cports.exe": "cports-x64",
    "FileTypesMan.exe": "filetypesman-x64",
    "RegScanner.exe": "regscanner-x86",   # battery W07 ran the x86 one last
    "shexview.exe": "shexview-x64",
    "WhatInStartup.exe": "whatinstartup-x64",
}
total = 0
for srcdir, name in MAP.items():
    d = os.path.join(ROOT, "miniwin-results", srcdir, "screenshots")
    if not os.path.isdir(d):
        continue
    od = os.path.join(OUT, name)
    os.makedirs(od, exist_ok=True)
    for f in sorted(os.listdir(d)):
        if not f.endswith(".bmp"):
            continue
        p = os.path.join(d, f)
        try:
            Image.open(p).save(os.path.join(od, f[:-4] + ".png"), optimize=True)
            total += 1
        except Exception as e:
            print("skip", p, e)
print("converted", total)
