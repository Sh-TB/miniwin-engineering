#!/usr/bin/env python3
"""SWEEP-055 (SW-1273): re-stage the LibreOffice 26.8.1.1 x64 MSI from the
ALIVE mirror (downloadarchive .../old/26.8.1.1/... = HTTP 200; the 26.8.1.2
path 404s since SWEEP-054). Verify against the SWEEP-050 provenance pin
c6298b10 (374,870,016 B). Parallel ranged download, resumable, sha256 check."""
import os, sys, threading, urllib.request, time, hashlib

URL = ("https://downloadarchive.documentfoundation.org/libreoffice/old/26.8.1.1/"
       "win/x86_64/LibreOffice_26.8.1.1_Win_x86-64.msi")
OUT = "artifacts/sweep055/stage/LibreOffice_26.8.1.1_Win_x86-64.msi"
PIN_SHA = "c6298b10cfa1748bcbedad94c62ce771e240e5021bfd6d7a967cd571b04b5312"
NTHREADS = 6
CHUNK = 16 * 1024 * 1024

def get_size(url):
    req = urllib.request.Request(url, method="HEAD")
    req.add_header("User-Agent", "miniwin-sweep055/1.0")
    with urllib.request.urlopen(req, timeout=60) as r:
        return int(r.headers["Content-Length"])

def fetch_range(url, start, end, parts, idx, fails):
    part = f"{parts}/part{idx:04d}"
    want = end - start + 1
    if os.path.exists(part) and os.path.getsize(part) == want:
        return
    have = os.path.getsize(part) if os.path.exists(part) else 0
    for attempt in range(8):
        try:
            req = urllib.request.Request(url, headers={"Range": f"bytes={start+have}-{end}",
                                                       "User-Agent": "miniwin-sweep055/1.0"})
            with urllib.request.urlopen(req, timeout=120) as r, open(part, "ab") as f:
                while True:
                    buf = r.read(1024 * 512)
                    if not buf:
                        break
                    f.write(buf)
                    have += len(buf)
            if have >= want:
                with open(part, "r+b") as f:
                    f.truncate(want)
                return
        except Exception as e:
            fails.append((idx, attempt, str(e)[:80]))
            time.sleep(2)
            have = os.path.getsize(part) if os.path.exists(part) else 0
    if have < want:
        raise RuntimeError(f"part {idx} incomplete: {have}/{want}")

def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    size = get_size(URL)
    print(f"mirror alive, content-length={size}")
    nparts = (size + CHUNK - 1) // CHUNK
    parts = OUT + ".parts"
    os.makedirs(parts, exist_ok=True)
    threads, fails = [], []
    for i in range(nparts):
        start = i * CHUNK
        end = min(size - 1, start + CHUNK - 1)
        t = threading.Thread(target=fetch_range, args=(URL, start, end, parts, i, fails))
        t.start(); threads.append(t)
    done = 0
    while any(t.is_alive() for t in threads):
        time.sleep(5)
        done += 1
        got = sum(os.path.getsize(f"{parts}/{p}") for p in os.listdir(parts)) if os.path.isdir(parts) else 0
        print(f"  progress {got/1e6:.0f}/{size/1e6:.0f} MB ({got*100//size}%)", flush=True)
    for t in threads:
        t.join()
    if fails:
        print("range failures (retried ok):", fails[:5])
    with open(OUT, "wb") as out:
        for i in range(nparts):
            with open(f"{parts}/part{i:04d}", "rb") as f:
                while True:
                    buf = f.read(1024 * 1024)
                    if not buf:
                        break
                    out.write(buf)
    h = hashlib.sha256()
    with open(OUT, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    got = h.hexdigest()
    ok = got == PIN_SHA
    print(f"sha256={got[:16]}.. pin={PIN_SHA[:16]}.. MATCH={ok}")
    if ok:
        import shutil
        shutil.rmtree(parts, ignore_errors=True)
        print(f"STAGED {OUT} ({os.path.getsize(OUT)} bytes) — SW-1273 mirror resolved")
        return 0
    return 1

if __name__ == "__main__":
    sys.exit(main())
