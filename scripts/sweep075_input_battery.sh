#!/bin/bash
# SWEEP-075: puzzle input battery — 3 cold runs per puzzle; scripted mouse
# clicks at board center + offset; verdict = launch OK + frames captured +
# post-input frame differs from the initial one.
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
D=artifacts/sweep074/dl
run_puzzle() {
    local p="$1" exe="$2"
    local base=$(basename "$exe" .exe)
    local out=""
    for run in 1 2 3; do
        rm -rf "miniwin-results/$base.exe"
        MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=60 \
        MINWIN_MOUSE="120,120;200,150;160,200" MINWIN_MOUSE_EVERY=4 \
        timeout 60 ./minwin_loader "$exe" > "/tmp/sw75in_${p}_r${run}.log" 2>&1
        local rc=$?
        local frames=$(ls "miniwin-results/$base.exe/screenshots/" 2>/dev/null | wc -l)
        local crash=$(grep -ac '\[CRASH\]' "/tmp/sw75in_${p}_r${run}.log")
        out="$out rc=$rc/$frames/$crash"
    done
    # state-change check on run 3's frames
    local changed=$(python3 - "$base" <<'PYEOF'
import sys, os, hashlib
d = f"miniwin-results/{sys.argv[1]}.exe/screenshots"
try:
    fs = sorted(os.listdir(d))
    h0 = hashlib.md5(open(f"{d}/{fs[0]}","rb").read()).hexdigest()
    diff = any(hashlib.md5(open(f"{d}/{f}","rb").read()).hexdigest() != h0 for f in fs[1:])
    print(len(fs), "Y" if diff else "N")
except Exception:
    print("0", "N")
PYEOF
)
    echo -e "$p\t$base\t$out\tframes=$changed"
}
export -f run_puzzle
{
run_puzzle P01 $D/blackbox.exe
run_puzzle P02 $D/bridges.exe
run_puzzle P03 $D/cube.exe
run_puzzle P04 $D/dominosa.exe
run_puzzle P05 $D/fifteen.exe
run_puzzle P06 $D/flood.exe
run_puzzle P07 $D/guess.exe
run_puzzle P08 $D/lightup.exe
run_puzzle P09 $D/map.exe
run_puzzle P10 $D/mines.exe
run_puzzle P11 $D/netgame.exe
run_puzzle P12 $D/pattern.exe
run_puzzle P13 $D/pegs.exe
run_puzzle P14 $D/range.exe
run_puzzle P15 $D/rect.exe
run_puzzle P16 $D/samegame.exe
run_puzzle P17 $D/signpost.exe
run_puzzle P18 $D/slant.exe
run_puzzle P19 $D/solo.exe
run_puzzle P20 $D/towers.exe
} | tee evidence/sweep075/input_battery.tsv
echo "=== SWEEP-075 input battery complete ==="
