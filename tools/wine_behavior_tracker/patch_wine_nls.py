#!/usr/bin/env python3
"""Patch the Debian-repack wineserver64/wine64 lookup path
"/usr/share/wine/wine/nls" (unwritable on this host) to a short writable
project path of <= same length. Path strings are NUL-terminated; a shorter
replacement padded with NULs is fine."""
import shutil, sys, os

W = "/home/z/my-project/tools/wine-10.0/usr/lib/wine"
OLD = b"/usr/share/wine/wine/nls\x00"
NEW = b"/home/z/my-project/p/w\x00"  # 22 chars, fits in 24+1

os.makedirs("/home/z/my-project/p/w/nls", exist_ok=True)
src = "/home/z/my-project/tools/wine-10.0/usr/share/wine/nls"
for f in os.listdir(src):
    shutil.copy(os.path.join(src, f), "/home/z/my-project/p/w/nls/")

for exe in ["wineserver64", "wine64"]:
    p = os.path.join(W, exe)
    data = bytearray(open(p, 'rb').read())
    idx = data.find(OLD)
    if idx < 0:
        print(f"{exe}: pattern not found (already patched?)")
        continue
    data[idx:idx+len(OLD)] = NEW + b"\x00" * (len(OLD) - len(NEW))
    open(p, 'wb').write(data)
    os.chmod(p, 0o755)
    print(f"{exe}: patched @ {idx:#x}")
print("done")
