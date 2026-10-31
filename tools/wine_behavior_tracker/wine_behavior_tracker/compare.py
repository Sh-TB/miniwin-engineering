"""Wine-vs-MiniWin comparison: normalize per checkpoint, find first divergence.

Discipline:
- Never compares PIDs, timestamps, addresses, random paths.
- A missing Wine-observed event in MiniWin is a divergence CANDIDATE;
  confidence: 'confirmed' only when tied to a checkpoint assertion outcome,
  'supported' when two independent capture sources agree, else 'speculative'.
- Identical verified workflow outputs with different internal call sequences
  are NOT divergences (Wine is a behavioral reference, not a clone target).
"""
import json
from collections import Counter
from pathlib import Path


def load_run_events(run_dir):
    p = Path(run_dir) / "parsed" / "events.jsonl"
    events = []
    if not p.exists():
        return events
    with open(p, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return events


def load_checkpoints(run_dir):
    ck_dir = Path(run_dir) / "checkpoints"
    out = {}
    if not ck_dir.exists():
        return out
    for f in sorted(ck_dir.glob("*.json")):
        try:
            out[f.stem] = json.load(open(f))
        except (json.JSONDecodeError, OSError):
            continue
    return out


def api_family(api):
    """Coarse family: strip W/A suffix and module prefixes."""
    if not api:
        return None
    name = api.split("!")[-1].split(".")[-1]
    if name.endswith("W") and not name.endswith("CW"):
        return name[:-1]
    if name.endswith("A"):
        return name[:-1]
    return name


def compare_runs(wine_run, miniwin_run):
    wine_events = load_run_events(wine_run)
    mw_events = load_run_events(miniwin_run)
    wine_cks = load_checkpoints(wine_run)
    mw_cks = load_checkpoints(miniwin_run)

    wine_fams = Counter(api_family(e.get("api")) for e in wine_events
                        if e.get("event_type") == "api_call")
    mw_fams = Counter(api_family(e.get("api")) for e in mw_events
                      if e.get("event_type") == "api_call")

    candidates = []

    # 1. Checkpoint outcome mismatches = CONFIRMED divergences (asserted)
    for ck_id in sorted(set(wine_cks) | set(mw_cks)):
        w = wine_cks.get(ck_id, {}).get("outcome", "absent")
        m = mw_cks.get(ck_id, {}).get("outcome", "absent")
        if w != m:
            candidates.append({
                "candidate_id": f"DIV-{ck_id}",
                "checkpoint": ck_id,
                "expected": w,
                "observed": m,
                "confidence": "confirmed",
                "note": "checkpoint outcome differs — asserted workflow divergence",
                "wine_artifact": str(Path(wine_run) / "checkpoints" / f"{ck_id}.json"),
                "miniwin_artifact": str(Path(miniwin_run) / "checkpoints" / f"{ck_id}.json"),
            })

    # 2. Wine-used API families absent from MiniWin run = candidates only
    #    (a missing event is a candidate, not an explanation)
    for fam, cnt in wine_fams.most_common():
        if fam and fam not in mw_fams:
            candidates.append({
                "candidate_id": f"DIV-API-{fam}",
                "checkpoint": "any",
                "expected": f"{fam} used {cnt}x under Wine",
                "observed": "absent from MiniWin capture",
                "confidence": "speculative",
                "note": "divergence candidate; needs a discriminating experiment "
                        "tied to a functional effect before promotion",
            })

    # 3. Snapshot effect comparison
    for run, label in ((wine_run, "wine"), (miniwin_run, "miniwin")):
        d = Path(run) / "snapshots" / "diff.json"
        if d.exists():
            diff = json.load(open(d))
            candidates.append({
                "candidate_id": f"FS-{label}",
                "checkpoint": "filesystem",
                "expected": "n/a (informational)",
                "observed": {k: len(v) for k, v in diff.items()},
                "confidence": "supported",
                "note": "observed side effects; attribution to APIs needs more evidence",
            })

    return {
        "wine_run": str(wine_run),
        "miniwin_run": str(miniwin_run),
        "wine_api_families": len(wine_fams),
        "miniwin_api_families": len(mw_fams),
        "divergence_candidates": candidates,
        "confirmed": [c for c in candidates if c["confidence"] == "confirmed"],
        "note": "no trace-similarity percentage is produced on purpose",
    }
