#!/usr/bin/env python3
"""MiniWin L0 normalizer + differential engine (mission Phase 3).

l0_normalize.py — two readers, ONE event schema:
  wine side: artifacts produced by l0_tracer.py (events/*.jsonl already
             normalized there; this module just re-reads them)
  miniwin side: miniwin-results/<exe>/api_trace.json (the engine's text
             trace) -> the same call_pair events

Unified event fields (both sides):
  side ('wine'|'miniwin'), kind ('call_pair'|'channel_event'|...),
  function, arguments, retval, retval_known, tid, attribution, flags

l0_diff.py — comparison pipeline:
  Wine inventory  vs MiniWin inventory
    * missing calls   (Wine observed, MiniWin never called)
    * extra calls     (MiniWin-only, informational)
    * frequency deltas
    * return-value mismatches where both sides recorded known retvals
    * first meaningful divergence: walking Wine's app-attributed
      first-use order, the first function MiniWin never calls
  Report: Markdown (human) + JSON verdict (machine). Raw evidence is never
  rewritten — reports link back to the experiment directories.

Usage:
  python3 l0_normalize.py mw <api_trace.json> --out events.jsonl
  python3 l0_diff.py --wine-dir <l0 experiment dir> \
                     --mw-trace <api_trace.json> --out <report_dir>
"""

import argparse
import hashlib
import json
import re
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

DIFF_VERSION = "1.0"

# MiniWin trace grammar (miniwin-results/<exe>/api_trace.json -> "trace"[])
# call:    [Tccc0] [API] Func(args)              (+ optional continuation)
# call+r:  [Tccc0] [API] Func(args) = result | -> result
MW_CALL_RE = re.compile(
    r'^\[T(?P<tid>[0-9a-f]+)\] \[API\] (?P<func>[A-Za-z_][A-Za-z0-9_]*):?'
    r'\((?P<args>[^)]*)\)(?P<tail>.*)$')
MW_CONT_RE = re.compile(r'^\[T[0-9a-f]+\] \[API\]\s+->\s+(?P<rest>.*)$')
# Loader-internal lines that are NOT guest API calls
MW_SKIP_RE = re.compile(
    r'^(PE Loader|Section \[|KUSD|map_crt_stream|Import|  \[|\[GUI|\[GDI|\[RES|'
    r'exit|FASTFAIL|HEAP-TRAP|CRASH|TRACE|WARN|DEBUG)')


def normalize_mw_trace(api_trace_path):
    """MiniWin api_trace.json -> unified events list.

    The engine's api_trace.json is JSON-shaped but not always strictly
    valid JSON (trace lines may contain unescaped quotes). Parse it as
    text: each element inside the `trace: [ ... ]` array is one line
    written as `"..."` (with a trailing comma) — unquote and feed.
    """
    text = open(api_trace_path, encoding='utf-8', errors='replace').read()
    m = re.search(r'"trace"\s*:\s*\[(.*)', text, re.S)
    lines = []
    if m:
        body = m.group(1)
        # cut at the array terminator (last `]`)
        end = body.rfind(']')
        if end >= 0:
            body = body[:end]
        for ln in body.splitlines():
            s = ln.strip()
            if s.startswith('"'):
                s = s[1:]
            if s.endswith(','):
                s = s[:-1]
            if s.endswith('"'):
                s = s[:-1]
            if s:
                lines.append(s)
    else:
        lines = text.splitlines()
    events = []
    pending = None
    for line in lines:
        m = MW_CONT_RE.match(line)
        if m and pending is not None:
            rest = m.group('rest').strip()
            val, known = _mw_value(rest)
            pending['retval'], pending['retval_known'] = val, known
            events.append(pending)
            pending = None
            continue
        m = MW_CALL_RE.match(line)
        if not m:
            continue
        func = m.group('func')
        if MW_SKIP_RE.match(func):
            continue
        tail = m.group('tail').strip()
        args = m.group('args')
        val, known = _mw_value(tail)
        ev = {
            'side': 'miniwin', 'kind': 'call_pair', 'function': func,
            'arguments': args, 'tid': m.group('tid'),
            'attribution': 'app', 'flags': [],
            'retval': val, 'retval_known': known,
        }
        if known:
            events.append(ev)
        else:
            # result may arrive on the next line as a continuation
            pending = ev
    if pending is not None:
        events.append(pending)
    return events


def _mw_value(tail):
    """Parse ' = X' / '-> X' tails. Unknown stays unknown.
    Strip engine annotations like ' (value=0)' that decorate raw pointers."""
    if not tail:
        return None, False
    t = tail.strip()
    for sep in ('->', '='):
        if t.startswith(sep):
            v = t[len(sep):].strip()
            v = re.sub(r'\s+\((?:value|handle|retval)=[^)]*\)\s*$', '', v).strip()
            if v:
                return v, True
            return None, False
    return None, False


