#!/bin/bash
# APPCOMPAT sweep runner — run every compiled test on the MiniWin loader,
# capture stdout/stderr/exit code into evidence dir.
# Usage: bash run_sweep.sh [binary_dir] [out_dir]
set -u
LOADER=/home/z/my-project/workspace/miniwin-engineering/minwin_loader
BIN_DIR=${1:-/home/z/my-project/workspace/miniwin-engineering/tests/appcompat}
OUT_DIR=${2:-/home/z/my-project/workspace/miniwin-engineering/evidence/master/appcompat/run-$(date +%Y%m%d_%H%M%S)}
mkdir -p "$OUT_DIR"
RUNDIR=$(mktemp -d /tmp/appcompat_run_XXXXXX)
cd "$RUNDIR" || exit 1

TESTS="t01_hello t02_math t03_mem t04_string t05_file t06_setjmp t07_big_c_app t08_big_cpp_stl t09_big_cpp_iostream"
echo "test,exit,signal,class" > "$OUT_DIR/summary.csv"

for t in $TESTS; do
    exe="$BIN_DIR/$t.exe"
    [ -f "$exe" ] || { echo "$t: MISSING BINARY" | tee -a "$OUT_DIR/summary.csv" >/dev/null; continue; }
    stdout_f="$OUT_DIR/${t}.stdout"
    stderr_f="$OUT_DIR/${t}.stderr"
    timeout 90 "$LOADER" "$exe" >"$stdout_f" 2>"$stderr_f"
    rc=$?
    # classify
    if [ $rc -eq 0 ] && ! grep -q "FAIL:" "$stdout_f"; then class=PASS
    elif grep -q "PASS" "$stdout_f" && grep -q "FAIL:" "$stdout_f"; then class=VERIFY_FAIL
    elif [ $rc -eq 139 ] || [ $rc -eq 134 ] || [ $rc -eq 138 ] || [ $rc -eq 136 ]; then class=CRASH
    elif [ $rc -eq 124 ]; then class=TIMEOUT
    else class=FAIL; fi
    echo "$t,$rc,,$class" >> "$OUT_DIR/summary.csv"
    echo "=== $t -> exit=$rc class=$class"
    grep -E "FAIL:|PASS|WARN.*unresolved" "$stdout_f" "$stderr_f" 2>/dev/null | head -4
done
echo; echo "=== SUMMARY ==="; cat "$OUT_DIR/summary.csv"
echo "OUT_DIR=$OUT_DIR"
