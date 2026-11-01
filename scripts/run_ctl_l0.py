#!/usr/bin/env python3
"""run_ctl_l0.py — W8 (SW-1595): L0 battery runner for the 15 ctl programs.

Runs each guest x3 through minwin_loader, then:
  * determinism: sha256 of the filtered ENGINE-EVENT stream ([TIP]/[TIPPAINT]/
    [SCRL]/[SCRLPAINT]/[LB]/[PBM]/[TBM]/[UDM]/[SSICON] lines) across runs
  * checks: guest-reported PASS/FAIL counts from stderr
  * frames: count of rendered BMP evidence
Writes artifacts/wine-l0-w8/ctl_l0_summary.tsv + a per-program JSON pack.
"""
import hashlib
import json
import os
import re
import subprocess
import sys

REPO = "/home/z/my-project/workspace/miniwin-engineering"
OUT = os.path.join(REPO, "artifacts", "wine-l0-w8")
os.makedirs(OUT, exist_ok=True)

EVENT_RE = re.compile(
    r"\[(TIP|TIPPAINT|SCRL|SCRLPAINT|LB|PBM|PBMPAINT|TBM|TBMPAINT|UDM|SSICON)\]")
TID_RE = re.compile(r"^\[T[0-9a-f]+\] \[API\] ")

MODES = {
    1: "tooltip_direct", 2: "tooltip_callback", 3: "tooltip_toolbar",
    4: "tooltip_menu", 5: "tooltip_track", 6: "scrollbar_h",
    7: "scrollbar_v", 8: "buttons", 9: "static_frames", 10: "static_icon",
    11: "button_icon", 12: "listbox", 13: "combobox", 14: "edit_scroll",
    15: "progress_track",
}

rows = []
for i in range(1, 16):
    exe = os.path.join(REPO, "tests", "controls", f"ctl{i:02d}.exe")
    hashes = []
    passn = failn = 0
    frames = 0
    events_last = []
    for run in range(1, 4):
        env = dict(os.environ)
        env["MINWIN_GUI_EVENTS"] = "1"
        env["MINWIN_TRACE"] = "1" if run == 2 else env.get("MINWIN_TRACE", "")
        # keep the trace file from run 1 as the evidence copy
        trace_path = os.path.join(REPO, "miniwin-results", f"ctl{i:02d}.exe",
                                  "api_trace.json")
        if run == 1 and os.path.exists(trace_path):
            os.remove(trace_path)
        p = subprocess.run([os.path.join(REPO, "minwin_loader"), exe],
                           cwd=REPO, env=env, capture_output=True,
                           text=True, timeout=120)
        err = p.stderr or ""
        m = re.search(r"---- ctl\d+: (\d+) passed, (\d+) failed", err)
        if m:
            passn, failn = int(m.group(1)), int(m.group(2))
        evs = []
        if os.path.exists(trace_path):
            with open(trace_path, errors="replace") as f:
                evs = [TID_RE.sub("", ln.rstrip()) for ln in f
                       if EVENT_RE.search(ln)]
        hashes.append(hashlib.sha256(
            "\n".join(evs).encode()).hexdigest()[:16])
        if run == 1:
            events_last = evs
        sdir = os.path.join(REPO, "miniwin-results", f"ctl{i:02d}.exe",
                            "screenshots")
        if run == 1 and os.path.isdir(sdir):
            frames = len([x for x in os.listdir(sdir) if x.endswith(".bmp")])
    deterministic = len(set(hashes)) == 1
    rows.append({
        "mode": i, "name": MODES[i], "pass": passn, "fail": failn,
        "deterministic": deterministic, "hash": hashes[0],
        "events": len(events_last), "frames": frames,
    })
    print(f"ctl{i:02d} {MODES[i]:18s} pass={passn:3d} fail={failn} "
          f"det={deterministic} events={len(events_last):4d} frames={frames}")

with open(os.path.join(OUT, "ctl_l0_summary.tsv"), "w") as f:
    f.write("mode\tname\tpass\tfail\tdeterministic\tevent_stream_sha16\tevents\tframes\n")
    for r in rows:
        f.write(f"{r['mode']}\t{r['name']}\t{r['pass']}\t{r['fail']}\t"
                f"{r['deterministic']}\t{r['hash']}\t{r['events']}\t{r['frames']}\n")

total_pass = sum(r["pass"] for r in rows)
total_fail = sum(r["fail"] for r in rows)
all_det = all(r["deterministic"] for r in rows)
print(f"\nTOTAL: {total_pass} passed, {total_fail} failed; "
      f"determinism {'3x/3x OK' if all_det else 'MISMATCH'}")
sys.exit(0 if (total_fail == 0 and all_det) else 1)
