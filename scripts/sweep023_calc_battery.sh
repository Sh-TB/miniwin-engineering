#!/bin/bash
# SWEEP-023 SW-999: calc acceptance + NEW child-focus/keypad battery on the
# rebuilt (zig-restored) binaries. 4 scenarios x3 runs = 12/12 required:
#   A/C/D: button-click arithmetic on tests/gui/calc.exe (standing acceptance)
#   K:     miniwin_calc WM_KEYDOWN battery 1,2,+,3,Enter,ESC -> "1+2=3"
#   F:     NEW focus walk: Tab/arrow/Enter stream into miniwin_calc — program
#          must survive the whole walk and exit 0 with frames rendered
#          (child-focus routing regression guard).
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CALC="$REPO/tests/gui/calc.exe"
MCALC="$REPO/artifacts/sweep023/miniwin_calc.exe"
OUT="$REPO/artifacts/sweep023/calc_battery"
mkdir -p "$OUT"
pass=0; fail=0
run_clicks() { # name clicks expected
  local name="$1" clicks="$2" want="$3"
  for i in 1 2 3; do
    local d="$OUT/${name}_run${i}"
    mkdir -p "$d"
    ( cd "$d" && MW_GUI_EVENTS=1 MINWIN_CLICKS="$clicks" MINWIN_SCREENSHOT=0 \
      timeout 10 "$REPO/minwin_loader" "$CALC" >stdout.txt 2>stderr.txt )
    local code=$?
    if [ "$code" = "0" ] && grep -q "expr='.*=$want'" "$d/stdout.txt"; then
      echo "PASS $name run$i ($want)"; pass=$((pass+1))
    else
      echo "FAIL $name run$i exit=$code expected=$want"; fail=$((fail+1))
    fi
  done
}
run_keys() { # name keys
  local name="$1" keys="$2"
  for i in 1 2 3; do
    local d="$OUT/${name}_run${i}"
    mkdir -p "$d"
    ( cd "$d" && MINWIN_TRACE=1 MW_GUI_EVENTS=1 MW_GUI_KEYS="$keys" \
      timeout 20 "$REPO/minwin_loader" "$MCALC" >stdout.txt 2>stderr.txt )
    local code=$?
    local tr="$d/miniwin-results/miniwin_calc.exe/api_trace.json"
    if [ "$code" = "0" ] \
       && grep -q "TextOutA(20,20) '12+3='" "$tr" \
       && grep -q "TextOutA(280,52) '15'" "$tr"; then
      echo "PASS $name run$i (12+3=15 via WM_KEYDOWN)"; pass=$((pass+1))
    else
      echo "FAIL $name run$i exit=$code"; fail=$((fail+1))
    fi
  done
}
run_focus() { # name keys
  local name="$1" keys="$2"
  for i in 1 2 3; do
    local d="$OUT/${name}_run${i}"
    mkdir -p "$d"
    ( cd "$d" && MW_GUI_EVENTS=1 MW_GUI_KEYS="$keys" \
      timeout 20 "$REPO/minwin_loader" "$MCALC" >stdout.txt 2>stderr.txt )
    local code=$?
    local frames=$(ls "$d/miniwin-results/miniwin_calc.exe/screenshots/"frame_*.bmp 2>/dev/null | wc -l)
    if [ "$code" = "0" ] && [ "$frames" -gt 0 ]; then
      echo "PASS $name run$i (exit=0 frames=$frames)"; pass=$((pass+1))
    else
      echo "FAIL $name run$i exit=$code frames=$frames"; fail=$((fail+1))
    fi
  done
}
run_clicks A "109,110,116,105,106,114"     57  # 1 2 + 4 5 =
run_clicks C "103,108,102,114"             72  # 9 * 8 =
run_clicks D "102,105,104,109,110,114"      7  # 8 4 / 1 2 =
run_keys   K "49,50,187,51,13,27"              # 1 2 + 3 Enter ESC -> 1+2=3
run_focus  F "9,9,9,9,37,39,40,38,9,13,27"    # Tab x4 + arrows + Tab + Enter + ESC
echo "== SWEEP-023 calc battery: $pass pass / $fail fail (need 15 pass) =="
[ "$fail" = "0" ] && [ "$pass" = "15" ]
