#!/bin/bash
# SWEEP-014 — GUI calculator acceptance (3 scenarios x 3 runs, 9/9 required)
# Scenarios (button ids, tests/gui/calc.c grid):
#   A: 12+45=57   C: 9*8=72   D: 84/12=7   (D added this sweep)
# Requires MW_GUI_EVENTS=1 (pump v3 arm) + MINWIN_CLICKS.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
EXE="$REPO/tests/gui/calc.exe"
OUT="$REPO/artifacts/sweep022_calc"
mkdir -p "$OUT"
pass=0; fail=0
run_case() { # name clicks expected
  local name="$1" clicks="$2" want="$3"
  for i in 1 2 3; do
    local d="$OUT/${name}_run${i}"
    mkdir -p "$d"
    ( cd "$d" && MW_GUI_EVENTS=1 MINWIN_CLICKS="$clicks" MINWIN_SCREENSHOT=0 \
      timeout 10 "$REPO/minwin_loader" "$EXE" >stdout.txt 2>stderr.txt )
    local code=$?
    if [ "$code" = "0" ] && grep -q "expr='.*=$want'" "$d/stdout.txt"; then
      echo "PASS $name run$i ($want)"; pass=$((pass+1))
    else
      echo "FAIL $name run$i exit=$code expected=$want"; fail=$((fail+1))
    fi
  done
}
run_case A "109,110,116,105,106,114"     57  # 1 2 + 4 5 =
run_case C "103,108,102,114"             72  # 9 * 8 =
run_case D "102,105,104,109,110,114"      7  # 8 4 / 1 2 =
echo "== calc acceptance: $pass pass / $fail fail (need 9 pass) =="
[ "$fail" = "0" ] && [ "$pass" = "9" ]
