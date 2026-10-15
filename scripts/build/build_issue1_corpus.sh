#!/bin/bash
# ISS1-BUILD — build the 5 recreated Issue #1 Phase-1 fixtures with mingw-w64.
# Produces PE32+ x64 console binaries, statically linked (kernel32-only imports
# where possible), deterministic behavior, self-checking exit codes.
set -euo pipefail
export PATH=/home/z/my-project/scripts/mingw-debs/root/usr/bin:$PATH
REPO=/home/z/my-project/workspace/miniwin-engineering
SRC=$REPO/tests/corpus
OUT=$REPO/tests/corpus/bin
mkdir -p "$OUT"
cd "$SRC"

CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++
CFLAGS="-O2 -static -Wall -Wextra"

echo "== building t08_big_cpp_stl (C++) =="
$CXX $CFLAGS -o "$OUT/t08_big_cpp_stl.exe" t08_big_cpp_stl.cpp

echo "== building t09_recon_c_smoke (C) =="
$CC $CFLAGS -o "$OUT/t09_recon_c_smoke.exe" t09_recon_c_smoke.c

echo "== building t10_float_math (C) =="
$CC $CFLAGS -o "$OUT/t10_float_math.exe" t10_float_math.c -lm

echo "== building t11_abi_deep (C, host-verified expected) =="
$CC $CFLAGS -DHOST_PRESERVE_EXPECTED=1761356 -o "$OUT/t11_abi_deep.exe" t11_abi_deep.c

echo "== building t12_libc_io (C) =="
$CC $CFLAGS -o "$OUT/t12_libc_io.exe" t12_libc_io.c

echo "== artifacts =="
file "$OUT"/*.exe
sha256sum "$OUT"/*.exe | tee "$OUT/SHA256SUMS"
