"""JSONL event schema v1: writer, reader, validation.

Discipline (spec 2.6):
- Absent information stays absent — never fabricated.
- Malformed raw lines become structured parser_error records.
- Raw-line provenance is preserved (raw_offset + raw_line when available).
- confidence is one of raw / derived / inferred.
"""
import json
import re

from . import EVENT_TYPES, SCHEMA_FIELDS, CONFIDENCE_LEVELS


def now_iso():
    import datetime
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds")


class EventWriter:
    def __init__(self, path, run_id, capture_source):
        self.path = str(path)
        self.run_id = run_id
        self.capture_source = capture_source
        self.sequence = 0
        self.dropped = 0
        self._fh = open(self.path, "a", encoding="utf-8")

    def emit(self, event_type, **fields):
        if event_type not in EVENT_TYPES:
            self.dropped += 1
            return None
        self.sequence += 1
        ev = {
            "run_id": self.run_id,
            "timestamp": fields.pop("timestamp", now_iso()),
            "sequence": self.sequence,
            "event_type": event_type,
            "capture_source": fields.pop("capture_source", self.capture_source),
            "confidence": fields.pop("confidence", "raw"),
        }
        for k, v in fields.items():
            if v is not None:
                ev[k] = v
        self._fh.write(json.dumps(ev, ensure_ascii=False, default=str) + "\n")
        return ev

    def emit_parser_error(self, raw_line, reason, **extra):
        return self.emit("parser_error", reason=reason, raw_line=raw_line[:2000],
                         confidence="derived", **extra)

    def close(self):
        self._fh.flush()
        self._fh.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def read_events(path):
    """Yield (event_or_None, parser_error_or_None). Strict per-line."""
    with open(path, "r", encoding="utf-8") as fh:
        for i, line in enumerate(fh):
            line = line.strip()
            if not line:
                continue
            try:
                ev = json.loads(line)
            except json.JSONDecodeError as e:
                yield None, {"line_no": i + 1, "reason": f"invalid JSON: {e}",
                             "raw": line[:500]}
                continue
            yield ev, None


def validate_event(ev, strict=False):
    """Check structural validity of one event dict. Returns list of problems."""
    problems = []
    if not isinstance(ev, dict):
        return ["event is not an object"]
    for req in ("run_id", "timestamp", "sequence", "event_type", "capture_source"):
        if req not in ev:
            problems.append(f"missing required field {req}")
    if ev.get("event_type") not in EVENT_TYPES:
        problems.append(f"unknown event_type {ev.get('event_type')!r}")
    conf = ev.get("confidence")
    if conf is not None and conf not in CONFIDENCE_LEVELS:
        problems.append(f"bad confidence {conf!r}")
    if strict and problems:
        return problems
    return problems


def summarize(path):
    """Cheap event counts per type + sequence coverage."""
    counts = {}
    total = 0
    malformed = 0
    seqs = []
    for ev, perr in read_events(path):
        if perr:
            malformed += 1
            continue
        total += 1
        counts[ev["event_type"]] = counts.get(ev["event_type"], 0) + 1
        seqs.append(ev.get("sequence"))
    return {"total": total, "malformed": malformed, "by_type": counts,
            "sequence_min": min(seqs) if seqs else None,
            "sequence_max": max(seqs) if seqs else None}
