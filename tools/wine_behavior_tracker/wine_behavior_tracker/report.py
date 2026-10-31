"""Markdown report generator for a run (from real artifacts only)."""
import json
from pathlib import Path

from .schema import summarize


def report_for_run(run_dir):
    run_dir = Path(run_dir)
    meta = json.loads((run_dir / "run.json").read_text())
    lines = [f"# Run report — {meta.get('run_id')}", ""]
    lines.append(f"- spec: `{meta.get('spec')}`  engine: **{meta.get('engine')}**"
                 f" {meta.get('engine_detail', '')}")
    lines.append(f"- started {meta.get('start')} — duration {meta.get('duration_s', '?')}s")
    lines.append(f"- status: **{meta.get('status', 'completed')}**")
    if meta.get("blocked_reason"):
        lines.append(f"- blocked: {meta['blocked_reason']}")
    if meta.get("fixture_hashes"):
        lines.append(f"- fixture hashes: `{meta['fixture_hashes']}`")
    lines.append("")

    ck_dir = run_dir / "checkpoints"
    if ck_dir.exists():
        lines.append("## Checkpoints")
        lines.append("| id | outcome | exit | verifications |")
        lines.append("|---|---|---|---|")
        for f in sorted(ck_dir.glob("*.json")):
            c = json.load(open(f))
            vs = ", ".join(f"{v['name']}={'OK' if v['ok'] else 'FAIL'}"
                           for v in c.get("verifications", []))
            lines.append(f"| {c['id']} | {c.get('outcome')} | {c.get('exit_code')} | {vs} |")
        lines.append("")

    ev = run_dir / "parsed" / "events.jsonl"
    if ev.exists():
        s = summarize(ev)
        lines.append("## Events")
        lines.append(f"- total {s['total']}, malformed {s['malformed']}, "
                     f"sequence {s['sequence_min']}..{s['sequence_max']}")
        for t, n in sorted(s["by_type"].items(), key=lambda x: -x[1]):
            lines.append(f"- {t}: {n}")
        lines.append("")

    d = run_dir / "snapshots" / "diff.json"
    if d.exists():
        diff = json.loads(d.read_text())
        lines.append("## Filesystem effects (workdir)")
        lines.append(f"- added {len(diff['added'])}, changed {len(diff['changed'])}, "
                     f"removed {len(diff['removed'])}")
        for a in diff["added"][:20]:
            lines.append(f"  - + `{a}`")
        lines.append("")

    lines.append("## Manifest")
    m = run_dir / "manifest.sha256"
    if m.exists():
        n = len(m.read_text().strip().splitlines())
        lines.append(f"- {n} hashed artifacts (verify: `python -m wine_behavior_tracker "
                     f"verify --run {meta.get('run_id')}`)")
    return "\n".join(lines) + "\n"
