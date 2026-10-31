#!/bin/bash
# SWEEP-059: expand all downloaded archives + inventory every PE executable.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
DL=artifacts/sweep059/dl
EX=artifacts/sweep059/expanded
G=artifacts/sweep059/guests
mkdir -p "$EX" "$G"

# expand every zip with system unzip; installers (PE32) run through the
# engine's 7za (self-extract capable) when it's a CAB/SFX.
cd "$DL" || exit 1
for f in *.bin; do
    n="${f%.bin}"
    ft=$(file -b "$f")
    case "$ft" in
        Zip*)
            [ -d "$EX/$n" ] && continue
            mkdir -p "$EX/$n"
            unzip -o -qq "$f" -d "$EX/$n" 2>/dev/null
            echo "UNZIP $n rc=$?"
            ;;
        PE32*)
            # keep the raw exe as a guest directly
            mkdir -p "$EX/$n"
            cp "$f" "$EX/$n/$n.exe"
            echo "PE32 $n kept raw"
            ;;
        *)
            echo "SKIP $n ($ft)"
            ;;
    esac
done

echo "=== inventory PEs ==="
python3 - <<'EOF'
import os, struct
def pe_info(path):
    try:
        with open(path,'rb') as f: d=f.read(4096)
        if d[:2] != b'MZ': return None
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        f = open(path,'rb'); f.seek(pe); sig = f.read(4); f.close()
        if sig != b'PE\x00\x00': return None
        with open(path,'rb') as f:
            f.seek(pe+4); mach = struct.unpack('<H', f.read(2))[0]
            f.seek(pe+22); chars = struct.unpack('<H', f.read(2))[0]
        arch = 'x64' if mach==0x8664 else ('x86' if mach==0x14c else hex(mach))
        gui = 'GUI' if chars & 0x2000 else 'CLI'
        return arch, gui
    except Exception:
        return None

root = 'artifacts/sweep059/expanded'
guests = 'artifacts/sweep059/guests'
count = 0
for dirpath, _, files in os.walk(root):
    for fn in files:
        if not fn.lower().endswith(('.exe','.dll')): continue
        p = os.path.join(dirpath, fn)
        info = pe_info(p)
        if not info: continue
        count += 1
        rel = os.path.relpath(p, root)
        arch, gui = info
        print(f"{rel} [{arch} {gui}] ({os.path.getsize(p)}b)")
        # stage GUI exes as guests
        if fn.lower().endswith('.exe') and gui == 'GUI':
            gname = os.path.splitext(fn)[0]
            gd = os.path.join(guests, gname)
            os.makedirs(gd, exist_ok=True)
            dst = os.path.join(gd, fn)
            if not os.path.exists(dst):
                import shutil; shutil.copy2(p, dst)
print("TOTAL PEs:", count)
EOF
