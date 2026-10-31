#!/usr/bin/env python3
"""SWEEP-054: extract XP x64 accessory payloads from CRMPXOEM_EN.iso (AMD64 dir).
Extends sweep049 WANT list with SPIDER.EX_ (Spider Solitaire — the 5th classic
2D game for the user's 10-programs + 5-games mandate) + PBRUSH/DRWTSN32 extras."""
import sys, os
import pycdlib

ISO = sys.argv[1] if len(sys.argv) > 1 else "artifacts/sweep050/stage/xp_x64.iso"
OUTDIR = sys.argv[2] if len(sys.argv) > 2 else "artifacts/sweep054/xp64_raw"
WANT = ["CALC.EX_", "MSPAINT.EX_", "WINMINE.EX_", "SOL.EX_", "CARDS.DL_",
        "FREECELL.EX_", "MSHEARTS.EX_", "SPIDER.EX_", "NOTEPAD.EX_", "REGEDIT.EX_",
        "TASKMGR.EX_", "CHARMAP.EX_", "CLIPBRD.EX_", "WRITE.EX_", "WORDPAD.EX_",
        "MFC42U.DL_", "RICHED20.DL_", "TASKMAN.EX_", "DRWTSN32.EX_", "PING.EX_",
        "NET.EX_", "REGEDT32.EX_", "SNDVOL32.EX_", "WINVER.EX_", "MSPAINT.CH_",
        "CALC.CH_", "WINMINE.CH_", "SPIDER.CH_", "PBRUSH.EX_"]

iso = pycdlib.PyCdlib()
iso.open(ISO)
root = iso.list_children(iso_path="/")
sub = None
for c in root:
    ident = c.file_identifier().decode("utf-8", "replace")
    if ident.upper().startswith("AMD64"):
        sub = c
if sub is None:
    print("NO AMD64 DIR; root:", [c.file_identifier().decode("utf-8", "replace") for c in root][:24])
    sys.exit(1)
kids = iso.list_children(iso_path="/AMD64")
count = 0
missing = list(WANT)
os.makedirs(OUTDIR, exist_ok=True)
seen = set()
for k in kids:
    ident = k.file_identifier().decode("utf-8", "replace")
    base = ident.split(";")[0].upper()
    if base in WANT and base not in seen:
        iso.get_file_from_iso(f"{OUTDIR}/{base.lower()}", iso_path="/AMD64/" + ident)
        print("extracted", base, os.path.getsize(f"{OUTDIR}/{base.lower()}"))
        seen.add(base)
        missing.remove(base)
        count += 1
print(f"done: {count}/{len(WANT)}")
if missing:
    print("MISSING:", ",".join(missing))
