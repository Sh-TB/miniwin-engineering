#!/bin/bash
# TASK-G1-001 — G1 acceptance pin (one command, evidence-producing).
#
# Re-establishes the G1 acceptance state after ANY loader.c change:
#   1. clean build (0 errors enforced)
#   2. UPX --version must print the exact banner and exit 0
#   3. stderr must be free of [CRASH] dumps (crash_handler unused)
#   4. regression suite must be 12/12 (test-10 = G1 success state)
#   5. record binary hash + trace + stdout into evidence/master/g1/<ts>/
#
# Usage: scripts/g1_accept.sh [repo_root]   (default: script's grandparent dir)
# Exit:  0 = G1 pinned; 1 = any check failed (do NOT claim G1 without this)

set -uo pipefail

REPO="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$REPO" || exit 1
TS=$(date -u +%Y%m%d_%H%M%S)
EV="evidence/master/g1/$TS"
FAIL=0

say()  { printf '%s\n' "$*"; }
fail() { printf 'G1-PIN FAIL: %s\n' "$*" >&2; FAIL=1; }

say "== G1 acceptance pin @ $(date -u +%FT%TZ) =="

# 1. clean build
say "-- clean build"
make clean >/dev/null 2>&1
if ! make > /tmp/g1_build.log 2>&1; then
    fail "build errors (see /tmp/g1_build.log)"; say "== G1 PIN: FAILED =="; exit 1
fi
ERRS=$(grep -cE ' error' /tmp/g1_build.log || true)
[ "$ERRS" -eq 0 ] || fail "build reported $ERRS error lines"
HASH=$(sha256sum minwin_loader | awk '{print $1}')
say "   binary sha256 = $HASH"

# 2+3. run UPX --version
say "-- run: ./minwin_loader samples/upx_decompressed.exe --version"
rm -rf miniwin-results/upx_decompressed.exe
./minwin_loader samples/upx_decompressed.exe --version > /tmp/g1_stdout.txt 2> /tmp/g1_stderr.txt
EC=$?
[ "$EC" -eq 0 ] || fail "exit code $EC (expected 0)"
grep -q '^upx 4\.2\.4$' /tmp/g1_stdout.txt || fail "missing exact 'upx 4.2.4' version line"
grep -q 'UPX comes with ABSOLUTELY NO WARRANTY' /tmp/g1_stdout.txt || fail "missing no-warranty banner line"
if grep -q '\[CRASH\]' /tmp/g1_stderr.txt; then fail "[CRASH] dump present in stderr"; fi

# 4. regression
say "-- regression suite"
if bash tests/regression/test_pe_loader_regression.sh "$REPO" > /tmp/g1_reg.txt 2>&1; then
    say "   $(grep 'Results:' /tmp/g1_reg.txt | tail -1)"
else
    fail "regression suite failed (see /tmp/g1_reg.txt)"
fi

# 5. evidence
mkdir -p "$EV"
cp /tmp/g1_stdout.txt "$EV/upx_stdout.txt"
cp /tmp/g1_stderr.txt "$EV/upx_stderr.txt"
cp /tmp/g1_reg.txt     "$EV/regression.txt" 2>/dev/null || true
cp miniwin-results/upx_decompressed.exe/api_trace.json "$EV/upx_trace.json" 2>/dev/null || true
{ echo "timestamp_utc=$TS"; echo "binary_sha256=$HASH"; } > "$EV/hashes.txt"
say "-- evidence recorded in $EV"

if [ "$FAIL" -eq 0 ]; then
    say "== G1 PIN: OK (exit 0, banner exact, no crash, regression green) =="
    exit 0
else
    say "== G1 PIN: FAILED =="
    exit 1
fi
