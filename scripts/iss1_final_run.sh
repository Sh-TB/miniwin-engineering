#!/bin/bash
# ISS1-FINAL — final 6-target acceptance run for Issue #1 Phase 1.
# 3x repeats each (§11.4), evidence to evidence/master/issue1_final_<ts>/
set -uo pipefail
REPO=/home/z/my-project/workspace/miniwin-engineering
LOADER=$REPO/minwin_loader
EV=$REPO/evidence/master/issue1_final_$(date -u +%Y%m%d_%H%M%S)
mkdir -p "$EV"; echo "$EV" > /tmp/iss1_final_ev

declare -A TARGS=(
  [upx_424]="$REPO/samples/upx_decompressed.exe --version"
  [t08_big_cpp_stl]="$REPO/tests/corpus/bin/t08_big_cpp_stl.exe"
  [t09_recon_c_smoke]="$REPO/tests/corpus/bin/t09_recon_c_smoke.exe"
  [t10_float_math]="$REPO/tests/corpus/bin/t10_float_math.exe"
  [t11_abi_deep]="$REPO/tests/corpus/bin/t11_abi_deep.exe"
  [t12_libc_io]="$REPO/tests/corpus/bin/t12_libc_io.exe"
)

for name in upx_424 t09_recon_c_smoke t10_float_math t11_abi_deep t12_libc_io t08_big_cpp_stl; do
  exe="${TARGS[$name]}"
  cd "$REPO"
  rm -f t12_testfile.bin t12_text.txt t12_probe.txt
  for i in 1 2 3; do
    timeout 90 $LOADER $exe > "$EV/${name}_run$i.stdout" 2> "$EV/${name}_run$i.stderr"
    ec=$?
    s=$(sha256sum "$EV/${name}_run$i.stdout" | awk '{print $1}' | head -c 12)
    crash=$(grep -c '\[CRASH\]' "$EV/${name}_run$i.stderr" 2>/dev/null || echo 0)
    echo "$name run$i: exit=$ec stdout_sha12=$s crash_lines=$crash"
  done
done
