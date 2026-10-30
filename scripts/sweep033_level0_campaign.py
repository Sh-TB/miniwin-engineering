#!/usr/bin/env python3
"""SWEEP-033 — Level-0 Wine relay tracker over a 22-app real corpus.

User's methodology (2026-10-30): pass each famous program through a level-0
tracker under Wine, capture EVERY call, compare with MiniWin, derive the base
API set that must be implemented. This script implements exactly that:

  wine side   : wine-10.0 (WineHQ noble build), WINEDEBUG=+relay, one shared
                seeded prefix; relay stderr is the level-0 trace.
  miniwin side: ./minwin_loader <exe> in an isolated workdir; the loader's
                api_trace.json yields the resolved/unresolved import sets.
  diff        : dynamically-called-under-wine MINUS miniwin-resolved =
                the honest per-app gap; aggregated -> BASE API priorities.

Outputs land in artifacts/sweep033/ inside the repo. Raw relay logs are
deleted after parsing (disk budget); a 2 MB head sample + full stats stay.
"""
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

REPO = Path("/home/z/my-project/workspace/miniwin-engineering")
WINE = Path("/home/z/my-project/tools/wine-10.0/bin/wine64")
SEED_ZLIB = Path("/home/z/my-project/tools/wine-10.0/seeds/zlib1.dll")
ART = REPO / "artifacts" / "sweep033"
WORK = Path("/home/z/tmp/sweep033")
PREFIX = Path("/home/z/tmp/sweep033-prefix")
S50 = REPO / "artifacts" / "sweep50" / "dl"
RELAY_CAP = 60 * 1024 * 1024   # parse at most 60 MB of relay per app
EVENTS_KEEP = 60_000           # bounded events file (head)

sys.path.insert(0, str(REPO / "tools" / "wine_behavior_tracker"))
from wine_behavior_tracker.schema import EventWriter          # noqa: E402
from wine_behavior_tracker.relay_parse import parse_relay_to_events  # noqa: E402


class BoundedWriter(EventWriter):
    """EventWriter that stops persisting after EVENTS_KEEP events (stats stay)."""

    def emit(self, event_type, **fields):
        if self.sequence >= EVENTS_KEEP:
            self.dropped += 1
            return None
        return super().emit(event_type, **fields)


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


CORPUS = [
    # name, exe, argv, timeout_s, kind
    ("winrar-gui",   "/home/z/tmp/corpus/wr-out/WinRAR.exe", [], 35, "GUI-INIT"),
    ("rar-console",  "/home/z/tmp/corpus/wr-out/Rar.exe", [], 20, "CONSOLE"),
    ("unrar",        "/home/z/tmp/corpus/wr-out/UnRAR.exe", [], 20, "CONSOLE"),
    ("putty",        S50 / "putty/putty.exe", [], 25, "GUI-INIT"),
    ("puttygen",     S50 / "puttygen/puttygen.exe", [], 25, "GUI-INIT"),
    ("pageant",      S50 / "pageant/pageant.exe", [], 20, "GUI-INIT"),
    ("procexp64",    S50 / "procexp64/procexp64.exe", ["/accepteula"], 25, "GUI-INIT"),
    ("procmon64",    S50 / "procmon64/Procmon64.exe",
     ["/AcceptEula", "/Quiet", "/Minimized", "/BackingFile", "pm.pml"], 25, "GUI-INIT"),
    ("bginfo64",     S50 / "bginfo64/Bginfo64.exe",
     ["/accepteula", "/timer", "0", "/nolicprompt", "/silent"], 25, "GUI-INIT"),
    ("rammap64",     S50 / "rammap64/RAMMap64.exe", ["/accepteula"], 25, "GUI-INIT"),
    ("autoruns64",   S50 / "autoruns64/Autoruns64.exe", ["/accepteula"], 25, "GUI-INIT"),
    ("zoomit64",     S50 / "zoomit64/ZoomIt64.exe", ["/accepteula"], 20, "GUI-INIT"),
    ("produkey",     S50 / "produkey/ProduKey.exe", ["/stext", "pk.txt"], 25, "CONSOLE"),
    ("cports",       S50 / "cports/cports.exe", ["/stext", "cp.txt"], 25, "CONSOLE"),
    ("diskmon64",    S50 / "diskmon64/Diskmon64.exe", ["/accepteula"], 20, "GUI-INIT"),
    ("searchmyfiles", S50 / "searchmyfiles/SearchMyFiles.exe", ["/stext", "smf.txt"], 30, "GUI-INIT"),
    ("winmergeu",    S50 / "winmergeu/WinMergeU.exe", ["/?"], 25, "GUI-INIT"),
    ("coreinfo64",   S50 / "coreinfoex64/Coreinfo64.exe", [], 20, "CONSOLE"),
    ("7za",          REPO / "samples/real/7za.exe", [], 15, "CONSOLE"),
    ("jq",           REPO / "samples/real/jq-windows-amd64.exe", ["--version"], 15, "CONSOLE"),
    ("7z-sfx",       REPO / "samples/real/7z2604-x64.exe", [], 25, "GUI-INIT"),
    ("notepadpp",    Path("/home/z/my-project/workspace/wine-ref/npp/notepad++.exe"), [], 40, "GUI-INIT"),
]

