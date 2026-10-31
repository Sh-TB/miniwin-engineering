#!/usr/bin/env python3
"""SWEEP-50 phase 1: mass-download ~70 small Windows PE programs across spectra.

Spectra covered (user directive 2026-10-18 (from every spectrum)):
  - toolchain: MSVC/UCRT, MSVCRT (old), MinGW, Go-static, Rust-MSVC, PyInstaller
  - subsystem: console + GUI
  - era: 2000s msvcrt (Sysinternals/GnuWin32) .. 2026
  - arch: x64 majority + a few x86 boundary probes

Everything lands in artifacts/sweep50/dl/<name>/  (extracted exes auto-collected).
Failures are logged and skipped — the pool just needs >=50 good x64 exes.

Usage:
  python3 scripts/sweep50_download.py            # download + extract
  python3 scripts/sweep50_download.py --census   # pefile census of collected exes
"""
import os, re, sys, json, zipfile, subprocess, shutil, hashlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DL = os.path.join(ROOT, "artifacts", "sweep50", "dl")
LOG = os.path.join(ROOT, "artifacts", "sweep50", "download_log.tsv")
UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/126 Safari/537.36"

# (name, url) — fixed URLs only; GitHub "latest" resolved via API below.
FIXED = [
    # --- official single exes / tiny zips ---
    ("7zr",       "https://www.7-zip.org/a/7zr.exe"),
    ("busybox64", "http://frippery.org/busybox/busybox64.exe"),
    ("plink",     "https://the.earth.li/~sgtatham/putty/latest/w64/plink.exe"),
    ("pscp",      "https://the.earth.li/~sgtatham/putty/latest/w64/pscp.exe"),
    ("psftp",     "https://the.earth.li/~sgtatham/putty/latest/w64/psftp.exe"),
    ("puttygen",  "https://the.earth.li/~sgtatham/putty/latest/w64/puttygen.exe"),
    ("pageant",   "https://the.earth.li/~sgtatham/putty/latest/w64/pageant.exe"),
    ("putty",     "https://the.earth.li/~sgtatham/putty/latest/w64/putty.exe"),
    ("mkcert",    "https://github.com/FiloSottile/mkcert/releases/download/v1.4.4/mkcert-v1.4.4-windows-amd64.exe"),
    ("bombardier","https://github.com/codesenberg/bombardier/releases/download/v1.2.6/bombardier-windows-amd64.exe"),
    ("ytdlp",     "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"),
    ("cloudflared","https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-windows-amd64.exe"),
    # --- wimlib / dos2unix / assemblers ---
    ("wimlib",    "https://wimlib.net/downloads/wimlib-1.14.4-windows-x86_64-bin.zip"),
    ("dos2unix",  "https://downloads.sourceforge.net/project/dos2unix/dos2unix/7.5.2/dos2unix-7.5.2-win64.zip"),
    ("nasm",      "https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip"),
    ("ninja",     "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip"),
    # --- x86 boundary probes (GnuWin32, 32-bit msvcrt) ---
    ("gnugrep-x86", "https://downloads.sourceforge.net/project/gnuwin32/grep/2.5.4/grep-2.5.4-bin.zip"),
    ("gnutree-x86", "https://downloads.sourceforge.net/project/gnuwin32/tree/1.5.2.2/tree-1.5.2.2-bin.zip"),
    ("gnused-x86",  "https://downloads.sourceforge.net/project/gnuwin32/sed/4.2.1/sed-4.2.1-bin.zip"),
    # --- NirSoft (GUI, old-era) ---
    ("cports",    "https://www.nirsoft.net/utils/cports-x64.zip"),
    ("produkey",  "https://www.nirsoft.net/utils/produkey-x64.zip"),
    ("searchmyfiles", "https://www.nirsoft.net/utils/searchmyfiles-x64.zip"),
    # --- Sysinternals (old-era msvcrt; zips contain x.exe + x64.exe) ---
    ("si-strings", "https://download.sysinternals.com/files/Strings.zip"),
    ("si-du",      "https://download.sysinternals.com/files/DU.zip"),
    ("si-psinfo",  "https://download.sysinternals.com/files/PsInfo.zip"),
    ("si-whois",   "https://download.sysinternals.com/files/Whois.zip"),
    ("si-handle",  "https://download.sysinternals.com/files/Handle.zip"),
    ("si-accesschk","https://download.sysinternals.com/files/AccessChk.zip"),
    ("si-listdlls","https://download.sysinternals.com/files/ListDLLs.zip"),
    ("si-logonsessions","https://download.sysinternals.com/files/LogonSessions.zip"),
    ("si-sigcheck","https://download.sysinternals.com/files/Sigcheck.zip"),
    ("si-coreinfo","https://download.sysinternals.com/files/Coreinfo.zip"),
    ("si-psexec",  "https://download.sysinternals.com/files/PsExec.zip"),
    ("si-psping",  "https://download.sysinternals.com/files/PsPing.zip"),
    ("si-volumeid","https://download.sysinternals.com/files/VolumeId.zip"),
    ("si-hex2dec", "https://download.sysinternals.com/files/hex2dec.zip"),
    ("si-testlimit","https://download.sysinternals.com/files/Testlimit.zip"),
    ("si-bginfo",  "https://download.sysinternals.com/files/BGInfo.zip"),
    ("si-tcpvcon", "https://download.sysinternals.com/files/TCPVCon.zip"),
    ("si-pipelist","https://download.sysinternals.com/files/PipeList.zip"),
    ("si-pslist",  "https://download.sysinternals.com/files/PsList.zip"),
    ("si-autologon","https://download.sysinternals.com/files/Autologon.zip"),
]

