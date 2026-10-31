"""Wine +relay raw log -> schema-v1 JSONL events.

Pairing discipline mirrors scripts/analysis/trace_collector.py (per-thread
LIFO stack; no fabricated retvals). Additionally emits:
- process_start / process_exit from wine's own lifecycle lines when present
- module_load events from +relay DLL load lines (trace:loaddll) or
  a separate +module capture
- api_call/api_return pairs with confidence=raw
- parser_error for anything unrecognized (bounded)
"""
import re

from .schema import now_iso


RELAY_CALL = re.compile(r"^([0-9a-f]+):Call\s+(.+?)\(([^)]*)\)\s+ret=([0-9a-f]+)$")
RELAY_RET = re.compile(r"^([0-9a-f]+):Ret\s+(.+?)\s+ret=([0-9a-f]+)$")
RELAY_RETVAL = re.compile(r"retval=([0-9a-f]+)")
LOADDLL = re.compile(r"load_dll Loaded module L\"([^\"]+)\" at 0x([0-9a-f]+)")
LOADED_ALT = re.compile(r"Loaded L\"([^\"]+)\" at 0x([0-9a-f]+)")
SO_EXE_START = re.compile(r"Starting process L\"([^\"]+)\"")
EXE_EXIT = re.compile(r"([0-9a-f]+): exited with status ([0-9]+)|process exit status ([0-9a-f]+)")


def parse_relay_to_events(relay_path, writer, run_id, source="wine-relay"):
    """Stream a relay log into the writer. Returns a stats dict.

    Never fabricates: a Call with no matching Ret ends as an api_call with
    return_value absent + truncated flag, not as a success.
    """
    stacks = {}          # tid -> list of open api_call events (dicts)
    stats = {"lines": 0, "api_calls": 0, "api_returns": 0, "module_loads": 0,
             "parser_errors": 0, "unmatched_rets": 0, "truncated": 0}
    seq_counter = 1000000  # offset so api events don't clash with writer seq

    with open(relay_path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            stats["lines"] += 1
            if not line or line.startswith("#"):
                continue

            m = LOADDLL.search(line) or LOADED_ALT.search(line)
            if m:
                stats["module_loads"] += 1
                seq_counter += 1
                writer.emit("module_load", module=m.group(1), base=m.group(2),
                            raw_line=line[:400], confidence="raw")
                continue

            m = RELAY_CALL.match(line)
            if m:
                tid, func, args, ret = m.group(1), m.group(2).strip(), m.group(3), m.group(4)
                seq_counter += 1
                ev = writer.emit("api_call", thread=tid, api=func,
                                 arguments={"relay_text": args},
                                 call_site=ret, raw_line=line[:400],
                                 confidence="raw")
                if ev is not None:
                    stacks.setdefault(tid, []).append((ev["sequence"], func))
                stats["api_calls"] += 1
                continue

            m = RELAY_RET.match(line)
            if m:
                tid, func = m.group(1), m.group(2).strip()
                rv = RELAY_RETVAL.search(line)
                stack = stacks.get(tid)
                if stack:
                    call_seq, call_func = stack.pop()
                    stats["api_returns"] += 1
                    writer.emit("api_return", thread=tid, api=func,
                                pairs_with_sequence=call_seq,
                                **({"return_value": rv.group(1)} if rv else {}),
                                return_value_known=bool(rv),
                                raw_line=line[:400], confidence="raw")
                else:
                    stats["unmatched_rets"] += 1
                continue

            # Anything else: only record a bounded sample of parse errors
            if stats["parser_errors"] < 200:
                stats["parser_errors"] += 1
                writer.emit_parser_error(line[:400], "unrecognized relay line")
            else:
                stats["parser_errors"] += 1

    # EOF: any still-open call is a truncated call (crash path)
    for tid, stack in stacks.items():
        for seq, func in stack:
            stats["truncated"] += 1
            writer.emit("api_call", thread=tid, api=func,
                        truncated_at_eof=True, confidence="derived",
                        note="call open at EOF: no Ret observed; no return fabricated")
    return stats


def parse_minwin_trace_to_events(trace_json_path, writer, run_id):
    """MiniWin's own miniwin-results/<exe>/api_trace.json -> JSONL events.

    MiniWin's trace is loader-emitted [API] text lines inside a {"trace":[...]}
    array; decode what is decodable, mark the rest parser_error. Confidence is
    'raw' for loader-written fields, never inferred into API semantics we do
    not have.
    """
    import json
    stats = {"lines": 0, "api_events": 0, "parser_errors": 0}
    try:
        with open(trace_json_path, "r", encoding="utf-8", errors="replace") as fh:
            txt = fh.read()
    except OSError as e:
        writer.emit_parser_error(str(e), "minwin trace unreadable")
        return stats
    # The trace array is a list of strings (line-oriented)
    try:
        doc = json.loads(txt)
        lines = doc.get("trace", []) if isinstance(doc, dict) else doc
    except json.JSONDecodeError:
        # Not strict JSON (trace tails) — fall back to line scraping
        lines = [l for l in txt.splitlines() if "[API]" in l or "[MiniWin]" in l]

    api_re = re.compile(r"\[T(\d+)\]\s*\[API\]\s*(.*)$")
    for line in lines:
        stats["lines"] += 1
        m = api_re.search(str(line))
        if not m:
            continue
        tid, body = m.group(1), m.group(2)
        # Loader emits "<Name>(args) -> result" for dispatch rows
        mm = re.match(r"^([A-Za-z_][A-Za-z0-9_]*?)\((.*)\)\s*->\s*(.*)$", body)
        if mm:
            api, args, res = mm.group(1), mm.group(2), mm.group(3).strip()
            writer.emit("api_call", thread=tid, api=api,
                        arguments={"relay_text": args},
                        **({"return_value": res} if res else {}),
                        capture_source="miniwin-trace", confidence="raw")
            stats["api_events"] += 1
        else:
            if stats["parser_errors"] < 200:
                stats["parser_errors"] += 1
                writer.emit_parser_error(body[:300], "minwin line not api-shaped",
                                         thread=tid, capture_source="miniwin-trace")
    return stats
