#!/bin/bash
# TASK-TEST-004 v2 — dispatch tests driven by the REAL loader (non-vacuous).
#
# The old test_dispatch_harness.c re-implemented the dispatcher, so it could
# pass even when the loader's dispatch never ran (state.md inconsistency #5).
# This harness runs tests/dispatch_tests/test_*.exe through ./minwin_loader
# and asserts evidence from the loader's OWN api_trace.json.
#
# Status after TEST-004 slice 2 (B-1/B-2/B-3 closed in the builder):
#   DISPATCH contract: all 4 tests raise through the real IAT call, reach
#   RtlDispatchException, have a real handler invoked by the loader, get a
#   disposition back, and resume cleanly. No SIGSEGV anywhere.
#   Exit-code contract: after ContinueExecution resume, the mw_RaiseException
#   stub's return value (1) is still in EAX, so the EP returns 1 and the
#   loader exits 1 — deterministic and trace-evidenced ("Entry point
#   returned 1"). exit 1 here means SUCCESS for these tests.
#   Open items queued as micro-tasks (not asserted yet):
#     C-1: test_c's ContinueSearch handler at frame 1 is dead code today —
#          the frame-0 handler answers first. Restructure so the INNER
#          handler returns ContinueSearch and an OUTER handler terminates
#          (two HANDLER_CALL events: disposition=1 then disposition=0).
#     D-1: test_a and test_d share the same walk shape after slice 2
#          (frame-1 catch + ContinueExecution). Differentiate or merge.
#
# Exit: 0 = all dispatch contracts hold.
#
# NOTE: rows assert loader OBSERVABLES from api_trace.json only — never the
# loader's source. Evolving a row requires a trace citation.

set -uo pipefail
REPO="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
cd "$REPO" || exit 1
LOADER="$REPO/minwin_loader"
DIR="$REPO/tests/dispatch_tests"
PASS=0; FAIL=0; KNOWN_BROKEN=0

events() { # $1 = results subdir name -> prints [API]-prefixed trace events
    python3 - "$REPO/miniwin-results/$1/api_trace.json" <<'PYEOF'
import sys
try:
    for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
        s = line.strip()
        if s.startswith('[API] '):
            print(s[6:])
except FileNotFoundError:
    pass
PYEOF
}

run_case() { # $1=exe  $2=expected-exit(int|"any")  $3='|'-separated required-trace-substrings  $4=known-broken(0|1)  $5=label
    local exe="$1" exp_ec="$2" need="$3" broken="$4" label="$5"
    local base; base=$(basename "$exe")
    rm -rf "$REPO/miniwin-results"
    "$LOADER" "$exe" >/dev/null 2>/tmp/t004_stderr.txt
    local ec=$?
    local ev; ev=$(events "$base")
    local ok=1 missing=""
    [ "$exp_ec" != "any" ] && [ "$ec" -ne "$exp_ec" ] && { ok=0; missing="ec=$ec (want $exp_ec)"; }
    local IFS='|'
    for sub in $need; do
        if ! grep -q "$sub" <<<"$ev"; then
            ok=0; missing="${missing:+$missing }missing='$sub'"
        fi
    done
    unset IFS

    if [ "$broken" = "1" ]; then
        if [ "$ok" -eq 0 ]; then
            echo "  KNOWN-BROKEN (documented): $label"; KNOWN_BROKEN=$((KNOWN_BROKEN+1))
        else
            echo "  UPGRADE ME — known-broken case now satisfies its contract: $label (move to a real PASS row)"; FAIL=$((FAIL+1))
        fi
    elif [ "$ok" -eq 1 ]; then
        echo "  PASS: $label"; PASS=$((PASS+1))
    else
        echo "  FAIL: $label ($missing)"; FAIL=$((FAIL+1))
    fi
}

echo "== TASK-TEST-004 v2: real-loader dispatch tests (slice 2 contracts) =="

# Dispatch contract rows — every test must:
#   (1) exit with the documented EP-return code (1 = stub EAX after resume),
#   (2) show RtlDispatchException ran (real raise through the IAT),
#   (3) show a handler was actually invoked and returned a disposition,
#   (4) show the ContinueExecution resume path completed.
DISPATCH_EVID='RtlDispatchException|HANDLER_CALL\] returned disposition=0|Handler returned ContinueExecution — resuming'

run_case "$DIR/test_a.exe" 1 "$DISPATCH_EVID" 0 "test_a: raise in func_C -> frame-1 catch (func_B), ContinueExecution resume (B-3 closed)"
run_case "$DIR/test_b.exe" 1 "$DISPATCH_EVID" 0 "test_b: raise in func_C -> frame-0 catch (func_C's own handler), resume (B-1 closed)"
run_case "$DIR/test_c.exe" 1 "$DISPATCH_EVID" 0 "test_c: dispatch reached, frame-0 catch, resume (real ContinueSearch chain = C-1, queued)"
run_case "$DIR/test_d.exe" 1 "$DISPATCH_EVID" 0 "test_d: raise in func_C -> frame-1 catch (func_B), ContinueExecution resume"

rm -rf "$REPO/miniwin-results"
echo "== Results: $PASS passed, $FAIL failed, $KNOWN_BROKEN known-broken-as-documented =="
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
