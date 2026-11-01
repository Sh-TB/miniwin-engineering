#!/usr/bin/env python3
"""Tests for the L0 tracer/normalizer/differential tools (mission Phase 7).

SYNTHETIC FIXTURES: trace strings below are hand-written parser fixtures,
clearly labeled. They are NOT Wine evidence. The real-trace integration
test at the bottom validates against artifacts captured by l0_tracer.py
from real Wine runs (skipped when absent).

Run: python3 -m unittest tests.analysis.test_l0_tools -v
"""

import json
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts" / "analysis"))

import l0_tracer as t0          # noqa: E402
import l0_normalize as tnorm    # noqa: E402


def normalize_lines(lines, module_map=None):
    tmp = Path(tempfile.mkdtemp())
    raw = tmp / "synthetic_relay.log"
    raw.write_text("\n".join(lines) + "\n")
    jw = t0.JsonlWriter(tmp / "ev.jsonl")
    norm = t0.RelayNormalizer(jw, module_map)
    for ln in lines:
        norm.feed(ln)
    rep = norm.finish()
    jw.close()
    events = [json.loads(l) for l in open(tmp / "ev.jsonl")]
    return events, rep


class TestRelayPairing(unittest.TestCase):
    def test_nested_calls_paired_lifo(self):
        events, rep = normalize_lines([
            "0024:Call user32.DispatchMessageW(0022fdf8) ret=004013a5",
            "0024:Call windowproc(1,2,3,4) ret=7b867baf",
            "0024:Ret  windowproc() retval=00000000 ret=7b867baf",
            "0024:Ret  user32.DispatchMessageW() retval=00000000 ret=004013a5",
        ])
        pairs = [e for e in events if e["kind"] == "call_pair"]
        self.assertEqual([p["function"] for p in pairs],
                         ["windowproc", "user32.DispatchMessageW"])
        self.assertEqual(rep["completed_pairs"], 2)

    def test_interleaved_tids(self):
        events, rep = normalize_lines([
            "00aa:Call kernel32.CreateFileW(80001234) ret=00bda1d6",
            "00bb:Call kernel32.GetLastError() ret=00cda2e7",
            "00bb:Ret  kernel32.GetLastError() retval=000000b7 ret=00cda2e7",
            "00aa:Ret  kernel32.CreateFileW() retval=ffffffff ret=00bda1d6",
        ])
        pairs = {e["tid"]: e for e in events if e["kind"] == "call_pair"}
        self.assertEqual(pairs["00aa"]["retval"], "ffffffff")
        self.assertEqual(pairs["00bb"]["retval"], "000000b7")
        self.assertEqual(rep["limitations"].get("ret_addr_mismatch", 0), 0)

    def test_missing_retval_stays_unknown(self):
        events, rep = normalize_lines([
            "0024:Call kernel32.Sleep(64) ret=401122",
            "0024:Ret  kernel32.Sleep() ret=401122",
        ])
        pairs = [e for e in events if e["kind"] == "call_pair"]
        self.assertEqual(len(pairs), 1)
        self.assertIsNone(pairs[0]["retval"])
        self.assertFalse(pairs[0]["retval_known"])
        self.assertEqual(rep["limitations"].get("missing_retval", 0), 1)

    def test_malformed_retval_flagged(self):
        events, rep = normalize_lines([
            "0024:Call kernel32.Sleep(64) ret=401122",
            "0024:Ret  kernel32.Sleep() retval=zznothex ret=401122",
        ])
        pairs = [e for e in events if e["kind"] == "call_pair"]
        self.assertEqual(len(pairs), 1)
        self.assertIsNone(pairs[0]["retval"])
        self.assertEqual(rep["limitations"].get("malformed_retval", 0), 1)

    def test_wine10_pedll_no_trailing_ret(self):
        """Real wine-10.1 shape: PE DLL lines carry NO trailing ret=."""
        events, rep = normalize_lines([
            '002c:Call PE DLL (proc=00006FFFFFF41C90,module=00006FFFFFF30000 L"ntdll.dll",reason=PROCESS_ATTACH,res=00007FFFFE2FFB00)',
            '002c:Ret  PE DLL (proc=00006FFFFFF41C90,module=00006FFFFFF30000 L"ntdll.dll",reason=PROCESS_ATTACH,res=00007FFFFE2FFB00) retval=1',
        ])
        pairs = [e for e in events if e["kind"] == "call_pair"]
        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0]["function"], "DllMain[ntdll.dll]")
        self.assertEqual(pairs[0]["retval"], "1")
        self.assertIn("pe_dll_event", pairs[0]["flags"])

    def test_retaddr_mismatch_flagged(self):
        events, rep = normalize_lines([
            "0024:Call kernel32.Foo(1) ret=00aaaaaa",
            "0024:Ret  kernel32.Foo() retval=00000001 ret=00bbbbbb",
        ])
        pairs = [e for e in events if e["kind"] == "call_pair"]
        self.assertIn("ret_addr_mismatch", pairs[0]["flags"])
        self.assertEqual(rep["limitations"].get("ret_addr_mismatch", 0), 1)

    def test_orphan_repair_walks_stack(self):
        events, rep = normalize_lines([
            "0024:Call kernel32.Outer(1) ret=00aaaaaa",
            "0024:Call kernel32.Inner(2) ret=00bbbbbb",
            "0024:Ret  kernel32.Outer() retval=00000007 ret=00aaaaaa",
        ])
        by = {e["function"]: e for e in events if e["kind"] == "call_pair"}
        self.assertIn("orphaned_by_retaddr_repair", by["kernel32.Inner"]["flags"])
        self.assertIn("retaddr_repaired_pair", by["kernel32.Outer"]["flags"])

    def test_attribution_module_map(self):
        mmap = [{"lo": 0x140000000, "hi": 0x140100000,
                 "path": "Z:\\app\\test.exe", "attr": "app"}]
        events, _ = normalize_lines([
            "0024:Call kernel32.Beep(1,1) ret=140001234",
            "0024:Ret  kernel32.Beep() retval=00000001 ret=140001234",
        ], module_map=mmap)
        pairs = [e for e in events if e["kind"] == "call_pair"]
        self.assertEqual(pairs[0]["attribution"], "app")


