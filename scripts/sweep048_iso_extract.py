#!/usr/bin/env python3
"""SWEEP-048: extract XP accessory payloads (.EX_/ .DL_) from the XP SP3 ISO i386 dir."""
import sys, os
import pycdlib

ISO = sys.argv[1] if len(sys.argv) > 1 else "xp_sp3.iso"
WANT = ["CALC.EX_", "MSPAINT.EX_", "WINMINE.EX_", "SOL.EX_", "CARDS.DL_",
        "FREECELL.EX_", "MSHEARTS.EX_", "NOTEPAD.EX_", "REGEDIT.EX_", "TASKMGR.EX_",
        "CHARMAP.EX_", "CLIPBRD.EX_", "MSPAINT.CH_", "CALC.CH_", "WINMINE.CH_"]

iso = pycdlib.PyCdlib()
iso.open(ISO)
root = iso.list_children("/")
names = [c.file_identifier().decode("utf-8", "replace") for c in root]
print("root:", names[:20])
i386 = None
for c in root:
    ident = c.file_identifier().decode("utf-8", "replace")
    if ident.upper().startswith("I386"):
        i386 = c
if i386 is None:
    print("NO I386 DIR"); sys.exit(1)
kids = iso.list_children("/I386")
count = 0
os.makedirs("out", exist_ok=True)
for k in kids:
    ident = k.file_identifier().decode("utf-8", "replace")
    base = ident.split(";")[0].upper()
    if base in WANT:
        iso.get_file_from_iso(f"out/{base.lower()}", "/I386/" + ident)
        print("extracted", base, os.path.getsize(f"out/{base.lower()}"))
        count += 1
print(f"done: {count}/{len(WANT)}")
