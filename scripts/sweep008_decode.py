#!/usr/bin/env python3
"""SWEEP-008 SW-555: decode stub guest_rip -> exact dll!import.

guest_rip is the RETURN address of an `FF 15 disp32` IAT call:
  call site: ... FF 15 <disp32> | guest_rip
  slot_va = guest_rip + disp32  (RIP-relative after the 6-byte call)
Map slot_va through the PE import table -> "dll!import".
"""
import json, os, re, struct, sys
import pefile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DL = os.path.join(REPO, "artifacts", "sweep50", "dl")
RUNS = os.path.join(REPO, "artifacts", "sweep50", "runs_stubrip")

EXES = {
    "autoruns64": os.path.join(DL, "autoruns64", "Autoruns64.exe"),
    "rammap64": os.path.join(DL, "rammap64", "RAMMap64.exe"),
    "procexp64": os.path.join(DL, "procexp64", "procexp64.exe"),
}

GUEST_RIP = re.compile(r"guest_rip=0x([0-9a-f]+)")


def build_slot_map(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    slots = {}
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        dll = entry.dll.decode(errors="replace")
        for imp in entry.imports:
            if imp.address is None:
                continue
            name = imp.name.decode(errors="replace") if imp.name else f"ord{imp.ordinal}"
            slots[imp.address] = f"{dll}!{name}"
    # also delay-load imports
    try:
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"]])
        for entry in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
            dll = entry.dll.decode(errors="replace")
            for imp in entry.imports:
                if imp.address is None:
                    continue
                name = imp.name.decode(errors="replace") if imp.name else f"ord{imp.ordinal}"
                slots[imp.address] = f"{dll}!{name}"
    except Exception:
        pass
    return slots


def decode_rip(slots, rip):
    """Read FF 15 disp32 ending at rip via in-memory scan of our own mapping table."""
    return None  # filled by caller with file bytes


def main():
    out = {}
    for name, exe in EXES.items():
        if not os.path.exists(exe):
            print(f"[skip] {name}: {exe} missing")
            continue
        pe = pefile.PE(exe, fast_load=True)
        image_base = pe.OPTIONAL_HEADER.ImageBase
        data = open(exe, "rb").read()
        slots = build_slot_map(exe)
        stderr = open(os.path.join(RUNS, name, "stderr.txt"), errors="replace").read()
        rips = [int(m, 16) for m in GUEST_RIP.findall(stderr)]
        rows = []
        for rip in rips:
            # window-scan backwards for the nearest RIP-relative IAT reference
            # (FF 15 = call [rip+d], FF 25 = jmp [rip+d], 48/4C 8B modrm=/05..3D
            # = mov r64,[rip+d] feeding an indirect call rax)
            best = None
            lo = max(0, rip - 160 - image_base)
            hi = rip - image_base
            for o in range(lo, hi):
                b = data[o:o + 7]
                if len(b) < 6:
                    continue
                slot_va = None
                if b[0] == 0xFF and b[1] in (0x15, 0x25):
                    disp = struct.unpack("<i", b[2:6])[0]
                    end = o + 6
                    slot_va = (image_base + end) + disp if end + disp >= 0 else None
                    slot_va = image_base + end + disp
                    kind = "call" if b[1] == 0x15 else "jmp"
                    if end <= rip:
                        best = (end, kind, slot_va)
                elif b[0] in (0x48, 0x4C) and b[1] == 0x8B and (b[2] & 0xC7) == 0x05:
                    disp = struct.unpack("<i", b[3:7])[0]
                    end = o + 7
                    slot_va = image_base + end + disp
                    if end <= rip:
                        best = (end, "mov", slot_va)
            if best and best[2] in slots:
                rows.append({"rip": hex(rip), "slot": hex(best[2]), "import": slots[best[2]], "via": best[1]})
            elif best:
                rows.append({"rip": hex(rip), "slot": hex(best[2]), "import": f"UNKNOWN({hex(best[2])})", "via": best[1]})
            else:
                rows.append({"rip": hex(rip), "err": "no IAT ref in window"})
        out[name] = {"image_base": hex(image_base), "n_calls": len(rips), "calls": rows}
        print(f"== {name}: {len(rips)} stub calls decoded")
        for r in rows:
            print("   ", r)
    os.makedirs(os.path.join(REPO, "artifacts", "sweep50"), exist_ok=True)
    with open(os.path.join(REPO, "artifacts", "sweep50", "sweep008_stub_callers.json"), "w") as f:
        json.dump(out, f, indent=1)
    print("saved artifacts/sweep50/sweep008_stub_callers.json")


if __name__ == "__main__":
    main()