# GitHub latest-release: (name, owner/repo, asset regex, is_archive)
LATEST = [
    ("ripgrep",  "BurntSushi/ripgrep",       r"x86_64-pc-windows-msvc\.zip$"),
    ("fd",       "sharkdp/fd",               r"x86_64-pc-windows-msvc\.zip$"),
    ("bat",      "sharkdp/bat",              r"x86_64-pc-windows-msvc\.zip$"),
    ("hexyl",    "sharkdp/hexyl",            r"x86_64-pc-windows-msvc\.zip$"),
    ("dust",     "bootandy/dust",            r"x86_64-pc-windows-msvc\.zip$"),
    ("hyperfine","sharkdp/hyperfine",        r"x86_64-pc-windows-msvc\.zip$"),
    ("sd",       "chmln/sd",                 r"x86_64-pc-windows-msvc\.zip$"),
    ("procs",    "dalance/procs",            r"x86_64-windows\.zip$"),
    ("just",     "casey/just",               r"x86_64-pc-windows-msvc\.zip$"),
    ("watchexec","watchexec/watchexec",      r"x86_64-pc-windows-msvc\.zip$"),
    ("eza",      "eza-community/eza",        r"x86_64-pc-windows-msvc\.zip$"),
    ("delta",    "dandavison/delta",         r"x86_64-pc-windows-msvc\.zip$"),
    ("age",      "FiloSottile/age",          r"windows-amd64\.zip$"),
    ("caddy",    "caddyserver/caddy",        r"windows_amd64(?:_exe)?\.exe$"),
    ("gomplate", "hairyhenderson/gomplate",  r"windows-amd64.*\.exe$"),
    ("dufs",     "sigoden/dufs",             r"x86_64-pc-windows-msvc\.zip$"),
    ("restic",   "restic/restic",            r"windows_amd64\.zip$"),
    ("lego",     "go-acme/lego",             r"windows_amd64\.zip$"),
    ("frp",      "fatedier/frp",             r"windows_amd64\.zip$"),
    ("dnscrypt", "DNSCrypt/dnscrypt-proxy",  r"windows-amd64.*\.zip$"),
    ("openssh",  "PowerShell/Win32-OpenSSH", r"OpenSSH-Win64\.zip$"),
    ("ugrep",    "Genivia/ugrep",            r"windows-amd64.*\.zip$"),
    ("winmerge", "WinMerge/winmerge",        r"x64-exe\.zip$"),
    ("notepadpp","notepad-plus-plus/notepad-plus-plus", r"portable\.x64\.7z$"),
    ("xh",       "ducaale/xh",               r"x86_64-pc-windows-msvc\.zip$"),
]

def gh_api(url):
    import urllib.request, json
    headers = {"User-Agent": UA, "Accept": "application/vnd.github+json"}
    # token comes ONLY from env at invocation time — never stored in this file
    tok = os.environ.get("GITHUB_TOKEN")
    if tok:
        headers["Authorization"] = f"Bearer {tok}"
    req = urllib.request.Request(url, headers=headers)
    return json.load(urllib.request.urlopen(req, timeout=30))

def curl(url, out):
    r = subprocess.run(["curl", "-sSL", "--fail", "--retry", "2", "-m", "180",
                        "-A", UA, "-o", out, url], capture_output=True)
    return r.returncode == 0 and os.path.getsize(out) > 0

def extract(archive, dest):
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for zi in z.infolist():
                if zi.is_dir(): continue
                base = os.path.basename(zi.filename)
                if not base: continue
                with z.open(zi) as f, open(os.path.join(dest, base), "wb") as o:
                    shutil.copyfileobj(f, o)
        return True
    if archive.endswith(".7z"):
        for tool in ("7z", "7za", "7zr"):
            if shutil.which(tool):
                return subprocess.run([tool, "e", "-y", f"-o{dest}", archive],
                                      capture_output=True).returncode == 0
    if archive.endswith(".exe"):  # single exe already
        return True
    return False

def main():
    os.makedirs(DL, exist_ok=True)
    rows = []
    todo = [(n, u) for n, u in FIXED]
    # resolve latest-release assets
    for name, repo, rx in LATEST:
        try:
            rel = gh_api(f"https://api.github.com/repos/{repo}/releases/latest")
            pat = re.compile(rx)
            assets = [a for a in rel.get("assets", []) if pat.search(a["name"])]
            if assets:
                todo.append((name, assets[0]["browser_download_url"]))
                rows.append((name, "resolve", rel["tag_name"]))
            else:
                rows.append((name, "resolve", "NO-ASSET " + ",".join(a["name"] for a in rel.get("assets", [])[:6])))
        except Exception as e:
            rows.append((name, "resolve", f"ERR {e}"))

    for name, url in todo:
        d = os.path.join(DL, name)
        os.makedirs(d, exist_ok=True)
        out = os.path.join(d, url.split("/")[-1])
        if os.path.exists(out) and os.path.getsize(out) > 0:
            rows.append((name, "skip", f"exists url={url}"))
            print(f"[skip] {name}")
            continue
        ok = curl(url, out)
        status = "ok"
        if not ok:
            status = "DL-FAIL"
        else:
            if out.endswith((".zip", ".7z")):
                try:
                    status = "extract-ok" if extract(out, d) else "extract-fail"
                except Exception as e:
                    status = f"extract-err {e}"
            else:
                status = "exe-direct"
        rows.append((name, "download", f"{status} url={url}"))
        print(f"[{status}] {name}")

    with open(LOG, "w") as f:
        for r in rows:
            f.write("\t".join(r) + "\n")
    # count collected exes
    exes = []
    for dirpath, _, files in os.walk(DL):
        for fn in files:
            if fn.lower().endswith(".exe"):
                exes.append(os.path.join(dirpath, fn))
    print(f"\ncollected exes: {len(exes)}")

if __name__ == "__main__":
    main()