def load_wine_events(experiment_dir):
    """l0_tracer experiment dir -> unified wine events (relay only for diff)."""
    events = []
    ev_dir = Path(experiment_dir) / 'events'
    for jf in sorted(ev_dir.glob('*.jsonl')):
        if jf.stem != 'relay':
            continue
        for line in open(jf, encoding='utf-8', errors='replace'):
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                continue
            ev['side'] = 'wine'
            events.append(ev)
    return events


def inventories(events):
    by_func = Counter()
    rets = defaultdict(Counter)
    first_use_order = []
    seen = set()
    for ev in events:
        f = ev.get('function')
        if not f:
            continue
        by_func[f] += 1
        if ev.get('retval_known') and ev.get('retval') is not None:
            rets[f][str(ev['retval'])] += 1
        if f not in seen:
            seen.add(f)
            first_use_order.append(f)
    return by_func, rets, first_use_order


def _strip_dll(func):
    """'ntdll.RtlFreeHeap' -> ('ntdll', 'RtlFreeHeap'); bare names -> ('', f)."""
    if '.' in func:
        d, _, n = func.partition('.')
        return d.lower(), n
    return '', func


def diff(wine_events, mw_events, exe_name=None):
    w_inv, w_rets, w_order = inventories(wine_events)
    m_inv, m_rets, _ = inventories(mw_events)

    # Wine-side app-attributed inventory is the contract the app exercised.
    w_app_funcs = {e['function'] for e in wine_events
                   if e.get('attribution') == 'app' and e.get('function')}

    def canon(f):
        # MiniWin traces record bare names (no dll. prefix); compare by
        # the bare name on both sides.
        return _strip_dll(f)[1].lower()

    w_canon = {canon(f): f for f in w_inv}
    m_canon = {canon(f): f for f in m_inv}

    missing = []       # wine observed, miniwin never
    for cf, wf in sorted(w_canon.items()):
        if cf not in m_canon:
            app_only = wf in w_app_funcs or canon(wf) in {canon(x) for x in w_app_funcs}
            missing.append({'function': wf, 'wine_calls': w_inv[wf],
                            'app_attributed': app_only})

    extra = [{'function': mf, 'miniwin_calls': m_inv[mf]}
             for cf, mf in sorted(m_canon.items()) if cf not in w_canon]

    freq = []
    for cf in sorted(set(w_canon) & set(m_canon)):
        wf, mf = w_canon[cf], m_canon[cf]
        if w_inv[wf] != m_inv[mf]:
            freq.append({'function': wf, 'wine': w_inv[wf], 'miniwin': m_inv[mf]})

    retval_mismatch = []
    for cf in sorted(set(w_canon) & set(m_canon)):
        wf, mf = w_canon[cf], m_canon[cf]
        wr = w_rets.get(wf, Counter())
        mr = m_rets.get(mf, Counter())
        if not wr or not mr:
            continue
        # deterministic single-value functions only: multiple distinct
        # retvals make pointwise comparison meaningless (state-dependent).
        if len(wr) == 1 and len(mr) == 1:
            wv, mv = next(iter(wr)), next(iter(mr))
            wc, mc = _retclass(wv), _retclass(mv)
            if wc != mc:  # null vs ptr/num, ptr vs num — real mismatch class
                retval_mismatch.append({'function': wf, 'wine': wv,
                                        'miniwin': mv,
                                        'wine_n': wr[wv], 'miniwin_n': mr[mv]})
            elif wc == 'num' and _norm_retval(wv) != _norm_retval(mv):
                retval_mismatch.append({'function': wf, 'wine': wv,
                                        'miniwin': mv,
                                        'wine_n': wr[wv], 'miniwin_n': mr[mv]})
            # wc == mc == 'ptr': different address spaces — never a mismatch

    # first meaningful divergence: Wine's app-attributed first-use order,
    # first function MiniWin never calls.
    first_div = None
    seen = set()
    for ev in wine_events:
        f = ev.get('function')
        if not f or f in seen:
            continue
        seen.add(f)
        if ev.get('attribution') != 'app':
            continue
        if canon(f) not in m_canon:
            first_div = {'position': len(seen),
                         'function': f, 'wine_calls_total': w_inv[f],
                         'why': 'app-attributed call with zero MiniWin calls'}
            break

    return {
        'diff_version': DIFF_VERSION,
        'generated_utc': datetime.now(timezone.utc).isoformat(),
        'summary': {
            'wine_events': len(wine_events),
            'miniwin_events': len(mw_events),
            'wine_unique_functions': len(w_inv),
            'miniwin_unique_functions': len(m_inv),
            'missing_count': len(missing),
            'extra_count': len(extra),
            'freq_delta_count': len(freq),
            'retval_mismatch_count': len(retval_mismatch),
            'first_divergence': first_div,
        },
        'missing': missing,
        'extra': extra[:200],
        'frequency_deltas': freq[:200],
        'retval_mismatches': retval_mismatch[:200],
    }


