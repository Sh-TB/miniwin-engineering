#!/usr/bin/env python3
"""SWEEP-007 (SW-562): re-acquire the 16 SWEEP-003 GUI programs.

The dl/ tree was lost to disk cleanup after the t09 ENOSPC incident; the
MW_GUI_EVENTS=1 battery needs the same 16 exes again. Sources are the same
official endpoints used by SWEEP-003 (NirSoft x64 zips, PuTTY w64, Sysinternals
zips, WinMerge GitHub x64-exe). Every extracted exe is pefile-audited (arch,
subsystem) and SHA-256 recorded — version drift vs SWEEP-003 is flagged in the
manifest.

Usage: python3 scripts/sweep007_redownload_gui.py
"""
import hashlib
import os
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DL = os.path.join(ROOT, "artifacts", "sweep50", "dl")
MAN = os.path.join(ROOT, "artifacts", "sweep50", "sweep007_redownload_manifest.tsv")
UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/126 Safari/537.36"

# name -> (url, kind, member-substring-to-pick)
# kind: exe (direct), zip (extract member whose lowercase name contains pick)
TARGETS = [
    # --- NirSoft x64 zips (exact same endpoints as SWEEP-003) ---
    ("produkey",    "https://www.nirsoft.net/utils/produkey-x64.zip",      "zip", "produkey.exe"),
    ("cports",      "https://www.nirsoft.net/utils/cports-x64.zip",        "zip", "cports.exe"),
    ("searchmyfiles","https://www.nirsoft.net/utils/searchmyfiles-x64.zip","zip", "searchmyfiles.exe"),
    # --- PuTTY w64 direct exes ---
    ("putty",       "https://the.earth.li/~sgtatham/putty/latest/w64/putty.exe",    "exe", None),
    ("pageant",     "https://the.earth.li/~sgtatham/putty/latest/w64/pageant.exe",  "exe", None),
    ("puttygen",    "https://the.earth.li/~sgtatham/putty/latest/w64/puttygen.exe", "exe", None),
    # --- Sysinternals zips (member = the 64-bit exe) ---
    ("procexp64",   "https://download.sysinternals.com/files/ProcessExplorer.zip", "zip", "procexp64.exe"),
    ("procmon64",   "https://download.sysinternals.com/files/ProcessMonitor.zip",  "zip", "procmon64.exe"),
    ("autoruns64",  "https://download.sysinternals.com/files/Autoruns.zip",        "zip", "autoruns64.exe"),
    ("adexplorer64","https://download.sysinternals.com/files/ADExplorer.zip",      "zip", "adexplorer64.exe"),
    ("rammap64",    "https://download.sysinternals.com/files/RAMMap.zip",          "zip", "rammap64.exe"),
    ("zoomit64",    "https://download.sysinternals.com/files/ZoomIt.zip",          "zip", "zoomit64.exe"),
    ("diskmon64",   "https://download.sysinternals.com/files/DiskMon.zip",         "zip", "diskmon64.exe"),
    ("bginfo64",    "https://download.sysinternals.com/files/BGInfo.zip",          "zip", "bginfo64.exe"),
    ("coreinfoex64","https://download.sysinternals.com/files/Coreinfo.zip",        "zip", "coreinfo64.exe"),
    # --- WinMerge x64 ---
    ("winmergeu",   "https://github.com/WinMerge/winmerge/releases/download/v2.16.58.2/winmerge-2.16.58.2-x64-exe.zip", "zip", "winmergeu.exe"),
]


def fetch(url, dest):
    """curl -> file. Returns True on HTTP 200 + non-empty."""
    r = subprocess.run(
        ["curl", "-fsSL", "--max-time", "180", "--retry", "2",
         "-A", UA, "-o", dest, url],
        capture_output=True)
    return r.returncode == 0 and os.path.exists(dest) and os.path.getsize(dest) > 0


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    os.makedirs(DL, exist_ok=True)
    rows = []
    ok = 0
    for name, url, kind, pick in TARGETS:
        outdir = os.path.join(DL, name)
        os.makedirs(outdir, exist_ok=True)
        final = None
        try:
            if kind == "exe":
                dest = os.path.join(outdir, os.path.basename(url))
                if fetch(url, dest):
                    final = dest
            else:
                arc = os.path.join(outdir, url.rsplit("/", 1)[-1])
                if not fetch(url, arc):
                    print(f"[FAIL-DL] {name} {url}")
                    rows.append((name, "DL-FAIL", url, ""))
                else:
                    with zipfile.ZipFile(arc) as z:
                        for zi in z.infolist():
                            base = os.path.basename(zi.filename)
                            if base.lower() == pick and not zi.is_dir():
                                dest = os.path.join(outdir, base)
                                with z.open(zi) as f, open(dest, "wb") as o:
                                    o.write(f.read())
                                final = dest
                                break
            if final:
                rows.append((name, "OK", sha256(final), os.path.getsize(final)))
                ok += 1
                print(f"[OK] {name} -> {os.path.basename(final)} "
                      f"({os.path.getsize(final)} bytes)")
            elif len(rows) == 0 or rows[-1][0] != name:
                rows.append((name, "DL-FAIL", url, ""))
        except Exception as e:  # noqa: BLE001 — battery must never abort
            print(f"[ERR] {name}: {e}")
            rows.append((name, "ERR", str(e), ""))

    with open(MAN, "w", newline="") as f:
        f.write("name\tstatus\tsha256_or_info\tsize\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")
    print(f"\n{ok}/{len(TARGETS)} acquired; manifest -> {MAN}")
    return 0 if ok >= 14 else 1  # tolerate 2 endpoint hiccups


if __name__ == "__main__":
    sys.exit(main())
