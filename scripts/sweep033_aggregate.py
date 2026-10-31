#!/usr/bin/env python3
"""SWEEP-033 aggregator v2 — rebuild the campaign dataset from per-app workdir
artifacts and emit the BASE API deliverables.

Attribution model (honest, no fabrication):
  static  : the app's import table as parsed by miniwin's loader
            (miniwin resolved 100% of imports across the corpus this round).
  dynamic : every call recorded by the wine level-0 relay (whole process,
            including wine's own runtime internals).
  surface : dynamic ∩ static  — APIs the app both imports and actually calls
            under wine. This is the app's true exercised contract surface.
  infra   : dynamic − static  — wine runtime/loader internals + any
            GetProcAddress-discovered calls (attribution to app code needs
            relay caller-PC module ranges: future work; relay samples kept).

BASE API = union of surface sets across the corpus, ranked by how many apps
exercise each API and how hot it is (total calls).
"""
import json
import re
import shutil
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

REPO = Path("/home/z/my-project/workspace/miniwin-engineering")
WORK = Path("/home/z/tmp/sweep033")
ART = REPO / "artifacts" / "sweep033"
LOADER = REPO / "minwin_loader"

sys.path.insert(0, "/home/z/my-project/scripts")
from sweep033_level0_campaign import CORPUS, parse_minwin_trace  # noqa: E402

WINE_RC_OBS = {
    "winrar-gui": (None, "TIMEOUT-KILLED"), "rar-console": (0, "OK"),
    "unrar": (0, "OK"), "putty": (None, "TIMEOUT-KILLED"),
    "puttygen": (1, "OK"), "pageant": (None, "TIMEOUT-KILLED"),
    "procexp64": (None, "TIMEOUT-KILLED"), "procmon64": (None, "TIMEOUT-KILLED"),
    "bginfo64": (1, "OK"), "rammap64": (None, "TIMEOUT-KILLED"),
    "autoruns64": (1, "OK"), "zoomit64": (0, "OK"), "produkey": (0, "OK"),
    "cports": (0, "OK"), "diskmon64": (0, "OK"),
    "searchmyfiles": (0, "OK"), "winmergeu": (0, "OK"), "coreinfo64": (0, "OK"),
    "7za": (0, "OK"), "jq": (0, "OK"), "7z-sfx": (1, "OK"),
    "notepadpp": (255, "OK"),
}


def main():
    ART.mkdir(parents=True, exist_ok=True)
    apps = {}
    surface_app = defaultdict(list)   # api -> [apps]
    surface_calls = Counter()         # api -> total calls on surface
    for name, exe, argv, tout, kind in CORPUS:
        wd = WORK / name
        rec = {"exe": str(exe), "sha256": None, "argv": argv,
               "timeout_s": tout, "kind": kind}
        if Path(exe).exists():
            import hashlib
            h = hashlib.sha256()
            h.update(Path(exe).read_bytes())
            rec["sha256"] = h.hexdigest()
        dyn_path = wd / "apis.dynamic.json"
        dyn = {}
        if dyn_path.exists():
            d = json.load(open(dyn_path))
            dyn = {k: v for k, v in d["apis"].items()}
        rec["wine"] = {"rc": WINE_RC_OBS.get(name, (None, "?"))[0],
                       "status": WINE_RC_OBS.get(name, (None, "?"))[1],
                       "api_calls_distinct": len(dyn),
                       "api_calls_total": sum(dyn.values())}
        # miniwin side (cheap rerun for rc + fresh trace)
        mwdir = wd / "mw"
        mwdir.mkdir(parents=True, exist_ok=True)
        try:
            r = subprocess.run([str(LOADER), str(exe)] + list(argv), cwd=str(mwdir),
                               capture_output=True, timeout=max(tout, 40))
            rc, status = r.returncode, "OK"
            (mwdir / "mw.stdout.txt").write_bytes(r.stdout or b"")
            (mwdir / "mw.stderr.txt").write_bytes(r.stderr or b"")
        except subprocess.TimeoutExpired as e:
            rc, status = None, "TIMEOUT-KILLED"
            (mwdir / "mw.stdout.txt").write_bytes(e.stdout or b"")
            (mwdir / "mw.stderr.txt").write_bytes(e.stderr or b"")
        trace = mwdir / "miniwin-results" / Path(exe).name / "api_trace.json"
        resolved, unresolved, summary = set(), set(), None
        if trace.exists():
            resolved, unresolved, summary = parse_minwin_trace(trace)
            shutil.copy(trace, ART / f"{name}.miniwin-apitrace.json")
        rec["miniwin"] = {"rc": rc, "status": status,
                          "import_summary": summary,
                          "resolved_distinct": len(resolved),
                          "unresolved_distinct": len(unresolved),
                          "unresolved_list": sorted(unresolved)[:200]}
        surface = {api.upper(): n for api, n in dyn.items()
                   if api.upper() in {r.upper() for r in resolved}}
        infra = {api: n for api, n in dyn.items()
                 if api.upper() not in {r.upper() for r in resolved}}
        rec["surface_distinct"] = len(surface)
        rec["surface_calls"] = sum(surface.values())
        rec["infra_distinct"] = len(infra)
        rec["infra_calls"] = sum(infra.values())
        rec["surface_top"] = dict(Counter(surface).most_common(80))
        for api in surface:
            surface_app[api].append(name)
        surface_calls.update(surface)
        apps[name] = rec
        print(f"[{name}] dyn={len(dyn)} static={len(resolved)} "
              f"surface={len(surface)} ({sum(surface.values())} calls) "
              f"infra={len(infra)} mw_rc={rc}", flush=True)

    # ---------- BASE API ----------
    ranked = sorted(surface_app.items(), key=lambda kv: (-len(kv[1]), kv[0]))
    hot = [(api, surface_calls[api]) for api, _ in ranked]
    per_dll = Counter()
    for api in surface_app:
        per_dll[api.split(".")[0].upper()] += 1

    (ART / "campaign.json").write_text(json.dumps({
        "method": "level-0 WINEDEBUG=+relay capture (wine-10.0 WineHQ noble) vs "
                  "minwin_loader import sets; surface = dynamic ∩ static; "
                  "GUI-INIT apps are headless init-phase traces; console apps full runs",
        "apps": apps,
        "base_api_ranked": [{"api": api, "apps": sorted(a), "n": len(a),
                             "calls_total": surface_calls[api]}
                            for api, a in ranked],
        "per_dll_surface": dict(per_dll),
    }, indent=1))

    # matrix TSV: app x dll (surface distinct counts)
    dll_by_app = {}
    for name, rec in apps.items():
        c = Counter()
        for api in rec.get("surface_top", {}):
            c[api.split(".")[0].upper()] += 1
        dll_by_app[name] = c
    alldll = sorted({d for c in dll_by_app.values() for d in c})
    with open(ART / "matrix-app-x-dll.tsv", "w") as fh:
        fh.write("app\t" + "\t".join(alldll) + "\n")
        for name in dll_by_app:
            fh.write(name + "\t" + "\t".join(
                str(dll_by_app[name].get(d, 0)) for d in alldll) + "\n")

    # per-app artifacts into repo
    for name, exe, argv, tout, kind in CORPUS:
        src = WORK / name / "apis.dynamic.json"
        if src.exists():
            shutil.copy(src, ART / f"{name}.wine-dynamic-apis.json")
    print("aggregated ->", ART, flush=True)


if __name__ == "__main__":
    main()
