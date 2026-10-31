#!/usr/bin/env python3
"""SWEEP-051 Train 1: scan LibreOffice MSI structure — find PE streams and CAB streams.
SW-1261: the ODF display program for miniwin real-GUI verification.
"""
import olefile, struct, sys

MSI = "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep050/stage/LibreOffice.msi"

ole = olefile.OleFileIO(MSI)
streams = ole.listdir()
print(f"total streams: {len(streams)}")

mz_streams = []
cab_streams = []
big_streams = []
for s in streams:
    name = "/".join(s)
    try:
        sz = ole.get_size(name)
    except Exception:
        sz = -1
    if name.lower().endswith(".cab") or s[0].startswith("cab"):
        cab_streams.append((name, sz))
    if sz >= 200000:
        big_streams.append((name, sz))

print(f"cab-like streams: {len(cab_streams)}")
for n, s in cab_streams[:10]:
    print(f"  CAB {n} {s}")
print(f"streams >=200KB: {len(big_streams)}")
for n, s in sorted(big_streams, key=lambda t: -t[1])[:20]:
    print(f"  BIG {n} {s}")

# check the five standard MSI tables we need for a file extraction
for tbl in ("File", "Media", "Component", "Directory"):
    p = f"!{tbl}"
    print(tbl, "present:", ole.exists(p))
ole.close()
