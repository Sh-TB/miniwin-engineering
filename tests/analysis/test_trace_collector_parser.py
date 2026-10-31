#!/usr/bin/env python3
"""Parser regression tests for scripts/analysis/trace_collector.py.

ALL TRACE DATA IN THIS FILE IS **SYNTHETIC TEST DATA**: hand-written lines in
the exact grammar Wine +relay emits. They are labeled synthetic here, in the
fixture header written to any output, and in the test names. They exist ONLY
to pin the parser's pairing/retval behavior; they are NEVER evidence of real
Wine behavior (real-behavior validation requires an actual Wine capture).

Run:  python3 tests/analysis/test_trace_collector_parser.py
Exit 0 = all pass.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'scripts', 'analysis'))
import trace_collector as tc  # noqa: E402

SYNTHETIC = "SYNTHETIC-TEST-DATA (hand-written relay grammar, not a Wine capture)"

PASS = 0
FAIL = 0


def check(name, cond, detail=''):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  PASS: {name}")
    else:
        FAIL += 1
        print(f"  FAIL: {name} {detail}")


def parse_lines(lines):
    """Write a synthetic relay trace (clearly labeled in a header comment
    line the parser ignores) and run the real parser over it."""
    with tempfile.NamedTemporaryFile('w', suffix='.raw', delete=False) as f:
        f.write(f"# {SYNTHETIC}\n")
        f.write('\n'.join(lines))
        path = f.name
    try:
        return tc.parse_relay_trace(path)
    finally:
        os.unlink(path)


def test_nested_same_thread_lifo():
    # Wine nests calls on one tid: A calls B; B's Ret must pair with B, not A.
    evs = parse_lines([
        '00a1:Call kernel32.CreateFileW(...) ret=00401000',
        '00a1:Call kernelbase_strlen(...) ret=00402000',
        '00a1:Ret  kernelbase_strlen() retval=00000005 ret=00402000',
        '00a1:Ret  kernel32.CreateFileW() retval=00001234 ret=00401000',
    ])
    calls = [e for e in evs if 'parser_event' not in e]
    check("nested: 2 paired calls", len(calls) == 2, f"got {len(calls)}")
    by_func = {c['function']: c for c in calls}
    check("nested: inner pairs with inner",
          by_func['kernelbase_strlen']['return_value'] == '00000005')
    check("nested: outer pairs with outer",
          by_func['kernel32.CreateFileW']['return_value'] == '00001234')
    check("nested: outer pushed first (LIFO order)",
          calls[0]['function'] == 'kernelbase_strlen')


def test_missing_retval_not_fabricated():
    evs = parse_lines([
        '00a2:Call kernel32.ExitProcess(00000000) ret=00401000',
        '00a2:Ret  kernel32.ExitProcess() ret=00401000',  # no retval field
    ])
    calls = [e for e in evs if 'parser_event' not in e]
    check("retval: void Ret yields None", calls and calls[0].get('return_value') is None)
    check("retval: known flag False", calls and calls[0].get('return_value_known') is False)


def test_unmatched_ret_reported():
    evs = parse_lines([
        '00a3:Ret  user32.GetMessageW() retval=00000001 ret=00401000',
    ])
    ev = next((e for e in evs if e.get('parser_event') == 'unmatched_rets'), None)
    check("unmatched: surfaced as parser_event", ev is not None)
    check("unmatched: count 1", ev and ev['count'] == 1)
    calls = [e for e in evs if 'parser_event' not in e]
    check("unmatched: not attached to a fake call", len(calls) == 0)


def test_threads_do_not_cross_pair():
    evs = parse_lines([
        '00a4:Call kernel32.Sleep(00000064) ret=00401000',
        '00a5:Call kernel32.Sleep(000000c8) ret=00401000',
        '00a5:Ret  kernel32.Sleep() retval=00000000 ret=00401000',
        '00a4:Ret  kernel32.Sleep() retval=00000000 ret=00401000',
    ])
    calls = [e for e in evs if 'parser_event' not in e]
    check("threads: 2 calls paired within own tid", len(calls) == 2)
    check("threads: no unmatched event", not any(e.get('parser_event') for e in evs))
    check("threads: args kept per call",
          {c['thread_id']: c['arguments'] for c in calls} ==
          {'00a4': '00000064', '00a5': '000000c8'})


def test_truncated_call_at_eof():
    evs = parse_lines([
        '00a6:Call kernel32.WaitForSingleObject(00000abc,00001388) ret=00401000',
    ])
    calls = [e for e in evs if 'parser_event' not in e]
    check("truncated: EOF-open call emitted", len(calls) == 1)
    check("truncated: marked truncated_at_eof", calls and calls[0].get('truncated_at_eof') is True)
    check("truncated: retval None (no invention)", calls and calls[0].get('return_value') is None)


def test_pedll_pairing():
    evs = parse_lines([
        '00a7:Call PE DLL (proc=00001234,module=00005678  L"KERNEL32",reason=PROCESS_DETACH,res=00000001) ret=00000000',
        '00a7:Ret  PE DLL (proc=00001234,module=00005678  L"KERNEL32",reason=PROCESS_DETACH,res=00000001) retval=00000001 ret=00000000',
    ])
    calls = [e for e in evs if 'parser_event' not in e]
    check("pedll: paired", len(calls) == 1)
    check("pedll: function name carries module",
          calls and 'KERNEL32' in calls[0]['function'])
    check("pedll: retval captured", calls and calls[0]['return_value'] == '00000001')


def main():
    print(f"trace_collector parser regression — {SYNTHETIC}")
    test_nested_same_thread_lifo()
    test_missing_retval_not_fabricated()
    test_unmatched_ret_reported()
    test_threads_do_not_cross_pair()
    test_truncated_call_at_eof()
    test_pedll_pairing()
    print(f"RESULT: {PASS} passed, {FAIL} failed (all fixtures: SYNTHETIC-TEST-DATA)")
    return 1 if FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