def _retclass(v):
    """Classify a retval text: 'null' / 'ptr' (>=0x10000 hex) / 'num' / 'raw'.
    Pointer values legitimately differ across runtimes (each maps its own
    address space) — only NULL-vs-non-NULL and numeric values compare."""
    s = str(v).strip().lower()
    if s.startswith('0x'):
        s = s[2:]
    if not s:
        return 'raw'
    try:
        n = int(s, 16)
    except ValueError:
        return 'raw'
    if n == 0:
        return 'null'
    return 'ptr' if n >= 0x10000 else 'num'


def _norm_retval(v):
    """Normalize hex-ish retval text for comparison ('0x0' vs '00000000')."""
    s = str(v).strip().lower()
    if s.startswith('0x'):
        s = s[2:]
    s = s.lstrip('0') or '0'
    try:
        return str(int(s, 16))
    except ValueError:
        return s


def main(argv=None):
    ap = argparse.ArgumentParser(description='L0 differential engine')
    sub = ap.add_subparsers(dest='cmd', required=True)

    p1 = sub.add_parser('mw', help='normalize a MiniWin api_trace.json')
    p1.add_argument('trace')
    p1.add_argument('--out', required=True)

    p2 = sub.add_parser('diff', help='Wine vs MiniWin differential')
    p2.add_argument('--wine-dir', required=True, help='l0_tracer experiment dir')
    p2.add_argument('--mw-trace', required=True, help='MiniWin api_trace.json')
    p2.add_argument('--out', required=True, help='report output dir')

    a = ap.parse_args(argv)
    if a.cmd == 'mw':
        events = normalize_mw_trace(a.trace)
        with open(a.out, 'w') as f:
            for ev in events:
                f.write(json.dumps(ev, ensure_ascii=False) + '\n')
        print(f'{len(events)} events -> {a.out}')
        return 0

    if a.cmd == 'diff':
        wine_events = load_wine_events(a.wine_dir)
        mw_events = normalize_mw_trace(a.mw_trace)
        rep = diff(wine_events, mw_events)
        out = Path(a.out)
        out.mkdir(parents=True, exist_ok=True)
        with open(out / 'diff.json', 'w') as f:
            json.dump(rep, f, indent=2)
        s = rep['summary']
        md = ['# Wine vs MiniWin differential', '',
              f'- generated: {rep["generated_utc"]}',
              f'- wine events: {s["wine_events"]} '
              f'({s["wine_unique_functions"]} unique functions)',
              f'- miniwin events: {s["miniwin_events"]} '
              f'({s["miniwin_unique_functions"]} unique functions)',
              '',
              f'## First meaningful divergence',
              '']
        fd = s['first_divergence']
        if fd:
            md += [f'- `{fd["function"]}` — {fd["why"]} '
                   f'(wine calls: {fd["wine_calls_total"]})', '']
        else:
            md += ['- none found (every Wine app-attributed function was '
                   'also called by MiniWin)', '']
        md += ['## Missing calls (Wine observed, MiniWin never)', '',
               'COVERAGE NOTE: MiniWin traces only the calls its mw_*',
               'implementations log — a function listed here may be',
               'implemented-but-unlogged rather than absent. Verify against',
               'the compat database before treating it as a gap.', '',
               '| function | wine calls | app-attributed |', '|---|---|---|']
        for m in rep['missing'][:100]:
            md.append(f'| {m["function"]} | {m["wine_calls"]} | {m["app_attributed"]} |')
        md += ['', '## Return-value mismatches (deterministic functions)',
               '', 'Pointer-valued returns are class-compared (each runtime maps',
               'its own address space); only null-vs-non-null and numeric',
               'differences count.', '',
               '| function | wine | miniwin |', '|---|---|---|']
        for m in rep['retval_mismatches'][:60]:
            md.append(f'| {m["function"]} | {m["wine"]} | {m["miniwin"]} |')
        md += ['', '## Frequency deltas', '',
               '| function | wine | miniwin |', '|---|---|---|']
        for m in rep['frequency_deltas'][:60]:
            md.append(f'| {m["function"]} | {m["wine"]} | {m["miniwin"]} |')
        with open(out / 'diff.md', 'w') as f:
            f.write('\n'.join(md) + '\n')
        print(json.dumps(s, indent=2))
        return 0
    return 1


if __name__ == '__main__':
    sys.exit(main())