class TestMwNormalizer(unittest.TestCase):
    def test_call_with_inline_and_continuation_results(self):
        tmp = Path(tempfile.mkdtemp()) / "api_trace.json"
        body = '\n'.join([
            '{', '  "trace": [',
            '    "[Tccc0] [API] _errno() = 0x331af38 (value=0)",',
            '    "[Tccc0] [API] CreateFileW(a,b,c)",',
            '    "[Tccc0] [API]   -> 0x44",',
            '    "[Tccc0] [API] PE Loader: file=x size=1",',
            '    "exit(0)"', '  ]', '}',
        ])
        tmp.write_text(body)
        evs = tnorm.normalize_mw_trace(str(tmp))
        by = {e["function"]: e for e in evs}
        self.assertEqual(by["_errno"]["retval"], "0x331af38")  # annotation stripped
        self.assertEqual(by["CreateFileW"]["retval"], "0x44")
        self.assertNotIn("PE Loader", by)

    def test_retclass_pointer_never_mismatches(self):
        self.assertEqual(tnorm._retclass("0x331af38"), "ptr")
        self.assertEqual(tnorm._retclass("0024eab0"), "ptr")
        self.assertEqual(tnorm._retclass("00000000"), "null")
        self.assertEqual(tnorm._retclass("00000005"), "num")

    def test_first_divergence(self):
        wine = [
            {"side": "wine", "kind": "call_pair", "function": "kernel32.A",
             "attribution": "app", "retval": None},
            {"side": "wine", "kind": "call_pair", "function": "kernel32.B",
             "attribution": "app", "retval": None},
        ]
        mw = [{"side": "miniwin", "kind": "call_pair", "function": "A",
               "attribution": "app", "retval": None}]
        rep = tnorm.diff(wine, mw)
        self.assertEqual(rep["summary"]["first_divergence"]["function"],
                         "kernel32.B")
        self.assertEqual(rep["summary"]["retval_mismatch_count"], 0)


class TestRealTraceIntegration(unittest.TestCase):
    """Integration against REAL Wine captures (not synthetic). Skipped when
    the artifacts are absent — never faked."""

    def setUp(self):
        cands = sorted((REPO / "artifacts" / "l0-experiments").glob(
            "L0-*upx_decompressed*/experiment.json"))
        self.exp = cands[-1] if cands else None

    def test_real_capture_pairs(self):
        if not self.exp:
            self.skipTest("no real L0 capture present")
        spec = json.loads(self.exp.read_text())
        relay = spec["normalization"].get("relay", {})
        self.assertGreater(relay.get("completed_pairs", 0), 5000,
                           "real UPX capture must yield thousands of pairs")
        self.assertEqual(spec["wine_version"].startswith("wine-10"), True,
                         spec["wine_version"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
