#!/usr/bin/env python3
"""SWEEP-048: parallel ranged downloader for the XP SP3 ISO (archive.org).
6 concurrent streams, 16MB chunks, resumable via .parts directory."""
import os, sys, threading, urllib.request, time

URL = sys.argv[1] if len(sys.argv) > 2 else "https://archive.org/download/en_windows_xp_professional_with_service_pack_3_x86_cd_x14-80428_202609/en_windows_xp_professional_with_service_pack_3_x86_cd_x14-80428.iso"
OUT = sys.argv[2] if len(sys.argv) > 2 else (sys.argv[1] if len(sys.argv) > 1 else "xp_sp3.iso")
NTHREADS = 6
CHUNK = 16 * 1024 * 1024

def get_size():
    req = urllib.request.Request(URL, method="HEAD")
    with urllib.request.urlopen(req, timeout=30) as r:
        return int(r.headers["Content-Length"])

def fetch_range(start, end, idx, parts, fails):
    part = f"{parts}/part{idx:04d}"
    want = end - start + 1
    if os.path.exists(part) and os.path.getsize(part) == want:
        return
    have = os.path.getsize(part) if os.path.exists(part) else 0
    for attempt in range(6):
        try:
            req = urllib.request.Request(URL, headers={"Range": f"bytes={start+have}-{end}"})
            with urllib.request.urlopen(req, timeout=120) as r, open(part, "ab") as f:
                while True:
                    buf = r.read(1024 * 512)
                    if not buf:
                        break
                    f.write(buf)
                    have += len(buf)
            if have >= want:
                # truncate to exact size in case of overshoot
                with open(part, "r+b") as f:
                    f.truncate(want)
                return
        except Exception as e:
            fails.append((idx, attempt, str(e)[:80]))
            time.sleep(2)
            have = os.path.getsize(part) if os.path.exists(part) else 0
    fails.append((idx, -1, "EXHAUSTED"))

def main():
    size = get_size()
    print(f"total {size} bytes, {size/1e6:.1f} MB")
    parts = OUT + ".parts"
    os.makedirs(parts, exist_ok=True)
    ranges = []
    i = 0
    s = 0
    while s < size:
        e = min(s + CHUNK, size) - 1
        ranges.append((s, e, i))
        s = e + 1
        i += 1
    fails = []
    threads = []
    # simple pool: NTHREADS at a time
    queue = list(ranges)
    lock = threading.Lock()
    def worker():
        while True:
            with lock:
                if not queue:
                    return
                job = queue.pop(0)
            fetch_range(job[0], job[1], job[2], parts, fails)
    t0 = time.time()
    for _ in range(NTHREADS):
        t = threading.Thread(target=worker, daemon=True)
        t.start()
        threads.append(t)
    for t in threads:
        t.join()
    # verify + assemble
    missing = [j for j in ranges if not os.path.exists(f"{parts}/part{j[2]:04d}")
               or os.path.getsize(f"{parts}/part{j[2]:04d}") != j[1] - j[0] + 1]
    if missing:
        print(f"MISSING {len(missing)} parts: {missing[:5]} ... fails={fails[:5]}")
        sys.exit(1)
    with open(OUT, "wb") as out:
        for j in ranges:
            with open(f"{parts}/part{j[2]:04d}", "rb") as f:
                while True:
                    buf = f.read(1024 * 1024)
                    if not buf:
                        break
                    out.write(buf)
    dt = time.time() - t0
    print(f"ASSEMBLED {os.path.getsize(OUT)} bytes in {dt:.0f}s ({size/dt/1e6:.1f} MB/s)")

if __name__ == "__main__":
    main()