MW_RESOLVED = re.compile(r"\[\s*\d+\]\s+(?:Ordinal\s+\d+\s+->\s+)?(\w+)\s+->\s+0x[0-9a-fA-F]+")
MW_UNRESOLVED = re.compile(r"\[\s*\d+\]\s+UNRESOLVED:\s+(\w+)")
MW_DLL = re.compile(r"Import DLL:\s+(\S+?)(?:\.dll)?\s*\(")
MW_SUMMARY = re.compile(r"Import resolution:\s+(\d+) resolved,\s+(\d+) unresolved\s*\(total (\d+)\)")


def setup_prefix():
    sys32 = PREFIX / "drive_c" / "windows" / "system32"
    if PREFIX.exists() and sys32.exists() and len(list(sys32.glob("*"))) > 100:
        return {"reused": True, "system32_files": len(list(sys32.glob("*")))}
    if PREFIX.exists():
        shutil.rmtree(PREFIX)
    env = dict(os.environ, WINEPREFIX=str(PREFIX), TMPDIR="/home/z/tmp")
    r = subprocess.run([str(WINE), "wineboot", "-i"], env=env,
                       capture_output=True, text=True, timeout=300)
    n = len(list(sys32.glob("*"))) if sys32.exists() else 0
    if SEED_ZLIB.exists() and sys32.exists():
        shutil.copy(SEED_ZLIB, sys32 / "zlib1.dll")
    return {"wineboot_rc": r.returncode, "system32_files": n}


