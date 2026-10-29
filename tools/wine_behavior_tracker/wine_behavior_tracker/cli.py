"""CLI entry: python -m wine_behavior_tracker <cmd> ...

Commands (implemented, not aspirational):
  doctor    — environment audit (wine, xvfb, snapshot, hash tools)
  run       — execute a workflow spec, capture everything
  parse     — (re)parse raw relay/trace logs of a run into events.jsonl
  compare   — wine run vs miniwin run divergence report
  report    — markdown report for a run
  verify    — recompute manifest.sha256 of a run
  selftest  — synthetic end-to-end test (no Wine required)
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from . import __version__, runner, report as report_mod, compare as compare_mod
from .schema import EventWriter, summarize
from . import relay_parse

REPO = Path(__file__).resolve().parents[3]
RUNS = REPO / "artifacts" / "wine-tracker" / "runs"
DOCS = REPO / "artifacts" / "wine-tracker"


def find_run(run_id):
    matches = sorted(RUNS.glob(f"{run_id}*"))
    if not matches:
        sys.exit(f"run not found: {run_id} (under {RUNS})")
    return matches[-1]


def cmd_doctor(_):
    rows = []
    def check(name, ok, detail=""):
        rows.append((name, "OK" if ok else "MISSING", detail))
        return ok

    wine = os.environ.get("WINE_TRACKER_WINE", "/home/z/my-project/tools/wine-10.0")
    launcher = Path(wine) / "bin" / "wine64"
    wver = None
    if check("wine64 launcher", launcher.exists(), str(launcher)):
        try:
            env = os.environ.copy()
            env.setdefault("WINEPREFIX", "/home/z/my-project/prefixes/main")
            wver = subprocess.run([str(launcher), "--version"], capture_output=True,
                                  text=True, timeout=30, env=env).stdout.strip()
        except Exception as e:
            wver = f"error: {e}"
    loader = REPO / "minwin_loader"
    check("minwin_loader built", loader.exists(), str(loader))
    check("Xvfb", shutil.which("Xvfb") is not None)
    check("xvfb-run", shutil.which("xvfb-run") is not None)
    check("PIL (screenshot)", _has_pil())
    check("yaml", _has_yaml())
    prefix = os.environ.get("WINEPREFIX", "/home/z/my-project/prefixes/main")
    check("wine prefix initialized", (Path(prefix) / "drive_c").exists(), prefix)
    print(f"wine_behavior_tracker {__version__} — doctor")
    for name, status, detail in rows:
        print(f"  [{status:6s}] {name}  {detail}")
    if wver:
        print(f"  wine version: {wver}")
    print("Runs root:", RUNS)
    return 0


def _has_pil():
    try:
        import PIL  # noqa
        return True
    except ImportError:
        return False


def _has_yaml():
    try:
        import yaml  # noqa
        return True
    except ImportError:
        return False


def cmd_run(args):
    run_id, run_dir, meta = runner.run_spec(args.spec,
                                            only_checkpoints=args.only or None)
    print(json.dumps({"run_id": run_id, "run_dir": str(run_dir),
                      "status": meta.get("status", "completed")}, indent=1))
    rep = report_mod.report_for_run(run_dir)
    (Path(run_dir) / "reports" / "report.md").write_text(rep)
    print(rep)
    return 0 if meta.get("status") != "BLOCKED" else 3


def cmd_parse(args):
    run_dir = find_run(args.run)
    src = Path(args.frm) if args.frm else run_dir / "raw" / "wine_relay.raw"
    if not src.exists():
        # checkpoint stderr is the relay sink when WINEDEBUG routes there
        candidates = sorted((run_dir / "raw").glob("stderr_*.log"))
        biggest = max(candidates, key=lambda p: p.stat().st_size, default=None)
        if biggest and biggest.stat().st_size > 1000:
            src = biggest
        else:
            sys.exit(f"no relay source in run (looked for raw/wine_relay.raw, {candidates})")
    writer = EventWriter(run_dir / "parsed" / "events.jsonl", run_dir.name,
                         capture_source="wine-relay")
    stats = relay_parse.parse_relay_to_events(src, writer, run_dir.name)
    writer.close()
    print(json.dumps({"source": str(src), **stats}, indent=1))
    return 0


def cmd_compare(args):
    out = compare_mod.compare_runs(find_run(args.wine), find_run(args.miniwin))
    dest = DOCS / "reports"
    dest.mkdir(parents=True, exist_ok=True)
    fname = dest / f"compare-{Path(out['wine_run']).name}-vs-{Path(out['miniwin_run']).name}.json"
    fname.write_text(json.dumps(out, indent=2))
    print(json.dumps(out, indent=1))
    print(f"saved: {fname}")
    return 0


def cmd_report(args):
    run_dir = find_run(args.run)
    rep = report_mod.report_for_run(run_dir)
    if args.out:
        Path(args.out).write_text(rep)
    print(rep)
    return 0


def cmd_verify(args):
    run_dir = find_run(args.run)
    manifest = run_dir / "manifest.sha256"
    if not manifest.exists():
        sys.exit("no manifest in run")
    bad = 0
    total = 0
    for line in manifest.read_text().splitlines():
        if not line or line.startswith("UNREADABLE"):
            continue
        h, rel = line.split("  ", 1)
        total += 1
        p = run_dir / rel
        if not p.exists():
            print(f"MISSING {rel}")
            bad += 1
            continue
        actual = runner.sha256_file(p)
        if actual != h:
            print(f"CHANGED {rel}")
            bad += 1
    print(f"{total - bad}/{total} artifacts verified ({bad} problems)")
    return 1 if bad else 0


def cmd_selftest(_):
    """Synthetic end-to-end: parser + schema + snapshot-diff + manifest.
    Clearly labeled synthetic; proves control flow without Wine."""
    import tempfile
    from .schema import validate_event

    tmp = Path(tempfile.mkdtemp(prefix="wbt-selftest-"))
    ok = True

    def check(name, cond, detail=""):
        nonlocal ok
        print(f"  [{'PASS' if cond else 'FAIL'}] {name} {detail}")
        ok = ok and cond

    # 1. schema writer + validator
    with EventWriter(tmp / "events.jsonl", "selftest", "synthetic") as w:
        w.emit("run_meta", spec="selftest", confidence="derived")
        w.emit("api_call", api="kernel32.Sleep", arguments={"relay_text": "00000064"},
               confidence="raw", raw_line="SYNTHETIC-TEST-DATA")
        w.emit_parser_error("SYNTHETIC-TEST-DATA garbage line", "test")
    counts = summarize(tmp / "events.jsonl")
    check("jsonl counts", counts["total"] == 3 and counts["malformed"] == 0, str(counts))
    evs = [json.loads(l) for l in open(tmp / "events.jsonl")]
    check("validator ok on real events", all(not validate_event(e) for e in evs))

    # 2. relay parser on a synthetic relay log
    relay = tmp / "synthetic_relay.raw"  # labeled SYNTHETIC in content
    relay.write_text(
        "# SYNTHETIC-TEST-DATA — hand-written relay grammar, NOT a Wine capture\n"
        "00aa:Call kernel32.CreateFileW(0x1,0x2) ret=00401000\n"
        "00aa:Ret  kernel32.CreateFileW() retval=0000ffff ret=00401000\n"
        "Loaded L\"C:\\\\windows\\\\system32\\\\kernel32.dll\" at 0x7b000000\n"
        "00aa:Ret  user32.Orphan() retval=00000001 ret=00401000\n")
    with EventWriter(tmp / "relay_events.jsonl", "selftest", "wine-relay") as w:
        stats = relay_parse.parse_relay_to_events(relay, w, "selftest")
    check("relay: 1 api pair", stats["api_calls"] == 1 and stats["api_returns"] == 1, str(stats))
    check("relay: module load", stats["module_loads"] == 1, str(stats))
    check("relay: unmatched ret counted", stats["unmatched_rets"] == 1, str(stats))

    # 3. snapshot diff + manifest
    (tmp / "work").mkdir()
    (tmp / "work" / "a.txt").write_text("alpha")
    b = runner.snapshot_dir(tmp / "work", tmp / "before.json")
    (tmp / "work" / "a.txt").write_text("alpha2")
    (tmp / "work" / "b.bin").write_bytes(b"\x00\x01")
    a = runner.snapshot_dir(tmp / "work", tmp / "after.json")
    diff = runner.diff_snapshots(b, a)
    check("snapshot diff", diff["added"] == ["b.bin"] and diff["changed"] == ["a.txt"], str(diff))
    runner.write_manifest(tmp / "run")
    check("manifest written", (tmp / "run" / "manifest.sha256").exists())

    print("SELFTEST:", "PASS" if ok else "FAIL", "(all fixtures SYNTHETIC-TEST-DATA)")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(prog="wine_behavior_tracker",
                                 description="Wine behavior tracker (evidence-grade capture + diff)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("doctor").set_defaults(func=cmd_doctor)
    p_run = sub.add_parser("run"); p_run.add_argument("--spec", required=True)
    p_run.add_argument("--only", nargs="*", help="run only these checkpoint ids")
    p_run.set_defaults(func=cmd_run)
    p_parse = sub.add_parser("parse"); p_parse.add_argument("--run", required=True)
    p_parse.add_argument("--from", dest="frm", help="explicit relay source path")
    p_parse.set_defaults(func=cmd_parse)
    p_cmp = sub.add_parser("compare"); p_cmp.add_argument("--wine", required=True)
    p_cmp.add_argument("--miniwin", required=True)
    p_cmp.set_defaults(func=cmd_compare)
    p_rep = sub.add_parser("report"); p_rep.add_argument("--run", required=True)
    p_rep.add_argument("--out")
    p_rep.set_defaults(func=cmd_report)
    p_ver = sub.add_parser("verify"); p_ver.add_argument("--run", required=True)
    p_ver.set_defaults(func=cmd_verify)
    sub.add_parser("selftest").set_defaults(func=cmd_selftest)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
