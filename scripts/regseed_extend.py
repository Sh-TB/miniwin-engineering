#!/usr/bin/env python3
"""regseed_extend.py -- extend an MWREG001 registry snapshot with additional
keys/values (SW-1463, SWEEP-094).

Why: the WinRAR 7.12 first-run "setup" window polls HKCU\\Software\\WinRAR\\Setup
in a wait loop (api_trace.json: 5x RegOpenKeyExW(miss) + periodic re-reads of
General/Compression/Interface). On a real Windows machine that key EXISTS after
the installer finishes (the SFX setup phase writes its install state there).
MiniWin's install contract (evidence/sweep092/winrar_install_contract.tsv)
proved the SFX only writes 15 values (ProgramFilesDir + SFX install-location)
-- so the fixture was missing the post-install state. This tool adds it.

Format (src/loader/loader.c reg_snap_save()):
  magic "MWREG001" (8 bytes)
  then MW_REG_MAXKEY slots:
    u8 live
    if live:
      u32 path_units, path UTF-16LE (no NUL)
      u32 nvals
      per value: u32 name_units, name UTF-16LE, u32 type, u32 size, data bytes

Slots 0..6 are predefined roots (discarded on load, SW-1419) -- new keys are
appended at the first free slot after the last live one.

Usage: regseed_extend.py <snapshot_in> <snapshot_out> <spec.json>
Exit codes: 0 ok, 2 bad usage, 3 parse error.
"""
import json
import struct
import sys

MW_REG_MAXKEY = 4096  # keep in sync with loader.c (must not exceed)


def parse(path):
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:8] != b"MWREG001":
        raise ValueError("bad magic")
    off = 8
    slots = []
    for _ in range(MW_REG_MAXKEY):
        if off >= len(blob):
            break
        live = blob[off]
        off += 1
        if not live:
            slots.append(None)
            continue
        (plen,) = struct.unpack_from("<I", blob, off)
        off += 4
        path = blob[off:off + plen * 2].decode("utf-16-le")
        off += plen * 2
        (nvals,) = struct.unpack_from("<I", blob, off)
        off += 4
        vals = []
        for _ in range(nvals):
            (nlen,) = struct.unpack_from("<I", blob, off)
            off += 4
            name = blob[off:off + nlen * 2].decode("utf-16-le")
            off += nlen * 2
            vtype, vsize = struct.unpack_from("<II", blob, off)
            off += 8
            data = blob[off:off + vsize]
            off += vsize
            vals.append({"name": name, "type": vtype, "data": data})
        slots.append({"path": path, "vals": vals})
    if off != len(blob):
        raise ValueError("trailing bytes: %d != %d" % (off, len(blob)))
    return slots


def serialize(slots):
    out = bytearray(b"MWREG001")
    for slot in slots[:MW_REG_MAXKEY]:
        if slot is None:
            out.append(0)
            continue
        out.append(1)
        p = slot["path"].encode("utf-16-le")
        out += struct.pack("<I", len(p) // 2)
        out += p
        out += struct.pack("<I", len(slot["vals"]))
        for v in slot["vals"]:
            n = v["name"].encode("utf-16-le")
            out += struct.pack("<I", len(n) // 2)
            out += n
            out += struct.pack("<II", v["type"], len(v["data"]))
            out += v["data"]
    return bytes(out)


def val_sz(text):
    """REG_SZ value: UTF-16LE bytes including the NUL terminator (loader
    RegQueryValueExW copies size bytes; guests expect a terminated string).
    SW-1505 (SWEEP-107): the terminator is now APPENDED — the old
    `encode("utf-16-le")` dropped it, so L"" serialized as size=0 and
    WinRAR's Setup\\CustomExt wait-loop rejected the value as invalid
    (same defect class the loader's own seed had as SW-1498: real size
    for "abc" is (3+1)*2=8, for "" it is 2)."""
    raw = text.encode("utf-16-le") + b"\x00\x00"
    return 1, raw


def val_dw(n):
    return 4, struct.pack("<I", n & 0xFFFFFFFF)


def upsert(slots, path, new_vals):
    """Insert or merge a key. Values replace by name; type/size recomputed."""
    for s in slots:
        if s is not None and s["path"].lower() == path.lower():
            byname = {v["name"].lower(): v for v in s["vals"]}
            for name, (t, data) in new_vals.items():
                if name.lower() in byname:
                    byname[name.lower()]["type"] = t
                    byname[name.lower()]["data"] = data
                else:
                    s["vals"].append({"name": name, "type": t, "data": data})
            return False
    # append at first dead slot after the last live one
    last_live = -1
    for i, s in enumerate(slots):
        if s is not None:
            last_live = i
    target = last_live + 1
    if target >= MW_REG_MAXKEY:
        raise ValueError("snapshot full")
    vals = [{"name": name, "type": t, "data": d} for name, (t, d) in new_vals.items()]
    slots.insert(target, {"path": path, "vals": vals})
    return True


def main():
    if len(sys.argv) != 4:
        sys.stderr.write(__doc__)
        return 2
    src, dst, spec_path = sys.argv[1], sys.argv[2], sys.argv[3]
    slots = parse(src)
    live_before = sum(1 for s in slots if s is not None)
    with open(spec_path, "r", encoding="utf-8") as f:
        spec = json.load(f)
    added = 0
    for entry in spec:
        path = entry["path"]
        vals = {}
        for name, kind in entry["values"].items():
            if kind["kind"] == "sz":
                vals[name] = val_sz(kind["value"])
            elif kind["kind"] == "dword":
                vals[name] = val_dw(int(kind["value"], 0))
            else:
                raise ValueError("unknown kind %r" % kind["kind"])
        if upsert(slots, path, vals):
            added += 1
        print("seeded %s (%d values)" % (path, len(vals)))
    live_after = sum(1 for s in slots if s is not None)
    blob = serialize(slots)
    with open(dst, "wb") as f:
        f.write(blob)
    print("slots live: %d -> %d (+%d new keys); wrote %d bytes -> %s"
          % (live_before, live_after, added, len(blob), dst))
    return 0


if __name__ == "__main__":
    sys.exit(main())
