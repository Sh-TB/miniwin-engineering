#!/usr/bin/env python3
"""SWEEP-050: re-stage the gitignored payloads (XP x64 ISO / LibreOffice 26.8.1 MSI /
Everything 1.4.1.1028 x64 zip) with the provenance sha-pins from evidence/sweep049.
Parallel ranged download (6 streams, 16MB chunks, resumable), sha256 verify at end."""
import os, sys, threading, urllib.request, time, hashlib

JOBS = [
    # (name, url, out, expected_sha256, expected_size)
    ("xp_x64.iso",
     "https://archive.org/download/crmpxoem-en/CRMPXOEM_EN.iso",
     "artifacts/sweep050/stage/xp_x64.iso",
     "19b5a382bbe1c89d62a4c56da5470b9400039ca2591bf252a0e5e56f30ff4033",
     628617216),
    ("LibreOffice.msi",
     "https://downloadarchive.documentfoundation.org/libreoffice/old/26.8.1.1/win/x86_64/LibreOffice_26.8.1.1_Win_x86-64.msi",
     "artifacts/sweep050/stage/LibreOffice.msi",
     "c6298b10cfa1748bcbedad94c62ce771e240e5021bfd6d7a967cd571b04b5312",
     None),
    ("Everything.zip",
     "https://www.voidtools.com/Everything-1.4.1.1028.x64.zip",
     "artifacts/sweep050/stage/Everything-1.4.1.1028.x64.zip",
     "4f4a9b9299b608d48b065f95fd80b9c095cdc7114cd467cd9b54944b23559dfe",
     None),
]

NTHREADS = 6
CHUNK = 16 * 1024 * 1024

def get_size(url):
    req = urllib.request.Request(url, method="HEAD")
    req.add_header("User-Agent", "miniwin-sweep050/1.0")
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
                                                       "User-Agent": "miniwin-sweep050/1.0"})
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
        raise RuntimeError(f"part{idx:04d} incomplete {have}/{want}")

def download(name, url, out, want_sha, want_size):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if os.path.exists(out):
        h = hashlib.sha256()
        with open(out, "rb") as f:
            for b in iter(lambda: f.read(1 << 20), b""):
                h.update(b)
        if h.hexdigest() == want_sha:
            print(f"[{name}] ALREADY STAGED sha OK", flush=True)
            return True
        os.remove(out)
    size = get_size(url)
    if want_size and size != want_size:
        print(f"[{name}] SIZE MISMATCH remote={size} expected={want_size}", flush=True)
        return False
    nparts = (size + CHUNK - 1) // CHUNK
    parts = out + ".parts"
    os.makedirs(parts, exist_ok=True)
    fails = []
    threads = []
    for idx in range(nparts):
        start = idx * CHUNK
        end = min(size, start + CHUNK) - 1
        t = threading.Thread(target=fetch_range, args=(url, start, end, parts, idx, fails))
        t.start()
        threads.append(t)
        while len([t for t in threads if t.is_alive()]) >= NTHREADS:
            time.sleep(0.2)
    for t in threads:
        t.join()
    if fails:
        print(f"[{name}] {len(fails)} retries occurred; last={fails[-1]}", flush=True)
    with open(out, "wb") as o:
        for idx in range(nparts):
            with open(f"{parts}/part{idx:04d}", "rb") as p:
                while True:
                    buf = p.read(1 << 20)
                    if not buf:
                        break
                    o.write(buf)
    h = hashlib.sha256()
    with open(out, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    got = h.hexdigest()
    ok = got == want_sha
    print(f"[{name}] sha256 {'OK' if ok else 'MISMATCH got ' + got} ({os.path.getsize(out)}B)", flush=True)
    if ok:
        for idx in range(nparts):
            os.remove(f"{parts}/part{idx:04d}")
        os.rmdir(parts)
    return ok

def main():
    results = {}
    for name, url, out, sha, size in JOBS:
        try:
            results[name] = download(name, url, out, sha, size)
        except Exception as e:
            print(f"[{name}] FAILED: {e}", flush=True)
            results[name] = False
    print("SUMMARY:", results, flush=True)
    sys.exit(0 if all(results.values()) else 1)

if __name__ == "__main__":
    main()
