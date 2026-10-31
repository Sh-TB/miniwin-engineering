#!/usr/bin/env python3
# SWEEP-074: build the 48-target manifest (targets.tsv) — source, URL, size,
# sha256, PE machine/format/subsystem per binary. Honest BLOCKED rows included.
import os, struct, hashlib, subprocess

DL = "artifacts/sweep074/dl"
X = DL + "/x"
OUT = "evidence/sweep074/targets.tsv"
os.makedirs("evidence/sweep074", exist_ok=True)

def pe_info(path):
    try:
        d = open(path, "rb").read()
    except OSError:
        return None
    if len(d) < 0x40 or d[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if pe + 24 > len(d) or d[pe:pe+4] != b"PE\0\0":
        return ("MZ-only", "", "")
    mach = struct.unpack_from("<H", d, pe+4)[0]
    magic = struct.unpack_from("<H", d, pe+24)[0]
    subsys = struct.unpack_from("<H", d, pe+24+68)[0]
    fmt = "PE32+" if magic == 0x20b else ("PE32" if magic == 0x10b else hex(magic))
    arch = {0x8664: "x64", 0x14c: "x86"}.get(mach, hex(mach))
    return (fmt, arch, {2: "GUI", 3: "CUI"}.get(subsys, str(subsys)))

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()

rows = []
def add(tid, name, path, url, src, note):
    if path and os.path.isfile(path):
        i = pe_info(path)
        rows.append((tid, name, url, src, str(os.path.getsize(path)), sha256(path),
                     i[0] if i else "?", i[1] if i else "?", i[2] if i else "?", path, note))
    else:
        rows.append((tid, name, url, src, "0", "-", "BLOCKED", "-", "-", "", note))

PZ = "https://www.chiark.greenend.org.uk/~sgtatham/puzzles/%s.exe"
puzzles = ["blackbox","bridges","cube","dominosa","fifteen","flood","guess","lightup",
           "map","mines","netgame","pattern","pegs","range","rect","samegame",
           "signpost","slant","solo","towers"]
for i, g in enumerate(puzzles, 1):
    add("P%02d" % i, g, DL + "/" + g + ".exe", PZ % g, "sgtatham puzzles, MIT", "puzzle GUI")

UTIL = [
    ("U03","Notepad3 x86", X+"/notepad3-x86/Notepad3.exe", "github rizonesoft Notepad3 REL_7.26.602.1", "Freeware, source GPL"),
    ("U04","Notepad3 x64", X+"/notepad3-x64/Notepad3.exe", "github rizonesoft Notepad3 REL_7.26.602.1", "Freeware, source GPL"),
    ("U07","AutoHotkey x86 (U32)", X+"/ahk-v1/AutoHotkeyU32.exe", "github AutoHotkey v1.1.37.02", "GPLv2"),
    ("U08","AutoHotkey x64 (v2)", X+"/ahk-v2/AutoHotkey64.exe", "github AutoHotkey v2.0.19", "GPLv2"),
    ("U16","ZoomIt x86", DL+"/utils/zoomit32.exe", "live.sysinternals.com/ZoomIt.exe", "Sysinternals license"),
    ("U17","BgInfo x64", DL+"/utils/bginfo64.exe", "live.sysinternals.com/Bginfo64.exe", "Sysinternals license"),
    ("U18","RegScanner x86", X+"/regscanner-x86/RegScanner.exe", "nirsoft.net/utils/regscanner.zip", "Freeware"),
    ("U19","RegScanner x64", X+"/regscanner-x64/RegScanner.exe", "nirsoft.net/utils/regscanner-x64.zip", "Freeware"),
    ("U20","WhatInStartup x64", X+"/wis-x64/WhatInStartup.exe", "nirsoft.net/utils/whatinstartup-x64.zip", "Freeware"),
    ("U21","ShellMenuView x64", X+"/shmnview-x64/shmnview.exe", "nirsoft.net/utils/shmnview-x64.zip", "Freeware"),
    ("U22","ShellExView x64", X+"/shex-x64/shexview.exe", "nirsoft.net/utils/shexview-x64.zip", "Freeware"),
    ("U23","CurrPorts x64", X+"/cports-x64/cports.exe", "nirsoft.net/utils/cports-x64.zip", "Freeware"),
    ("U24","FileTypesMan x64", X+"/ftm-x64/FileTypesMan.exe", "nirsoft.net/utils/filetypesman-x64.zip", "Freeware"),
    ("U25","TcpView x64", DL+"/utils/tcpview64.exe", "live.sysinternals.com/Tcpview64.exe", "Sysinternals license"),
    ("U26","WinObj x64", DL+"/utils/winobj.exe", "live.sysinternals.com/Winobj.exe", "Sysinternals license"),
    ("U27","WinLister x64", X+"/winlister-x64/winlister.exe", "nirsoft.net/utils/winlister-x64.zip", "Freeware"),
    ("U28","Sigcheck x64 (CLI)", X+"/sigcheck/sigcheck64.exe", "download.sysinternals.com/files/Sigcheck.zip", "Sysinternals license"),
    ("R2","Strings x64 (CLI, replaces SciTE)", DL+"/utils/strings64.exe", "live.sysinternals.com/Strings.exe", "Sysinternals license"),
    ("R3","LastActivityView (replaces KeePass/WinSCP class)", X+"/lastactivityview/LastActivityView.exe", "nirsoft.net/utils/lastactivityview.zip", "Freeware"),
    ("U01","Explorer++ x86", None, "explorerplusplus.com + github derceg", "BLOCKED_DOWNLOAD (404/conn)"),
    ("U02","Explorer++ x64", None, "explorerplusplus.com + github derceg", "BLOCKED_DOWNLOAD (404/conn)"),
    ("U05","SciTE 32-bit", None, "scintilla.org", "BLOCKED_DOWNLOAD (conn refused)"),
    ("U06","SciTE x64", None, "scintilla.org", "BLOCKED_DOWNLOAD (conn refused)"),
    ("U09","KeePass 2 portable", None, "keepass.info", "BLOCKED_DOWNLOAD (HTML gate)"),
    ("U10","WinSCP portable", None, "winscp.net", "BLOCKED_DOWNLOAD (HTML gate)"),
    ("U11","HxD portable", None, "mh-nexus.de", "BLOCKED_DOWNLOAD (conn)"),
    ("U12","Resource Hacker", None, "angusj.com", "BLOCKED_DOWNLOAD (HTTP 409)"),
    ("U13","Ditto x64", None, "github sabrogden/Ditto", "BLOCKED_DOWNLOAD (404)"),
    ("U14","FastCopy x64", None, "fastcopy.jp", "BLOCKED_DOWNLOAD (conn)"),
    ("U15","FreeCommander XE", None, "freecommander.com", "BLOCKED_DOWNLOAD (conn)"),
]
for tid, name, path, url, src in UTIL:
    add(tid, name, path, url, src, "utility")

with open(OUT, "w") as f:
    f.write("id\tname\tresolved_url\tlicense_source\tsize\tsha256\tformat\tarch\tsubsystem\tpath\tnote\n")
    for r in rows:
        f.write("\t".join(r) + "\n")
print("wrote", OUT, len(rows), "rows;", sum(1 for r in rows if r[5] != "-"), "with binaries")
