#!/bin/bash
# ISS1-RUN — run all 6 Issue #1 Phase-1 targets through the MiniWin loader,
# 3x each per §11.4. Records exit code, stdout, stderr per run into evidence.
set -uo pipefail
REPO=/home/z/my-project/workspace/miniwin-engineering
LOADER=$REPO/minwin_loader
EV=$REPO/evidence/master/issue1_run_$(date -u +%Y%m%d_%H%M%S)
mkdir -p "$EV"
echo "$EV" > /tmp/iss1_run_ev

declare -A TARGS=(
  [t08_big_cpp_stl]="$REPO/tests/corpus/bin/t08_big_cpp_stl.exe"
  [t09_recon_c_smoke]="$REPO/tests/corpus/bin/t09_recon_c_smoke.exe"
  [t10_float_math]="$REPO/tests/corpus/bin/t10_float_math.exe"
  [t11_abi_deep]="$REPO/tests/corpus/bin/t11_abi_deep.exe"
  [t12_libc_io]="$REPO/tests/corpus/bin/t12_libc_io.exe"
)

for name in t09_recon_c_smoke t10_float_math t11_abi_deep t12_libc_io t08_big_cpp_stl; do
  exe="${TARGS[$name]}"
  for i in 1 2 3; do
    out="$EV/${name}_run$i.stdout"; err="$EV/${name}_run$i.stderr"
    cd "$REPO"
    timeout 90 "$LOADER" "$exe" > "$out" 2> "$err"
    ec=$?
    echo "$name run$i: exit=$ec stdout_bytes=$(wc -c < "$out")"
    if grep -q '\[CRASH\]' "$err" 2>/dev/null; then
      echo "$name run$i: CRASH -> $(head -3 "$err" | tail -2)"
    fi
  done
done
echo "EV_DIR=$EV"