def run_wine_app(name, exe, argv, timeout_s, workdir):
    """One level-0 relay pass. Returns record dict."""
    relay = workdir / "relay.log"
    outp = workdir / "wine.stdout.txt"
    if relay.exists():
        relay.unlink()
    env = dict(os.environ, WINEPREFIX=str(PREFIX), TMPDIR="/home/z/tmp",
               WINEDEBUG="+relay")
    cmd = [str(WINE), str(exe)] + list(argv)
    t0 = time.time()
    proc = subprocess.Popen(cmd, cwd=str(workdir), env=env,
                            stdout=open(outp, "wb"), stderr=open(relay, "wb"),
                            start_new_session=True)
    status = "OK"
    try:
        proc.wait(timeout=timeout_s)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        proc.wait()
        rc = None
        status = "TIMEOUT-KILLED"
    dur = round(time.time() - t0, 1)
    size = relay.stat().st_size if relay.exists() else 0
    rec = {"rc": rc, "status": status, "duration_s": dur, "relay_bytes": size}
    # parse
    ev_path = workdir / "events.jsonl"
    if ev_path.exists():
        ev_path.unlink()
    writer = BoundedWriter(str(ev_path), f"sweep033-{name}", "wine-relay")
    # bound parse input
    parse_src = relay
    truncated = False
    if size > RELAY_CAP:
        parse_src = workdir / "relay.head.log"
        with open(relay, "rb") as fi, open(parse_src, "wb") as fo:
            fo.write(fi.read(RELAY_CAP))
        truncated = True
    stats = parse_relay_to_events(str(parse_src), writer, f"sweep033-{name}")
    writer._fh.flush()
    rec["relay_parse"] = {k: v for k, v in stats.items()}
    rec["relay_truncated_at_cap"] = truncated
    # per-app dynamic API set — count directly from the relay text (single pass,
    # independent of the bounded events sample)
    CALL_RE = re.compile(r"^[0-9a-f]+:Call\s+(\S+?)\(.*")
    calls = Counter()
    with open(parse_src, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = CALL_RE.match(line)
            if m:
                api = m.group(1)
                api = re.sub(r"\.dll\.", ".", api, flags=re.I)
                calls[api] += 1
    rec["api_calls_distinct"] = len(calls)
    rec["api_calls_total"] = sum(calls.values())
    rec["events_bounded_at"] = EVENTS_KEEP
    (workdir / "apis.dynamic.json").write_text(
        json.dumps({"app": name, "distinct": len(calls),
                    "total": sum(calls.values()),
                    "apis": dict(calls.most_common())}, indent=1))
    # keep a 2MB sample, drop the raw log (disk budget)
    sample = workdir / "relay-sample.txt"
    with open(parse_src, "rb") as fi, open(sample, "wb") as fo:
        fo.write(fi.read(2 * 1024 * 1024))
    for p in (relay, parse_src):
        if p.exists():
            p.unlink()
    rec["events_bytes_persisted"] = 0 if not ev_path.exists() else ev_path.stat().st_size
    return rec


def parse_minwin_trace(path: Path):
    txt = path.read_text(encoding="utf-8", errors="replace")
    resolved, unresolved, dlls = set(), set(), []
    cur = None
    for line in txt.splitlines():
        m = MW_DLL.search(line)
        if m:
            cur = m.group(1).upper()
            dlls.append(cur)
            continue
        m = MW_RESOLVED.search(line)
        if m and cur and "UNRESOLVED" not in line:
            resolved.add(f"{cur}.{m.group(1)}")
            continue
        m = MW_UNRESOLVED.search(line)
        if m and cur:
            unresolved.add(f"{cur}.{m.group(1)}")
    sm = MW_SUMMARY.search(txt)
    summary = {"resolved": int(sm.group(1)), "unresolved": int(sm.group(2)),
               "total": int(sm.group(3))} if sm else None
    return resolved, unresolved, summary


def run_miniwin_app(name, exe, argv, timeout_s, workdir):
    mwdir = workdir / "mw"
    mwdir.mkdir(exist_ok=True)
    loader = REPO / "minwin_loader"
    cmd = [str(loader), str(exe)] + list(argv)
    t0 = time.time()
    try:
        r = subprocess.run(cmd, cwd=str(mwdir), capture_output=True,
                           timeout=timeout_s)
        rc, status = r.returncode, "OK"
        (mwdir / "mw.stdout.txt").write_bytes(r.stdout or b"")
        (mwdir / "mw.stderr.txt").write_bytes(r.stderr or b"")
    except subprocess.TimeoutExpired as e:
        rc, status = None, "TIMEOUT-KILLED"
        (mwdir / "mw.stdout.txt").write_bytes(e.stdout or b"")
        (mwdir / "mw.stderr.txt").write_bytes(e.stderr or b"")
    dur = round(time.time() - t0, 1)
    base = Path(exe).name
    trace = mwdir / "miniwin-results" / base / "api_trace.json"
    rec = {"rc": rc, "status": status, "duration_s": dur}
    if trace.exists():
        resolved, unresolved, summary = parse_minwin_trace(trace)
        rec["import_summary"] = summary
        rec["resolved_distinct"] = len(resolved)
        rec["unresolved_distinct"] = len(unresolved)
        rec["_resolved_set"] = sorted(resolved)
        rec["_unresolved_set"] = sorted(unresolved)
    else:
        rec["import_summary"] = None
    return rec


def main():
    only = sys.argv[1:] or None
    ART.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    campaign = {"name": "SWEEP-033 level-0 relay campaign",
                "wine": "wine-10.0 (WineHQ noble build, /home/z/my-project/tools/wine-10.0)",
                "winedebug": "+relay",
                "method": "level-0 relay capture under Wine vs miniwin loader import sets; "
                          "GUI apps are HEADLESS-INIT phase traces (no display server); "
                          "console apps are full runs",
                "prefix": None, "apps": {}}

    print("== prefix init ==", flush=True)
    campaign["prefix"] = setup_prefix()
    print(json.dumps(campaign["prefix"]), flush=True)

    for name, exe, argv, tout, kind in CORPUS:
        if only and name not in only:
            continue
        exe = Path(exe)
        if not exe.exists():
            print(f"[{name}] MISSING {exe}", flush=True)
            campaign["apps"][name] = {"error": f"missing exe {exe}"}
            continue
        wd = WORK / name
        wd.mkdir(parents=True, exist_ok=True)
        print(f"[{name}] wine {kind} argv={argv} ...", flush=True)
        w = run_wine_app(name, exe, argv, tout, wd)
        print(f"[{name}] wine: rc={w['rc']} {w['status']} "
              f"distinct={w.get('api_calls_distinct')} total={w.get('api_calls_total')} "
              f"relay={w.get('relay_bytes', 0)/1e6:.1f}MB cap={w.get('relay_truncated_at_cap')}",
              flush=True)
        print(f"[{name}] miniwin ...", flush=True)
        m = run_miniwin_app(name, exe, argv, max(tout, 40), wd)
        m.pop("_resolved_set", None)
        m.pop("_unresolved_set", None)
        print(f"[{name}] miniwin: rc={m['rc']} {m['status']} summary={m.get('import_summary')}",
              flush=True)
        campaign["apps"][name] = {
            "exe": str(exe), "sha256": sha256(exe), "argv": argv,
            "timeout_s": tout, "kind": kind,
            "wine": w, "miniwin": m,
        }
        (ART / "campaign.json").write_text(json.dumps(campaign, indent=1))
    print("== done ==", flush=True)


if __name__ == "__main__":
    main()
