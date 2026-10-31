#!/bin/bash
# IMalloc vtable ABI regression runner (SW-1501, APP-TEST-2).
#
# Builds the engine once with its main() renamed aside, links the ABI test
# against the REAL engine object code, and executes it. Exit 0 = all pass.
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC=${CC:-gcc}
OUT="$DIR/test_imalloc_abi_bin"

echo "[1/2] building engine + test link..."
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wno-unused-function -no-pie -I"$ROOT/include" \
    -Dmain=minwin_loader_main_unused \
    -o "$OUT" \
    "$ROOT/src/loader/loader.c" "$DIR/test_imalloc_abi.c" \
    -ldl -lm -lsqlite3 -Wl,-Ttext-segment=0x2000000 2>"$DIR/build.log"
if [ $? -ne 0 ]; then
    echo "BUILD FAILED:"; tail -20 "$DIR/build.log"; exit 2
fi

echo "[2/2] running vtable ABI test..."
"$OUT"
rc=$?
rm -f "$OUT"
if [ $rc -eq 0 ]; then echo "IMALLOC-ABI: ALL PASS"; else echo "IMALLOC-ABI: FAILED (rc=$rc)"; fi
exit $rc
