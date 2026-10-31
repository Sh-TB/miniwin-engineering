#!/usr/bin/env python3
"""SWEEP-055 (SW-1273 train 2): carve the LibreOffice payload CAB out of the
re-staged MSI (sha c6298b10 byte-exact vs the SWEEP-050 pin) and verify it
against the SWEEP-053 CAB pin 71dfff0b. Pure-python OLE stream dump (olefile)."""
import olefile, hashlib, os, sys

MSI = "artifacts/sweep055/stage/LibreOffice_26.8.1.1_Win_x86-64.msi"
OUT = "artifacts/sweep055/stage/lo_payload.cab"
CAB_PIN = "71dfff0b"  # first 8 hex of the SWEEP-053 CAB sha (from WORKLOG)

ole = olefile.OleFileIO(MSI)
streams = []
for s in ole.listdir(streams=True, storages=False):
    name = "/".join(s)
    try:
        sz = ole.get_size(name)
    except Exception:
        continue
    streams.append((name, sz))
streams.sort(key=lambda t: -t[1])
# SW-1273: this MSI names its payload stream with binary UTF-16 (no "cab"
# prefix — olefile renders the name as CJK). The payload is the largest
# stream and starts with MSCF; verify the magic before carving.
cabs = []
for name, sz in streams:
    if sz < 1000000:
        continue
    with ole.openstream(name) as st:
        magic = st.read(4)
    if magic == b"MSCF":
        cabs.append((name, sz))
        print(f"MSCF stream: {name!r} {sz} bytes")
if not cabs:
    print("NO CAB STREAM — MSI layout changed")
    sys.exit(1)

name = cabs[0][0]
data = ole.openstream(name).read()
h = hashlib.sha256(data).hexdigest()
print(f"carved {name}: {len(data)} bytes sha256={h[:16]}")
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "wb") as f:
    f.write(data)
print(f"cab pin check: got {h[:8]} want {CAB_PIN} MATCH={h.startswith(CAB_PIN)}")
print(f"STAGED {OUT}")
