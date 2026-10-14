#!/bin/bash
# MiniWin PE Loader Regression Tests
# Verifies critical execution milestones are preserved across changes.
#
# Usage: ./tests/test_pe_loader_regression.sh [minwin_dir]
#   minwin_dir defaults to this script's parent directory.
#
# Exit codes: 0 = all pass, 1 = any fail

set -euo pipefail

MINWIN_DIR="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
LOADER="$MINWIN_DIR/minwin_loader"
SAMPLES="$MINWIN_DIR/samples"
PASS=0
FAIL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass_test() {
    PASS=$((PASS + 1))
    echo -e "  ${GREEN}PASS${NC}: $1"
}

fail_test() {
    FAIL=$((FAIL + 1))
    echo -e "  ${RED}FAIL${NC}: $1"
}

run_test() {
    local name="$1"
    local trace_file
    trace_file=$(mktemp /tmp/minwin_test_XXXXXX.json)

    echo "---"
    echo "Test: $name"

    # Run loader, capture exit code
    set +e
    "$LOADER" "$SAMPLES/upx_decompressed.exe" --version \
        2>/tmp/minwin_stderr_test \
        1>/tmp/minwin_stdout_test
    local exit_code=$?
    set -e

    # Find the api_trace.json (it's written to miniwin-results/)
    local api_trace="$MINWIN_DIR/miniwin-results/upx_decompressed.exe/api_trace.json"

    # --- Test 1: Loader doesn't crash before reaching EP ---
    if grep -q 'Jumping to EP=' "$api_trace" 2>/dev/null; then
        pass_test "Entry point reached"
    else
        fail_test "Entry point NOT reached (crash before EP)"
    fi

    # --- Test 2: Imports resolved (162 entries) ---
    local import_count
    import_count=$(grep -F -c '> 0x' "$api_trace" 2>/dev/null || true)
    if [ -z "$import_count" ]; then import_count=0; fi
    if [ "$import_count" -ge 150 ]; then
        pass_test "Imports resolved ($import_count entries)"
    else
        fail_test "Too few imports resolved ($import_count, expected >= 150)"
    fi

    # --- Test 3: CRT initialization (_initterm called) ---
    if grep -q '_initterm' "$api_trace" 2>/dev/null; then
        pass_test "CRT init (_initterm) executed"
    else
        fail_test "CRT init (_initterm) NOT executed"
    fi

    # --- Test 4: __getmainargs called (MSVC CRT startup) ---
    if grep -q '__getmainargs' "$api_trace" 2>/dev/null; then
        pass_test "__getmainargs executed"
    else
        fail_test "__getmainargs NOT executed"
    fi

    # --- Test 5: SetUnhandledExceptionFilter registered ---
    if grep -q 'SetUnhandledExceptionFilter' "$api_trace" 2>/dev/null; then
        pass_test "SetUnhandledExceptionFilter called"
    else
        fail_test "SetUnhandledExceptionFilter NOT called"
    fi

    # --- Test 6: RaiseException(0x20474343) reached ---
    if grep -q 'ExceptionCode=0x20474343' "$api_trace" 2>/dev/null; then
        pass_test "RaiseException(0x20474343) reached"
    else
        fail_test "RaiseException(0x20474343) NOT reached"
    fi

    # --- Test 7: .pdata parsed ---
    if grep -q 'Entries=3030' "$api_trace" 2>/dev/null; then
        pass_test ".pdata parsed (3030 entries)"
    else
        fail_test ".pdata NOT parsed correctly"
    fi

    # --- Test 8: RUNTIME_FUNCTION lookup worked ---
    if grep -q 'RF begin=' "$api_trace" 2>/dev/null; then
        pass_test "RUNTIME_FUNCTION lookup succeeded"
    else
        fail_test "RUNTIME_FUNCTION lookup NOT found"
    fi

    # --- Test 9: UNWIND_INFO parsed ---
    if grep -q 'handler=0x' "$api_trace" 2>/dev/null; then
        pass_test "UNWIND_INFO parsed"
    else
        fail_test "UNWIND_INFO NOT parsed"
    fi

    # --- Test 10: Exit code matches the current verified failure frontier ---
    # History:
    #   pre-BUG-026:  exit 134 (SIGABRT before any frame walk)
    #   post-BUG-026: exit 139 (SIGSEGV at landing-pad resume, RSP+8 bug)
    #   post-BUG-027: exit 134 again, but at a LATER, verified point:
    #                 landing pad 0x4dcd24 resumes with correct RSP
    #                 (post_prologue_RSP = establisher - 0x48, trace-verified
    #                 alloc_total=0x48), catch TLS machinery runs, then
    #                 terminate -> abort (BUG-029, see docs/bugs/BUG-029.md).
    #                 134 here is PROGRESS, not the old pre-walk abort.
    if [ "$exit_code" -eq 134 ] && grep -q "terminate called after throwing" /tmp/minwin_stdout_test 2>/dev/null; then
        pass_test "Exit code 134 (verified BUG-029 frontier: terminate after landing-pad resume)"
    else
        fail_test "Exit code $exit_code (expected 134 = BUG-029 frontier; 139 was the BUG-026-era expectation)"
    fi

    # --- Test 11: malloc/calloc work (HeapAlloc regression) ---
    if grep -q 'malloc(' "$api_trace" 2>/dev/null; then
        pass_test "malloc calls succeed"
    else
        fail_test "No malloc calls (HeapAlloc may be broken)"
    fi

    # --- Test 12: CriticalSection init works ---
    if grep -q 'InitializeCriticalSection' "$api_trace" 2>/dev/null; then
        pass_test "InitializeCriticalSection works"
    else
        fail_test "InitializeCriticalSection NOT called"
    fi

    # Cleanup
    rm -f "$trace_file" /tmp/minwin_stderr_test /tmp/minwin_stdout_test
}

# ============================================================
# Main
# ============================================================

echo "======================================="

echo "MiniWin Regression Test Suite"
echo "Loader: $LOADER"
echo "Date:   $(date -Iseconds)"
echo "======================================="

# Pre-flight checks
if [ ! -x "$LOADER" ]; then
    echo -e "${RED}FATAL: loader not found or not executable: $LOADER${NC}"
    exit 1
fi

if [ ! -f "$SAMPLES/upx_decompressed.exe" ]; then
    echo -e "${RED}FATAL: sample not found: $SAMPLES/upx_decompressed.exe${NC}"
    exit 1
fi

echo ""

run_test "UPX --version execution"

echo ""
echo "======================================="
echo -e "Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}"
echo "======================================="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi

exit 0
